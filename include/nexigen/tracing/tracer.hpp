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
//   5. start stamp     : the SLOT's single-writer StampSource (one rdtsc +
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
#include <nexigen/tracing/detail/trace_context.hpp>
#include <nexigen/tracing/scoped_span.hpp>
#include <nexigen/tracing/span_context.hpp>
#include <nexigen/tracing/span_enums.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// @brief The provider that alone constructs and owns tracer handles.
///
/// @tparam ShardCapacity Ring capacity of the owned shards.
/// @tparam MaxThreads Shard-slot count of the pool.
template <std::size_t ShardCapacity, std::size_t MaxThreads>
class TracerProvider; // forward: the factory owns and creates tracers

/// @brief Construction guard: only a Tracer may create spans.
///
/// The tag carries no state; its constructor is private with every `Tracer`
/// instantiation befriended, so `ScopedSpan` cannot be built by user code and
/// the factory-only rule is enforced at compile time rather than by
/// convention.
///
/// @note Spec `tracing/tracer-provider`, requirement "Provider and tracer
///       factory hierarchy", scenario "Factory-only construction is pinned at
///       compile time".
class SpanPasskey {
    /// @brief Private default constructor: only a befriended `Tracer` can mint one.
    SpanPasskey() = default;

public:
    /// @brief The only types allowed to mint a passkey.
    ///
    /// @tparam SC Shard capacity of the tracer's provider.
    /// @tparam MT Shard-slot count of the tracer's provider.
    template <std::size_t SC, std::size_t MT> friend class Tracer;

    /// @brief Cold-path diagnostic (mandate 9). The tag carries no state.
    ///
    /// @return The literal `"SpanPasskey"`.
    [[nodiscard]] std::string to_string() const { return "SpanPasskey"; }
};

/// @brief Free helper (mandate 9): mirrors the member.
///
/// The argument is unnamed because the passkey carries no state.
///
/// @return The literal `"SpanPasskey"`.
[[nodiscard]] inline std::string to_string(const SpanPasskey&) {
    return "SpanPasskey";
}

