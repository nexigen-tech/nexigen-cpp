// nexigen::tracing — structural concepts and the null tracer (design D2;
// spec tracing/tracer-concepts).
//
// concepts::Tracer and concepts::TraceSink are C++23 structural concepts:
// conforming types satisfy them at compile time (static_assert), defective
// probes fail them at compile time — no base class, no virtual dispatch.
//
// The guard half of the Tracer contract is itself a concept (SpanGuard):
// start_span must return an RAII guard providing wait-free
// set_attribute/set_status, idempotent end(), context() and a cold
// to_string(). ScopedSpan<...> (the concrete guard) and NullTracer's null
// guard both satisfy it.
//
// NullTracer is the zero-instruction disabled path: every operation is an
// empty inline body, the guard is trivially destructible with no state, so
// a compilation unit using only NullTracer performs no identifier
// generation, no timestamp read, no thread-local work and no memory write
// when tracing is disabled (spec "NullTracer zero-instruction path").
#pragma once

#include <concepts>
#include <cstdint>
#include <format>
#include <source_location>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/tracing/span_context.hpp>
#include <nexigen/tracing/span_enums.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing::concepts {

/// @brief The RAII guard surface every tracer's `start_span` must return.
///
/// The requirements are exactly the span hot path plus one cold diagnostic:
/// `set_attribute` accepts the six supported value types and reports
/// acceptance with `bool` (`true` = applied); `end()` reports whether this
/// call ended the span (`true` = it did); `context()` hands off the
/// trace/span identity for cross-thread linking; `to_string()` is the
/// cold-path diagnostic. No base class or virtual function is involved — a
/// type conforms structurally or fails at compile time.
///
/// @tparam G Candidate guard type. The concept constrains it directly: it must
///         provide all six `set_attribute` overloads returning `bool`,
///         `set_status`/`end` returning `bool`, `context()` returning
///         `SpanContext`, and `to_string()` returning `std::string`.
/// @note Spec `tracing/tracer-concepts`, requirement "Tracer structural concept".
template <class G>
concept SpanGuard = requires(G& guard) {
    /// @brief Integer attribute store; `true` when applied.
    { guard.set_attribute(std::string_view{}, int{}) } -> std::same_as<bool>;
    /// @brief 64-bit signed attribute store; `true` when applied.
    { guard.set_attribute(std::string_view{}, std::int64_t{}) } -> std::same_as<bool>;
    /// @brief 64-bit unsigned attribute store; `true` when applied.
    { guard.set_attribute(std::string_view{}, std::uint64_t{}) } -> std::same_as<bool>;
    /// @brief Double attribute store; `true` when applied.
    { guard.set_attribute(std::string_view{}, double{}) } -> std::same_as<bool>;
    /// @brief Boolean attribute store; `true` when applied.
    { guard.set_attribute(std::string_view{}, bool{}) } -> std::same_as<bool>;
    /// @brief String attribute store; `true` when applied.
    { guard.set_attribute(std::string_view{}, std::string_view{}) } -> std::same_as<bool>;
    /// @brief Outcome store; `true` when applied.
    { guard.set_status(SpanStatus::Ok) } -> std::same_as<bool>;
    /// @brief Idempotent end; `true` when this call ended the span.
    { guard.end() } -> std::same_as<bool>;
    /// @brief Handoff snapshot of the span identity and decision.
    { guard.context() } -> std::same_as<SpanContext>;
    /// @brief Cold-path diagnostic text.
    { guard.to_string() } -> std::same_as<std::string>;
};

/// @brief A tracer: `start_span` returning a conforming guard, plus a cold
///        diagnostic `to_string()`.
///
/// Both call shapes must compile: the full form
/// `start_span(name, kind, parent, location)` and the defaulted form
/// `start_span(name, kind)`. There is no virtual dispatch and no base class.
///
/// @tparam T Candidate tracer type, constrained to return a type satisfying
///         `SpanGuard` from both `start_span` shapes and to provide
///         `to_string()` returning `std::string`.
/// @note Spec `tracing/tracer-concepts`, requirement "Tracer structural concept".
template <class T>
concept Tracer = requires(T& tracer, std::string_view name, SpanKind kind,
                          const SpanContext* parent, std::source_location location) {
    /// @brief Full span start: name, kind, optional parent context, location.
    { tracer.start_span(name, kind, parent, location) } -> SpanGuard;
    /// @brief Defaulted span start: name and kind only.
    { tracer.start_span(name, kind) } -> SpanGuard;
    /// @brief Cold-path diagnostic text.
    { tracer.to_string() } -> std::same_as<std::string>;
};

