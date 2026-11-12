// nexigen::tracing — the RAII span guard (design D8; spec tracing/scoped-span).
//
// ScopedSpan is a non-copyable, non-movable, non-default-constructible guard
// created EXCLUSIVELY by a Tracer's start_span (SpanPasskey-guarded; returned
// by value under guaranteed elision). The full record lives INLINE in the
// guard (record_): start_span fills it with fixed-offset stores, the owner
// thread mutates attributes/status in place, and end()/destruction pushes
// the 320-byte record into the calling thread's private shard through the
// provider's INLINE finish_span (no indirect call, no lock, no allocation).
//
// HOT PATH (design D8 accounting): end() = branch + one StampSource sample
// + one SPSC try_push + one TLS truncation ≈ well under the 20 ns budget.
// set_attribute/set_status = branch + fixed-offset stores, no atomic.
//
// An inactive guard (owner_ == nullptr) is the disabled/dropped/rejected
// short-circuit result: every operation is a no-op, destruction enqueues
// nothing, and the thread-local stack was never touched.
#pragma once

#include <cstdint>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/tracing/detail/active_span_stack.hpp>
#include <nexigen/tracing/span_context.hpp>
#include <nexigen/tracing/span_enums.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// @brief Construction tag defined in `tracer.hpp`; only a `Tracer` can mint one.
class SpanPasskey; // defined in tracer.hpp; the by-ref parameter below keeps
                   // this header self-contained (no include cycle)

/// @brief The provider that owns this guard's shard ring and stamp source.
///
/// @tparam ShardCapacity Ring capacity of the owning provider.
/// @tparam MaxThreads Shard-slot count of the owning provider.
template <std::size_t ShardCapacity, std::size_t MaxThreads> class TracerProvider;

/// @brief The `SpanPasskey`-holding factory that alone constructs guards.
///
/// @tparam ShardCapacity Ring capacity of the owning provider.
/// @tparam MaxThreads Shard-slot count of the owning provider.
template <std::size_t ShardCapacity, std::size_t MaxThreads> class Tracer;