/// @brief A named span factory handle.
///
/// Created by `TracerProvider::get_tracer`, placement-constructed into the
/// provider's pre-allocated pool, and valid until the provider is destroyed;
/// a dead handle fails with `NotInitialized` through `start_span`'s guard error
/// surface rather than corrupting memory. Handles are neither copyable nor
/// movable, so the pool owns exactly one object per slot. `start_span` runs the
/// entire hot pipeline inline — no allocation, no lock, no virtual call, no
/// cross-thread atomic on the accepted path.
///
/// @tparam ShardCapacity Ring capacity of the owning provider; must match its
///         `TracerProvider` instantiation.
/// @tparam MaxThreads Shard-slot count of the owning provider; must match too.
/// @note Spec `tracing/tracer-provider`, requirement "Provider and tracer
///       factory hierarchy".
template <std::size_t ShardCapacity, std::size_t MaxThreads> class Tracer {
public:
    /// @brief Copy construction is deleted: handles are pool-owned identities.
    Tracer(const Tracer&) = delete;
    /// @brief Copy assignment is deleted: handles are pool-owned identities.
    Tracer& operator=(const Tracer&) = delete;
    /// @brief Move construction is deleted: the pool slot address is identity.
    Tracer(Tracer&&) = delete;
    /// @brief Move assignment is deleted: the pool slot address is identity.
    Tracer& operator=(Tracer&&) = delete;

    /// @brief Start a span: the whole D8 pipeline, inline, `noexcept`.
    ///
    /// Order: liveness acquire load, ONE relaxed control load (disabled or
    /// `Drop` returns an inactive guard after ~1 ns), TLS shard-slot claim,
    /// cached thread id, the slot's own stamp, the optional rate budget, ids
    /// and parent selection, then the inline record fill. A sampled span pushes
    /// its own frame onto the calling thread's active-span stack and pushes one
    /// record into that thread's private shard; a rejected span (dead handle,
    /// disabled, `Drop`, exhausted slots, rate budget) returns an inactive
    /// guard that enqueues nothing.
    ///
    /// `parent` overrides the thread's active stack (cross-thread linking);
    /// without it the stack top parents the span automatically, an empty stack
    /// falls back to the propagated `TraceContext`, and depth `16` records the
    /// span as a bounded root.
    ///
    /// @param name Span name; stored inline, truncated at 47 characters.
    /// @param kind Causal role recorded on the span.
    /// @param parent Optional explicit parent context; `nullptr` uses the
    ///        calling thread's active stack.
    /// @param location Call-site location (deduced by default); file/function
    ///        pointers stay in program-static storage.
    /// @return The RAII guard, constructed directly in the caller's storage.
    /// @note Spec `tracing/tracer-provider`, requirement "Dynamic runtime
    ///       control": one relaxed control load, disabled or `Drop`
    ///       short-circuits at roughly 1 ns.
    /// @note Spec `tracing/scoped-span`, requirement "Cross-thread context
    ///       linking": an explicit parent wins over the active stack.
    /// @note Spec `tracing/scoped-span`, requirement "Disabled and dropped
    ///       short-circuit": no ids, stamps or TLS stack work on that path.
    /// @note Spec `tracing/tracer-provider`, requirement "Per-thread shard
    ///       ownership": threads beyond `MaxThreads` are dropped and counted in
    ///       `slot_exhausted`, never blocked.
    /// @note Spec `tracing/tracer-provider`, requirement "Drop visibility":
    ///       accepted-path losses surface through counters, not exceptions.
    [[nodiscard]] ScopedSpan<ShardCapacity, MaxThreads>
    start_span(std::string_view name, SpanKind kind, const SpanContext* parent = nullptr,
               std::source_location location = std::source_location::current()) noexcept {
        using Provider = TracerProvider<ShardCapacity, MaxThreads>;
        Provider& provider = *provider_;

        // (1) Liveness: a handle used after its provider died reports
        // NotInitialized through the guard's error surface (no memory error:
        // the provider storage is still mapped — documented contract).
        if (!provider.alive_.load(std::memory_order_acquire)) {
            return ScopedSpan<ShardCapacity, MaxThreads>{SpanPasskey{}, nullptr,
                                                         core::ErrorCode::NotInitialized};
        }

        // (2) Control short-circuit: ONE relaxed load + shifts; disabled or
        // Drop → inactive guard, nothing else on the path (~1 ns).
        const std::uint8_t control = provider.control_.state.load(std::memory_order_relaxed);
        const SamplingDecision sampling = static_cast<SamplingDecision>((control >> 1U) & 0x3U);
        if ((control & 0x1U) == 0U || sampling == SamplingDecision::Drop) {
            return ScopedSpan<ShardCapacity, MaxThreads>{SpanPasskey{}, nullptr,
                                                         core::ErrorCode::Ok};
        }

        // (3) Shard slot: TLS steady-state; the claim is cold.
        const std::int8_t slot = provider.this_thread_slot();
        if (slot < 0) {
            // Beyond MaxThreads distinct producers: dropped and counted
            // (slot_exhausted), never blocked (spec "Threads beyond the pool
            // drop visibly").
            return ScopedSpan<ShardCapacity, MaxThreads>{SpanPasskey{}, nullptr,
                                                         core::ErrorCode::Ok};
        }

        // (4) Cached thread id (syscall once per thread).
        const std::uint32_t tid = detail::cached_thread_id();

        // (5) Start stamp from the slot's single-writer source.
        const chrono::Timestamp start = provider.stamps_[static_cast<std::size_t>(slot)].sample();

        // (6) Configured span-rate budget: rejected before any queue
        // interaction; rate_limited counts exactly the rejected spans.
        if (provider.rate_policy_enabled()) {
            auto& state = provider.state(static_cast<std::size_t>(slot));
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
                state.rate_limited.store(state.rate_limited.load(std::memory_order_relaxed) + 1U,
                                         std::memory_order_relaxed);
                return ScopedSpan<ShardCapacity, MaxThreads>{SpanPasskey{}, nullptr,
                                                             core::ErrorCode::Ok};
            }
            ++state.tokens_used;
        }

        // (7) Ids and parent: explicit parent wins over the stack; an empty
        // stack is a fresh root; depth == max_span_depth records as a
        // BOUNDED ROOT (fresh trace id, zero parent, no push — design D7).
        detail::ActiveSpanStack& stack = detail::active_span_stack();
        const bool can_push = stack.depth() < detail::max_span_depth;
        TraceId trace_id{};
        SpanId parent_span_id{};
        if (parent != nullptr) {
            trace_id = parent->trace_id;
            parent_span_id = parent->span_id;
        } else if (!can_push) {
            trace_id = TraceId::generate(); // bounded root
        } else if (const detail::ActiveFrame* top = stack.top(); top != nullptr) {
            trace_id = top->trace_id; // inherit the active trace
            parent_span_id = top->span_id;
        } else if (const TraceContext& context = current_trace_context();
                   !context.trace_id.is_unset()) {
            // Cross-process fallback (change add-trace-propagation): a trace
            // received from another process via TraceHeader links the first
            // local span. The context is NOT consumed — subsequent same-thread
            // spans keep the same trace.
            trace_id = context.trace_id;
            parent_span_id = context.source_span_id;
        } else {
            trace_id = TraceId::generate(); // fresh root
        }
        const SpanId span_id = SpanId::generate();
        // A sampled span pushes ITS OWN frame (explicit-parent children
        // become the local parent for subsequent same-thread spans).
        const std::uint8_t stack_index =
            can_push ? stack.push(detail::ActiveFrame{trace_id, span_id}) : detail::max_span_depth;

        // (8) Construct the guard as a PRVALUE (C++17 guaranteed elision —
        // constructed directly in the caller's storage, no copy, no move)
        // with the record filled inline by the constructor.
        return ScopedSpan<ShardCapacity, MaxThreads>{
            SpanPasskey{},  &provider, slot,  stack_index, sampling, trace_id, span_id,
            parent_span_id, kind,      start, tid,         name,     location};
    }

    /// @brief The scope name this tracer was created with.
    ///
    /// @return View over the provider-pool name storage; empty when the handle
    ///         carries no name. Valid while the provider is alive.
    [[nodiscard]] std::string_view scope_name() const noexcept {
        return std::string_view(name_ != nullptr ? name_ : "");
    }

    /// @brief Cold-path diagnostic (mandate 9): the scope name.
    ///
    /// @return Text of the form `Tracer(scope=<name>)`; the free `to_string`
    ///         and `std::formatter` render the same string.
    /// @note Allocates; diagnostics/config/tests only, never the span path.
    [[nodiscard]] std::string to_string() const {
        return "Tracer(scope=" + std::string(scope_name()) + ")";
    }