/// @brief A trace sink: lifecycle start/stop, per-record delivery, flush, and a
///        cold-path diagnostic.
///
/// Every member is `noexcept` — a throwing `on_span` fails the concept at
/// compile time, so the worker can drain without exception handling. `start`,
/// `on_span` and `flush` report through `core::Status`; `stop` is a plain
/// `noexcept` call. Conforming sinks are pinned by `static_assert` in their own
/// headers.
///
/// @tparam S Candidate sink type, constrained to `noexcept`
///         `start`/`on_span`/`flush` returning `core::Status`, a `noexcept`
///         `stop`, and `to_string()` returning `std::string`.
/// @note Spec `tracing/tracer-concepts`, requirement "TraceSink structural
///       concept".
template <class S>
concept TraceSink = requires(S& sink, const SpanRecord& record) {
    /// @brief Begin delivery; non-throwing, reports through `core::Status`.
    { sink.start() } noexcept -> std::same_as<core::Status>;
    /// @brief Deliver one drained record; must never throw across the worker.
    { sink.on_span(record) } noexcept -> std::same_as<core::Status>;
    /// @brief Make accepted records durable; surfaces sink errors.
    { sink.flush() } noexcept -> std::same_as<core::Status>;
    /// @brief Stop delivery; plain non-throwing call with no status.
    { sink.stop() } noexcept;
    /// @brief Cold-path diagnostic text.
    { sink.to_string() } -> std::same_as<std::string>;
};

} // namespace nexigen::tracing::concepts

namespace nexigen::tracing {

/// @brief The zero-cost tracer: satisfies `concepts::Tracer` with every operation
///        an empty inline body.
///
/// The returned guard is trivially destructible and state free — under `-O1+`
/// with the result discarded nothing survives into the emitted code. Use it as
/// the disabled/compile-away tracer, or as the reference implementation of the
/// concepts.
///
/// @note Spec `tracing/tracer-concepts`, requirement "NullTracer zero-instruction
///       path".
class NullTracer {
public:
    /// @brief The null guard: all operations are no-ops reporting rejection.
    ///
    /// The type has no state and a trivial destructor, so a scope containing a
    /// null span performs no identifier generation, timestamp read, TLS access
    /// or memory write (spec "NullTracer zero-instruction path").
    ///
    /// @note Spec `tracing/tracer-concepts`, requirement "NullTracer
    ///       zero-instruction path".
    class NullSpan {
    public:
        /// @brief Rejected int attribute store; all arguments are ignored.
        ///
        /// @return Always `false`.
        [[nodiscard]] bool set_attribute(std::string_view, int) const noexcept { return false; }
        /// @brief Rejected int64 attribute store; all arguments are ignored.
        ///
        /// @return Always `false`.
        [[nodiscard]] bool set_attribute(std::string_view, std::int64_t) const noexcept {
            return false;
        }
        /// @brief Rejected uint64 attribute store; all arguments are ignored.
        ///
        /// @return Always `false`.
        [[nodiscard]] bool set_attribute(std::string_view, std::uint64_t) const noexcept {
            return false;
        }
        /// @brief Rejected double attribute store; all arguments are ignored.
        ///
        /// @return Always `false`.
        [[nodiscard]] bool set_attribute(std::string_view, double) const noexcept { return false; }
        /// @brief Rejected bool attribute store; all arguments are ignored.
        ///
        /// @return Always `false`.
        [[nodiscard]] bool set_attribute(std::string_view, bool) const noexcept { return false; }
        /// @brief Rejected string attribute store; all arguments are ignored.
        ///
        /// @return Always `false`.
        [[nodiscard]] bool set_attribute(std::string_view, std::string_view) const noexcept {
            return false;
        }
        /// @brief Rejected outcome store; the status argument is ignored.
        ///
        /// @return Always `false`.
        [[nodiscard]] bool set_status(SpanStatus) const noexcept { return false; }
        /// @brief No-op end; always `false` (this guard never ends a span).
        ///
        /// @return Always `false`.
        [[nodiscard]] bool end() const noexcept { return false; }
        /// @brief The zero/unset context of a span that was never recorded.
        ///
        /// @return Default-constructed `SpanContext` (unset ids, `Drop`).
        [[nodiscard]] SpanContext context() const noexcept { return {}; }
        /// @brief Cold-path diagnostic (mandate 9).
        ///
        /// @return The literal `"NullTracer::NullSpan"`.
        [[nodiscard]] std::string to_string() const { return "NullTracer::NullSpan"; }
    };

