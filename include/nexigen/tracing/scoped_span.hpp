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
// HOT PATH (design D8 accounting): end() = branch + one Stamp_source sample
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

class SpanPasskey; // defined in tracer.hpp; the by-ref parameter below keeps
                   // this header self-contained (no include cycle)
template <std::size_t ShardCapacity, std::size_t Max_threads>
class TracerProvider;
template <std::size_t ShardCapacity, std::size_t Max_threads>
class Tracer;

/// The RAII span guard. Constructed only by Tracer::start_span.
template <std::size_t ShardCapacity, std::size_t Max_threads>
class ScopedSpan {
public:
    ScopedSpan(const ScopedSpan&) = delete;
    ScopedSpan& operator=(const ScopedSpan&) = delete;
    ScopedSpan(ScopedSpan&&) = delete;
    ScopedSpan& operator=(ScopedSpan&&) = delete;

    /// Destruction enqueues exactly one record when the span is active and
    /// not yet ended (spec "Destruction enqueues exactly one record").
    ~ScopedSpan() {
        if (owner_ != nullptr && !ended_) {
            ended_ = true;
            static_cast<void>(owner_->finish_span(record_, slot_, stack_index_));
        }
    }

    // -- wait-free updates (spec tracing/scoped-span "Wait-free span updates")

    /// Store an int attribute (plain literals); false = rejected (ended,
    /// capacity full, oversized key) with no side effects.
    [[nodiscard]] bool set_attribute(std::string_view key, int value) noexcept {
        return set_attribute(key, static_cast<std::int64_t>(value));
    }
    /// Store an int64 attribute; same rejection contract.
    [[nodiscard]] bool set_attribute(std::string_view key, std::int64_t value) noexcept {
        return store_attribute(key, value);
    }
    /// Store a uint64 attribute; same rejection contract.
    [[nodiscard]] bool set_attribute(std::string_view key, std::uint64_t value) noexcept {
        return store_attribute(key, value);
    }
    /// Store a double attribute; same rejection contract.
    [[nodiscard]] bool set_attribute(std::string_view key, double value) noexcept {
        return store_attribute(key, value);
    }
    /// Store a bool attribute; same rejection contract.
    [[nodiscard]] bool set_attribute(std::string_view key, bool value) noexcept {
        return store_attribute(key, value);
    }
    /// Store a string attribute. Values ≤ 23 bytes copy inline; longer
    /// values become a ZERO-COPY view whose storage must outlive the span's
    /// drain (literals/program-static tables qualify — the SpanAttribute
    /// contract). Same rejection contract for the KEY.
    [[nodiscard]] bool set_attribute(std::string_view key, std::string_view value) noexcept {
        return store_attribute(key, value);
    }

    /// Set the outcome; false = rejected (ended/inactive), no side effects.
    [[nodiscard]] bool set_status(SpanStatus status) noexcept {
        if (owner_ == nullptr || ended_) {
            return false;
        }
        record_.status = status;
        return true;
    }

    /// End the span explicitly: enqueues the record once (destruction then
    /// performs no second push); subsequent calls are no-ops returning false.
    [[nodiscard]] bool end() noexcept {
        if (owner_ == nullptr || ended_) {
            return false;
        }
        ended_ = true;
        record_.flags |= SpanRecord::flag_ended_explicit;
        return owner_->finish_span(record_, slot_, stack_index_);
    }

    /// The handoff snapshot: trace id, span id, and the sampling decision
    /// made at start (spec "Context extraction"). Inactive guards report
    /// the zero/ unset context.
    [[nodiscard]] SpanContext context() const noexcept {
        return SpanContext{record_.trace_id, record_.span_id, sampled_};
    }

    /// True while the guard records (an active, not-yet-ended span).
    [[nodiscard]] bool active() const noexcept { return owner_ != nullptr && !ended_; }

    /// The failure classification of the START call: Ok for active and
    /// decision-inactive guards; NotInitialized when the tracer's provider
    /// was already destroyed (spec "Dead handles report NotInitialized").
    [[nodiscard]] core::ErrorCode error() const noexcept { return error_; }

    /// The inline record (tests/diagnostics; the owner-mutation surface).
    [[nodiscard]] const SpanRecord& record() const noexcept { return record_; }

    /// Cold-path diagnostic (mandate 9; spec "Guard renders its state"):
    /// name, kind, and current status.
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
    friend class Tracer<ShardCapacity, Max_threads>;

    /// Inactive guard: the disabled/dropped/rejected/dead short-circuit.
    ScopedSpan(const SpanPasskey&, TracerProvider<ShardCapacity, Max_threads>*,
               core::ErrorCode start_error) noexcept
        : owner_(nullptr), error_(start_error) {}

    /// Active guard: the D8 pipeline's results. The record is zeroed and
    /// filled INLINE in this constructor so start_span can return it as a
    /// prvalue (C++17 guaranteed elision — no copy, no move, one fill).
    ScopedSpan(const SpanPasskey&, TracerProvider<ShardCapacity, Max_threads>* owner,
               std::int8_t slot, std::uint8_t stack_index, SamplingDecision sampled,
               const TraceId& trace_id, const SpanId& span_id, const SpanId& parent_span_id,
               SpanKind kind, const chrono::Timestamp& start, std::uint32_t thread_id,
               std::string_view name, const std::source_location& location) noexcept
        : owner_(owner), slot_(slot), stack_index_(stack_index), ended_(false),
          sampled_(sampled), error_(core::ErrorCode::Ok) {
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

    /// The mutation surface for the public set_attribute overloads.
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

    SpanRecord record_{};                              ///< the inline payload
    TracerProvider<ShardCapacity, Max_threads>* owner_{}; ///< null = inactive
    std::int8_t slot_{};                               ///< shard index
    std::uint8_t stack_index_{};                       ///< frame index / sentinel
    bool ended_{};                                     ///< exactly-once guard
    SamplingDecision sampled_{SamplingDecision::Drop}; ///< decision at start
    core::ErrorCode error_{core::ErrorCode::Ok};       ///< start failure class
};

/// Free helpers (mandate 9): mirror the members.
template <std::size_t SC, std::size_t MT>
[[nodiscard]] std::string to_string(const ScopedSpan<SC, MT>& span) {
    return span.to_string();
}

/// Diagnostic JSON of the LIVE state (cold path); no from_json by design —
/// guards are created only by a Tracer (spec "Guard is not deserializable").
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

/// Guards format as their diagnostic text (mandate 11).
template <std::size_t SC, std::size_t MT>
struct std::formatter<nexigen::tracing::ScopedSpan<SC, MT>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::ScopedSpan<SC, MT>& span, Ctx& ctx) const {
        const std::string text = span.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
