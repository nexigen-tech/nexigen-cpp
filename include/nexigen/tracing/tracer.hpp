// nexigen::tracing — the tracer handle and the span passkey (design D8/D12;
// spec tracing/tracer-provider "Provider and tracer factory hierarchy",
// tracing/scoped-span).
//
// A Tracer is created EXCLUSIVELY by TracerProvider::get_tracer(scope_name)
// (private constructor, provider befriended) and lives in the provider's
// pre-allocated pool: no heap allocation, no locks on the span path.
//
// SpanPasskey is the construction guard for ScopedSpan: its constructor is
// private with Tracer befriended, so only a Tracer can mint one — user code
// cannot construct a span guard directly (pinned by negative static_asserts
// in the test TU).
//
// start_span runs the whole D8 hot pipeline INLINE:
//   1. liveness        : one acquire load of the provider's alive flag (dead
//                        handles fail with NotInitialized — spec scenario)
//   2. control         : ONE relaxed load of the control byte (bit0 enabled,
//                        bits1-2 sampling); disabled/Drop → inactive guard,
//                        nothing else executes (~1 ns, spec short-circuit)
//   3. shard slot      : thread_local {owner, slot} map — first call per
//                        (thread, provider) claims a slot (relaxed fetch_add
//                        + release high-water publish, cold); steady state
//                        is TLS only
//   4. thread id       : thread_local cached SYS_gettid
//   5. start stamp     : the SLOT's single-writer Stamp_source (one rdtsc +
//                        fixed-point multiply — a clock_gettime per span
//                        cannot fit the budget)
//   6. rate budget     : shard-owned plain window state; a rejected span
//                        counts rate_limited and short-circuits inactive
//                        BEFORE any queue interaction
//   7. ids + parent    : stack top inheritance, explicit-parent override,
//                        fresh root ids; over-deep (depth == 16) spans record
//                        as bounded roots; a sampled span pushes its own
//                        frame and remembers its index
//   8. record fill     : fixed-offset stores into the returned guard's
//                        inline record (name memcpy ≤ 47, static location
//                        pointers, kind, tid; status Unset, 0 attributes)
// The returned guard is constructed in the caller's storage under C++17
// guaranteed elision — no copy, no move.
#pragma once

#include <cstdint>
#include <format>
#include <source_location>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/tracing/detail/active_span_stack.hpp>
#include <nexigen/tracing/detail/id_generator.hpp>
#include <nexigen/tracing/scoped_span.hpp>
#include <nexigen/tracing/span_context.hpp>
#include <nexigen/tracing/span_enums.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

template <std::size_t ShardCapacity, std::size_t Max_threads>
class TracerProvider; // forward: the factory owns and creates tracers

/// Construction guard: only a Tracer may create spans. The tag carries no
/// state; its constructor is private with every Tracer instantiation
/// befriended (spec "Factory-only construction is pinned at compile time").
class SpanPasskey {
    SpanPasskey() = default;

public:
    template <std::size_t SC, std::size_t MT>
    friend class Tracer;

    /// Cold-path diagnostic (mandate 9). The tag carries no state.
    [[nodiscard]] std::string to_string() const { return "SpanPasskey"; }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const SpanPasskey&) { return "SpanPasskey"; }

/// A named span factory handle. Created by TracerProvider::get_tracer; the
/// handle stays valid until the provider's destruction (dead-handle use
/// fails with NotInitialized through start_span's error surface).
template <std::size_t ShardCapacity, std::size_t Max_threads>
class Tracer {
public:
    Tracer(const Tracer&) = delete;
    Tracer& operator=(const Tracer&) = delete;
    Tracer(Tracer&&) = delete;
    Tracer& operator=(Tracer&&) = delete;