/// @brief The RAII span guard. Constructed only by `Tracer::start_span`.
///
/// The full 320-byte `SpanRecord` lives inline in the guard, so start, updates
/// and the single end enqueue are fixed-offset stores with no allocation, no
/// lock and no indirect call. Destruction enqueues exactly once when the span
/// is still active; an explicit `end()` enqueues once and makes destruction a
/// no-op. An inactive guard (disabled provider, `Drop` sampling, exhausted
/// slots, rate budget, or a dead provider handle) carries `owner_ == nullptr`,
/// so every mutating member reports rejection without side effects, destruction
/// enqueues nothing, and no thread-local stack was touched.
///
/// @tparam ShardCapacity Capacity of the calling thread's provider shard; must
///         match the `TracerProvider` that created the guard.
/// @tparam MaxThreads Shard-slot count of that provider; must match as well.
/// @warning The guard is single-owner-thread state: mutate and destroy it on
///          the thread that started the span, and never move or copy it.
/// @note Spec `tracing/scoped-span`, requirement "RAII span lifecycle".
template <std::size_t ShardCapacity, std::size_t MaxThreads> class ScopedSpan {
public:
    /// @brief Copy construction is deleted: the guard owns a live shard slot.
    ScopedSpan(const ScopedSpan&) = delete;
    /// @brief Copy assignment is deleted: the guard owns a live shard slot.
    ScopedSpan& operator=(const ScopedSpan&) = delete;
    /// @brief Move construction is deleted: guaranteed elision replaces it.
    ScopedSpan(ScopedSpan&&) = delete;
    /// @brief Move assignment is deleted: guaranteed elision replaces it.
    ScopedSpan& operator=(ScopedSpan&&) = delete;

    /// @brief Destruction enqueues exactly one record when the span is active and
    ///        not yet ended (spec "Destruction enqueues exactly one record").
    ///
    /// The push is the provider's inline `finish_span`: end stamp, one SPSC
    /// `try_push`, TLS stack truncation — no lock, no allocation, no indirect
    /// call. A full shard drops the record and counts `buffer_full` instead of
    /// blocking the destroying thread.
    ~ScopedSpan() {
        if (owner_ != nullptr && !ended_) {
            ended_ = true;
            static_cast<void>(owner_->finish_span(record_, slot_, stack_index_));
        }
    }

    // -- wait-free updates (spec tracing/scoped-span "Wait-free span updates")

    /// @brief Store an int attribute (plain literals); false = rejected (ended,
    ///        capacity full, oversized key) with no side effects.
    ///
    /// @param key Attribute key; rejected whole when longer than the inline bound.
    /// @param value Value to store, widened to the `Int64` arm.
    /// @return `true` when stored; `false` when rejected with nothing mutated.
    [[nodiscard]] bool set_attribute(std::string_view key, int value) noexcept {
        return set_attribute(key, static_cast<std::int64_t>(value));
    }
    /// @brief Store an int64 attribute; same rejection contract.
    ///
    /// @param key Attribute key; rejected when inactive, ended, out of slots or
    ///        longer than the inline key bound.
    /// @param value Value to store.
    /// @return `true` when stored; `false` when rejected with nothing mutated.
    [[nodiscard]] bool set_attribute(std::string_view key, std::int64_t value) noexcept {
        return store_attribute(key, value);
    }
    /// @brief Store a uint64 attribute; same rejection contract.
    ///
    /// @param key Attribute key; rejected when inactive, ended, out of slots or
    ///        longer than the inline key bound.
    /// @param value Value to store.
    /// @return `true` when stored; `false` when rejected with nothing mutated.
    [[nodiscard]] bool set_attribute(std::string_view key, std::uint64_t value) noexcept {
        return store_attribute(key, value);
    }
    /// @brief Store a double attribute; same rejection contract.
    ///
    /// @param key Attribute key; rejected when inactive, ended, out of slots or
    ///        longer than the inline key bound.
    /// @param value Value to store bit-for-bit.
    /// @return `true` when stored; `false` when rejected with nothing mutated.
    [[nodiscard]] bool set_attribute(std::string_view key, double value) noexcept {
        return store_attribute(key, value);
    }
    /// @brief Store a bool attribute; same rejection contract.
    ///
    /// @param key Attribute key; rejected when inactive, ended, out of slots or
    ///        longer than the inline key bound.
    /// @param value Value to store.
    /// @return `true` when stored; `false` when rejected with nothing mutated.
    [[nodiscard]] bool set_attribute(std::string_view key, bool value) noexcept {
        return store_attribute(key, value);
    }
    /// @brief Store a string attribute.
    ///
    /// Values ≤ 23 bytes copy inline; longer values become a ZERO-COPY view
    /// whose storage must outlive the span's drain (literals/program-static
    /// tables qualify — the `SpanAttribute` contract). The KEY has the same
    /// rejection contract as the numeric overloads.
    ///
    /// @param key Attribute key; rejected whole when longer than the inline bound.
    /// @param value Value text; ≤ 23 bytes are copied, longer text is viewed.
    /// @return `true` when stored; `false` when rejected with nothing mutated.
    /// @warning A long `value` must reference string-literal or program-static
    ///          storage; a temporary buffer dangles by drain time.
    [[nodiscard]] bool set_attribute(std::string_view key, std::string_view value) noexcept {
        return store_attribute(key, value);
    }

    /// @brief Set the outcome; false = rejected (ended/inactive), no side effects.
    ///
    /// @param status Outcome to record (`Ok` or `Error`; `Unset` restores the
    ///        start default).
    /// @return `true` when stored; `false` when the span is inactive or ended.
    /// @note Spec `tracing/scoped-span`, requirement "Wait-free span updates".
    [[nodiscard]] bool set_status(SpanStatus status) noexcept {
        if (owner_ == nullptr || ended_) {
            return false;
        }
        record_.status = status;
        return true;
    }

    /// @brief End the span explicitly: enqueues the record once.
    ///
    /// Destruction then performs no second push, and subsequent calls are
    /// no-ops returning `false`. Sets `SpanRecord::flag_ended_explicit`.
    ///
    /// @return `true` when this call ended the span; `false` when it was
    ///         already ended or never active.
    /// @note Spec `tracing/scoped-span`, requirement "RAII span lifecycle".
    /// @note Spec `tracing/scoped-span`, requirement "Sub-20-nanosecond enqueue":
    ///       one wait-free `try_push`, never blocking on a full shard.
    [[nodiscard]] bool end() noexcept {
        if (owner_ == nullptr || ended_) {
            return false;
        }
        ended_ = true;
        record_.flags |= SpanRecord::flag_ended_explicit;
        return owner_->finish_span(record_, slot_, stack_index_);
    }

    /// @brief The handoff snapshot: trace id, span id, and the sampling decision
    ///        made at start (spec "Context extraction").
    ///
    /// Inactive guards report the zero/unset context. The returned value is
    /// trivially copyable and can be pushed through a bounded queue to link a
    /// child span on another thread.
    ///
    /// @return Copy of the guard's trace/span identity and decision.
    /// @note Spec `tracing/scoped-span`, requirement "Context extraction".
    [[nodiscard]] SpanContext context() const noexcept {
        return SpanContext{record_.trace_id, record_.span_id, sampled_};
    }

    /// @brief True while the guard records (an active, not-yet-ended span).
    ///
    /// @return `true` when `owner_` is set and `end()` has not run.
    [[nodiscard]] bool active() const noexcept { return owner_ != nullptr && !ended_; }

    /// @brief The failure classification of the START call.
    ///
    /// `Ok` for both active and decision-inactive guards (`Drop` sampling,
    /// disabled provider, exhausted slots, rate budget); `NotInitialized` when
    /// the tracer's provider was already destroyed.
    ///
    /// @return Start-result error code; never a per-update outcome.
    /// @note Spec `tracing/tracer-provider`, requirement "Shutdown drain and
    ///       flush barrier", scenario "Dead handles report NotInitialized".
    [[nodiscard]] core::ErrorCode error() const noexcept { return error_; }

    /// @brief The inline record (tests/diagnostics; the owner-mutation surface).
    ///
    /// @return Const reference to the guard-owned record; valid until the
    ///         guard is destroyed.
    [[nodiscard]] const SpanRecord& record() const noexcept { return record_; }

    /// @brief Cold-path diagnostic (mandate 9; spec "Guard renders its state"):
    ///        name, kind, and current status.
    ///
    /// @return Diagnostic text; the free `to_string` and the `std::formatter`
    ///         render the same string.
    /// @note Allocates a `std::string`; diagnostics/config/tests only, never a
    ///       hot-path call.
    [[nodiscard]] std::string to_string() const {
        std::string out = "ScopedSpan(name=" + std::string(record_.name_view());
        out += ", kind=" + std::string(tracing::to_string(record_.kind));
        out += ", status=" + std::string(tracing::to_string(record_.status));
        out += active() ? ", state=active" : ", state=inactive";
        out += ")";
        return out;
    }

private:
    // Only a Tracer (which alone can mint a SpanPasskey) constructs guards;
    // the passkey parameter is by reference so this header needs only a
    // forward declaration (tracer.hpp includes this header, not vice versa).
    friend class Tracer<ShardCapacity, MaxThreads>;

    /// @brief Inactive guard: the disabled/dropped/rejected/dead short-circuit.
    ///
    /// The passkey and owner arguments are ignored — the guard keeps
    /// `owner_ == nullptr`, enqueues nothing on destruction and never touches
    /// the thread-local stack.
    ///
    /// @param start_error Classification surfaced by `error()`; `Ok` for a
    ///        decision-based short-circuit, `NotInitialized` for a dead handle.
    ScopedSpan(const SpanPasskey&, TracerProvider<ShardCapacity, MaxThreads>*,
               core::ErrorCode start_error) noexcept
        : owner_(nullptr), error_(start_error) {}

    /// @brief Active guard: the D8 pipeline's results.
    ///
    /// The record is zeroed and filled INLINE in this constructor so
    /// `start_span` can return it as a prvalue (C++17 guaranteed elision — no
    /// copy, no move, one fill).
    ///
    /// @param owner Provider that owns the shard ring and stamp source.
    /// @param slot Claimed shard slot of the calling thread.
    /// @param stack_index Frame index to truncate the TLS stack back to.
    /// @param sampled Decision recorded at start and reported by `context()`.
    /// @param trace_id Causal group (inherited, explicit, or freshly generated).
    /// @param span_id This span's identifier.
    /// @param parent_span_id Parent identifier; zero marks a root span.
    /// @param kind Causal role carried into the record.
    /// @param start Monotonic start stamp sampled by the pipeline.
    /// @param thread_id Cached kernel thread id of the producer.
    /// @param name Span name; bounded copy, longer names truncated at 47.
    /// @param location Call-site location copied as static pointers and line.
    /// @note Spec `tracing/scoped-span`, requirement "RAII span lifecycle".
    ScopedSpan(const SpanPasskey&, TracerProvider<ShardCapacity, MaxThreads>* owner,
               std::int8_t slot, std::uint8_t stack_index, SamplingDecision sampled,
               const TraceId& trace_id, const SpanId& span_id, const SpanId& parent_span_id,
               SpanKind kind, const chrono::Timestamp& start, std::uint32_t thread_id,
               std::string_view name, const std::source_location& location) noexcept
        : owner_(owner), slot_(slot), stack_index_(stack_index), ended_(false), sampled_(sampled),
          error_(core::ErrorCode::Ok) {
        record_.trace_id = trace_id;
        record_.span_id = span_id;
        record_.parent_span_id = parent_span_id;
        record_.kind = kind;
        record_.status = SpanStatus::Unset;
        record_.flags = 0U;
        record_.attribute_count = 0U;
        record_.thread_id = thread_id;
        record_.start = start;
        record_.file_name = location.file_name();
        record_.function_name = location.function_name();
        record_.line = location.line();
        record_.column = location.column();
        const std::size_t name_take =
            name.size() <= SpanRecord::name_capacity ? name.size() : SpanRecord::name_capacity;
        for (std::size_t i = 0; i < name_take; ++i) {
            record_.name[i] = name[i];
        }
        record_.name[name_take] = '\0';
    }

    /// @brief The mutation surface for the public `set_attribute` overloads.
    ///
    /// Rejects without side effects when the guard is inactive or ended, when
    /// all `MaxAttributes` slots are taken, or when the key does not fit; a
    /// rejected store never leaves a partial attribute behind.
    ///
    /// @tparam V Value type forwarded to the matching `SpanAttribute::store`
    ///         overload (`std::int64_t`, `std::uint64_t`, `double`, `bool`, or
    ///         `std::string_view`).
    /// @param key Attribute key.
    /// @param value Value forwarded unchanged.
    /// @return `true` when appended; `false` when rejected with nothing mutated.
    /// @note Spec `tracing/scoped-span`, requirement "Wait-free span updates":
    ///       branch plus fixed-offset stores, no lock, no allocation.
    template <class V>
    [[nodiscard]] bool store_attribute(std::string_view key, V&& value) noexcept {
        if (owner_ == nullptr || ended_) {
            return false; // post-end / inactive: rejected, nothing mutated
        }
        if (record_.attribute_count >= MaxAttributes) {
            return false; // capacity bound: rejected without side effects
        }
        SpanAttribute& attr = record_.attributes[record_.attribute_count];
        if (!attr.store(key, std::forward<V>(value))) {
            return false; // oversized key: rejected whole
        }
        ++record_.attribute_count;
        return true;
    }

    SpanRecord record_{};                                ///< the inline payload
    TracerProvider<ShardCapacity, MaxThreads>* owner_{}; ///< null = inactive
    std::int8_t slot_{};                                 ///< shard index
    std::uint8_t stack_index_{};                         ///< frame index / sentinel
    bool ended_{};                                       ///< exactly-once guard
    SamplingDecision sampled_{SamplingDecision::Drop};   ///< decision at start
    core::ErrorCode error_{core::ErrorCode::Ok};         ///< start failure class
};