    /// @brief Returns the state-free null guard.
    ///
    /// All four arguments (name, kind, parent pointer, source location) are
    /// discarded without side effects: no ids, no timestamps, no TLS work. The
    /// returned guard performs nothing on destruction, so a disabled span costs
    /// the same as an empty statement.
    ///
    /// @return A default-constructed `NullSpan`.
    /// @note Spec `tracing/tracer-concepts`, requirement "NullTracer zero-instruction
    ///       path".
    [[nodiscard]] NullSpan
    start_span(std::string_view, SpanKind, const SpanContext* = nullptr,
               std::source_location = std::source_location::current()) const noexcept {
        return {};
    }

    /// @brief Cold-path diagnostic (mandate 9).
    ///
    /// @return The literal `"NullTracer()"`.
    [[nodiscard]] std::string to_string() const { return "NullTracer()"; }
};

/// @brief Free helper (mandate 9): mirrors the member.
///
/// The unnamed argument is ignored; the null tracer carries no state.
///
/// @return The literal `"NullTracer()"`.
[[nodiscard]] inline std::string to_string(const NullTracer&) {
    return "NullTracer()";
}
/// @brief Free helper (mandate 9): mirrors the guard's member.
///
/// @param span Guard to render.
/// @return The same text as `NullSpan::to_string()`.
[[nodiscard]] inline std::string to_string(const NullTracer::NullSpan& span) {
    return span.to_string();
}

/// @brief Diagnostic JSON (cold path); no `from_json` by design.
///
/// Null objects are not reconstructed from data — attempting `from_json` into
/// either null type fails at compile time.
///
/// @param j Destination document or field to overwrite.
/// @note Spec `tracing/tracer-concepts`, requirement "Null type serialization
///       surfaces", scenario "Null types are not deserializable".
inline void to_json(nlohmann::json& j, const NullTracer&) {
    j = nlohmann::json{{"type", "NullTracer"}};
}
/// @brief Diagnostic JSON of the null guard (cold path); no `from_json` by design.
///
/// @param j Destination document or field to overwrite.
/// @note Spec `tracing/tracer-concepts`, requirement "Null type serialization
///       surfaces".
inline void to_json(nlohmann::json& j, const NullTracer::NullSpan&) {
    j = nlohmann::json{{"type", "NullTracer::NullSpan"}};
}

static_assert(concepts::Tracer<NullTracer>, "NullTracer must satisfy the Tracer contract");
static_assert(std::is_trivially_destructible_v<NullTracer::NullSpan>,
              "the null guard must be trivially destructible (zero-instruction path)");

} // namespace nexigen::tracing

/// @brief `std::formatter` for `NullTracer` (mandate 11).
///
/// The null tracer and its guard format as their diagnostic strings, matching
/// their `to_string()` members.
template <> struct std::formatter<nexigen::tracing::NullTracer> : std::formatter<std::string_view> {
    /// @brief Render the null tracer as its diagnostic text.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::tracing::NullTracer&, Ctx& ctx) const {
        return std::formatter<std::string_view>::format("NullTracer()", ctx);
    }
};