    /// Start a span: the whole D8 pipeline, inline, noexcept. `parent`
    /// overrides the thread's active stack (cross-thread linking); without
    /// it the stack top parents the span automatically.
    [[nodiscard]] ScopedSpan<ShardCapacity, Max_threads>
    start_span(std::string_view name, SpanKind kind, const SpanContext* parent = nullptr,
               std::source_location location = std::source_location::current()) noexcept {
        using Provider = TracerProvider<ShardCapacity, Max_threads>;
        Provider& provider = *provider_;

        // (1) Liveness: a handle used after its provider died reports
        // NotInitialized through the guard's error surface (no memory error:
        // the provider storage is still mapped — documented contract).
        if (!provider.alive_.load(std::memory_order_acquire)) {
            return ScopedSpan<ShardCapacity, Max_threads>{
                SpanPasskey{}, nullptr, core::ErrorCode::NotInitialized};
        }

        // (2) Control short-circuit: ONE relaxed load + shifts; disabled or
        // Drop → inactive guard, nothing else on the path (~1 ns).
        const std::uint8_t control = provider.control_.state.load(std::memory_order_relaxed);
        const SamplingDecision sampling =
            static_cast<SamplingDecision>((control >> 1U) & 0x3U);
        if ((control & 0x1U) == 0U || sampling == SamplingDecision::Drop) {
            return ScopedSpan<ShardCapacity, Max_threads>{
                SpanPasskey{}, nullptr, core::ErrorCode::Ok};
        }

        // (3) Shard slot: TLS steady-state; the claim is cold.
        const std::int8_t slot = provider.this_thread_slot();
        if (slot < 0) {
            // Beyond Max_threads distinct producers: dropped and counted
            // (slot_exhausted), never blocked (spec "Threads beyond the pool
            // drop visibly").
            return ScopedSpan<ShardCapacity, Max_threads>{
                SpanPasskey{}, nullptr, core::ErrorCode::Ok};
        }

        // (4) Cached thread id (syscall once per thread).
        const std::uint32_t tid = detail::cached_thread_id();

        // (5) Start stamp from the slot's single-writer source.
        const chrono::Timestamp start = provider.stamps_[static_cast<std::size_t>(slot)]
                                             .sample();

        // (6) Configured span-rate budget: rejected before any queue
        // interaction; rate_limited counts exactly the rejected spans.
        if (provider.rate_policy_enabled()) {
            auto& state = provider.state_[static_cast<std::size_t>(slot)];
            const std::uint64_t ns = start.nanoseconds();
            if (state.window_start_ns != 0ULL &&
                ns - state.window_start_ns >= provider.rate_window_ns()) {
                const std::uint64_t skipped =
                    (ns - state.window_start_ns) / provider.rate_window_ns();
                state.window_start_ns += skipped * provider.rate_window_ns();
                state.tokens_used = 0;
            } else if (state.window_start_ns == 0ULL) {
                state.window_start_ns = ns; // first span opens window 0
            }
            if (state.tokens_used >= provider.rate_max_spans()) {
                state.rate_limited.store(
                    state.rate_limited.load(std::memory_order_relaxed) + 1U,
                    std::memory_order_relaxed);
                return ScopedSpan<ShardCapacity, Max_threads>{
                    SpanPasskey{}, nullptr, core::ErrorCode::Ok};
            }
            ++state.tokens_used;
        }

        // (7) Ids and parent: explicit parent wins over the stack; an empty
        // stack is a fresh root; depth == max_span_depth records as a
        // BOUNDED ROOT (fresh trace id, zero parent, no push — design D7).
        detail::Active_span_stack& stack = detail::active_span_stack();
        const bool can_push = stack.depth() < detail::max_span_depth;
        TraceId trace_id{};
        SpanId parent_span_id{};
        if (parent != nullptr) {
            trace_id = parent->trace_id;
            parent_span_id = parent->span_id;
        } else if (!can_push) {
            trace_id = TraceId::generate(); // bounded root
        } else if (const detail::Active_frame* top = stack.top(); top != nullptr) {
            trace_id = top->trace_id; // inherit the active trace
            parent_span_id = top->span_id;
        } else {
            trace_id = TraceId::generate(); // fresh root
        }
        const SpanId span_id = SpanId::generate();
        // A sampled span pushes ITS OWN frame (explicit-parent children
        // become the local parent for subsequent same-thread spans).
        const std::uint8_t stack_index = can_push
                                             ? stack.push(detail::Active_frame{trace_id,
                                                                                span_id})
                                             : detail::max_span_depth;

        // (8) Construct the guard as a PRVALUE (C++17 guaranteed elision —
        // constructed directly in the caller's storage, no copy, no move)
        // with the record filled inline by the constructor.
        return ScopedSpan<ShardCapacity, Max_threads>{
            SpanPasskey{}, &provider, slot, stack_index, sampling, trace_id, span_id,
            parent_span_id, kind, start, tid, name, location};
    }

    /// The scope name this tracer was created with.
    [[nodiscard]] std::string_view scope_name() const noexcept {
        return std::string_view(name_ != nullptr ? name_ : "");
    }

    /// Cold-path diagnostic (mandate 9): the scope name.
    [[nodiscard]] std::string to_string() const {
        return "Tracer(scope=" + std::string(scope_name()) + ")";
    }

private:
    friend class TracerProvider<ShardCapacity, Max_threads>;
    friend class ScopedSpan<ShardCapacity, Max_threads>;

    /// Factory-only construction: the provider (the befriended factory)
    /// placement-constructs tracers into its pre-allocated pool. No
    /// SpanPasskey here — the passkey guards SPAN construction by a Tracer.
    Tracer(TracerProvider<ShardCapacity, Max_threads>* provider, const char* name) noexcept
        : provider_(provider), name_(name) {}

    TracerProvider<ShardCapacity, Max_threads>* provider_{};
    const char* name_{}; ///< points into the provider's pool slot storage
};

/// Free helpers (mandate 9): mirror the members.
template <std::size_t SC, std::size_t MT>
[[nodiscard]] std::string to_string(const Tracer<SC, MT>& tracer) {
    return tracer.to_string();
}

/// Diagnostic JSON of the handle (cold path); no from_json by design — the
/// factory hierarchy is the only creator (spec "Live provider objects are
/// not deserializable").
template <std::size_t SC, std::size_t MT>
void to_json(nlohmann::json& j, const Tracer<SC, MT>& tracer) {
    j = nlohmann::json{{"scope", std::string(tracer.scope_name())}};
}

} // namespace nexigen::tracing

/// The passkey and tracer handles format as their diagnostic strings
/// (mandate 11).
template <>
struct std::formatter<nexigen::tracing::SpanPasskey> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::SpanPasskey&, Ctx& ctx) const {
        return std::formatter<std::string_view>::format("SpanPasskey", ctx);
    }
};
template <std::size_t SC, std::size_t MT>
struct std::formatter<nexigen::tracing::Tracer<SC, MT>> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::Tracer<SC, MT>& tracer, Ctx& ctx) const {
        const std::string text = tracer.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