private:
    // The provider owns this handle's pool slot; the guard consumes its finish path.
    friend class TracerProvider<ShardCapacity, MaxThreads>;
    friend class ScopedSpan<ShardCapacity, MaxThreads>;

    /// @brief Factory-only construction.
    ///
    /// The provider (the befriended factory) placement-constructs tracers into
    /// its pre-allocated pool. No `SpanPasskey` here — the passkey guards SPAN
    /// construction by a Tracer.
    ///
    /// @param provider Provider that owns the pool slot and the shards.
    /// @param name Scope name stored in the provider's pool slot; must outlive
    ///        the handle.
    Tracer(TracerProvider<ShardCapacity, MaxThreads>* provider, const char* name) noexcept
        : provider_(provider), name_(name) {}

    TracerProvider<ShardCapacity, MaxThreads>* provider_{}; ///< owning provider
    const char* name_{}; ///< points into the provider's pool slot storage
};

/// @brief Free helper (mandate 9): mirrors `Tracer::to_string`.
///
/// @tparam SC Shard capacity of the handle's provider.
/// @tparam MT Shard-slot count of the handle's provider.
/// @param tracer Handle to render.
/// @return The same text as the member.
template <std::size_t SC, std::size_t MT>
[[nodiscard]] std::string to_string(const Tracer<SC, MT>& tracer) {
    return tracer.to_string();
}

/// @brief Diagnostic JSON of the handle (cold path); no `from_json` by design.
///
/// Writes `{"scope": <name>}`; the factory hierarchy is the only creator, so a
/// handle is never reconstructed from data.
///
/// @tparam SC Shard capacity of the handle's provider.
/// @tparam MT Shard-slot count of the handle's provider.
/// @param j Destination document or field to overwrite.
/// @param tracer Handle to serialize.
/// @note Spec `tracing/tracer-provider`, requirement "Provider serialization
///       surfaces", scenario "Live provider objects are not deserializable".
template <std::size_t SC, std::size_t MT>
void to_json(nlohmann::json& j, const Tracer<SC, MT>& tracer) {
    j = nlohmann::json{{"scope", std::string(tracer.scope_name())}};
}

} // namespace nexigen::tracing

/// @brief `std::formatter` for `SpanPasskey` (mandate 11).
///
/// The passkey formats as its diagnostic string, matching `to_string()`.
template <>
struct std::formatter<nexigen::tracing::SpanPasskey> : std::formatter<std::string_view> {
    /// @brief Render the passkey as its diagnostic text.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::tracing::SpanPasskey&, Ctx& ctx) const {
        return std::formatter<std::string_view>::format("SpanPasskey", ctx);
    }
};
/// @brief `std::formatter` for `Tracer` (mandate 11).
///
/// Renders exactly `Tracer::to_string()` through the `std::string_view`
/// formatter, so fill, alignment and width options still apply.
///
/// @tparam SC Shard capacity of the handle's provider.
/// @tparam MT Shard-slot count of the handle's provider.
template <std::size_t SC, std::size_t MT>
struct std::formatter<nexigen::tracing::Tracer<SC, MT>> : std::formatter<std::string_view> {
    /// @brief Render one handle as its diagnostic text.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param tracer Handle to render.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::Tracer<SC, MT>& tracer, Ctx& ctx) const {
        const std::string text = tracer.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