/// @brief Free helper (mandate 9): mirrors `ScopedSpan::to_string`.
///
/// @tparam SC Shard capacity of the guard's provider.
/// @tparam MT Shard-slot count of the guard's provider.
/// @param span Guard to render.
/// @return The same text as the member.
template <std::size_t SC, std::size_t MT>
[[nodiscard]] std::string to_string(const ScopedSpan<SC, MT>& span) {
    return span.to_string();
}

/// @brief Diagnostic JSON of the LIVE state (cold path).
///
/// Writes name, kind, status and the current `active()` flag; there is no
/// `from_json` by design — guards are created only by a `Tracer`.
///
/// @tparam SC Shard capacity of the guard's provider.
/// @tparam MT Shard-slot count of the guard's provider.
/// @param j Destination document or field to overwrite.
/// @param span Guard to serialize.
/// @note Spec `tracing/scoped-span`, requirement "ScopedSpan serialization
///       surface": factory-bound types are deliberately not deserializable.
template <std::size_t SC, std::size_t MT>
void to_json(nlohmann::json& j, const ScopedSpan<SC, MT>& span) {
    j = nlohmann::json{
        {"name", std::string(span.record().name_view())},
        {"kind", span.record().kind},
        {"status", span.record().status},
        {"active", span.active()},
    };
}

} // namespace nexigen::tracing

/// @brief `std::formatter` for `ScopedSpan` (mandate 11).
///
/// Renders exactly `ScopedSpan::to_string()` through the `std::string_view`
/// formatter, so fill, alignment and width options still apply.
///
/// @tparam SC Shard capacity of the guard's provider.
/// @tparam MT Shard-slot count of the guard's provider.
template <std::size_t SC, std::size_t MT>
struct std::formatter<nexigen::tracing::ScopedSpan<SC, MT>> : std::formatter<std::string_view> {
    /// @brief Render one guard as its diagnostic text.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param span Guard to render.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::ScopedSpan<SC, MT>& span, Ctx& ctx) const {
        const std::string text = span.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
