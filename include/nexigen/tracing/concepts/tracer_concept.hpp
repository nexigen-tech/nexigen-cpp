// nexigen::tracing — structural concepts and the null tracer (design D2;
// spec tracing/tracer-concepts).
//
// concepts::Tracer and concepts::TraceSink are C++23 structural concepts:
// conforming types satisfy them at compile time (static_assert), defective
// probes fail them at compile time — no base class, no virtual dispatch.
//
// The guard half of the Tracer contract is itself a concept (Span_guard):
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

/// The RAII guard surface every tracer's start_span must return.
/// Wait-free updates return bool (true = applied); end() returns bool
/// (true = this call ended the span); context() is the handoff snapshot.
template <class G>
concept Span_guard = requires(G& guard) {
    { guard.set_attribute(std::string_view{}, int{}) } -> std::same_as<bool>;
    { guard.set_attribute(std::string_view{}, std::int64_t{}) } -> std::same_as<bool>;
    { guard.set_attribute(std::string_view{}, std::uint64_t{}) } -> std::same_as<bool>;
    { guard.set_attribute(std::string_view{}, double{}) } -> std::same_as<bool>;
    { guard.set_attribute(std::string_view{}, bool{}) } -> std::same_as<bool>;
    { guard.set_attribute(std::string_view{}, std::string_view{}) } -> std::same_as<bool>;
    { guard.set_status(SpanStatus::Ok) } -> std::same_as<bool>;
    { guard.end() } -> std::same_as<bool>;
    { guard.context() } -> std::same_as<SpanContext>;
    { guard.to_string() } -> std::same_as<std::string>;
};

/// A tracer: start_span(name, kind, optional parent context, source
/// location) returning a conforming guard (both the full and the defaulted
/// call shapes must compile), plus a cold-path diagnostic to_string().
template <class T>
concept Tracer = requires(T& tracer, std::string_view name, SpanKind kind,
                          const SpanContext* parent,
                          std::source_location location) {
    { tracer.start_span(name, kind, parent, location) } -> Span_guard;
    { tracer.start_span(name, kind) } -> Span_guard;
    { tracer.to_string() } -> std::same_as<std::string>;
};

/// A trace sink: lifecycle start/stop, per-record delivery, flush, and a
/// cold-path diagnostic. Every member is noexcept — a throwing on_span
/// fails the concept at compile time (spec "Non-conforming sinks are
/// rejected").
template <class S>
concept TraceSink = requires(S& sink, const SpanRecord& record) {
    { sink.start() } noexcept -> std::same_as<core::Status>;
    { sink.on_span(record) } noexcept -> std::same_as<core::Status>;
    { sink.flush() } noexcept -> std::same_as<core::Status>;
    { sink.stop() } noexcept;
    { sink.to_string() } -> std::same_as<std::string>;
};

} // namespace nexigen::tracing::concepts

namespace nexigen::tracing {

/// The zero-cost tracer: satisfies concepts::Tracer with every operation an
/// empty inline body. The returned guard is trivially destructible state
/// free — under -O1+ with the result discarded, nothing survives into the
/// emitted code.
class NullTracer {
public:
    /// The null guard: all operations are no-ops reporting rejection; the
    /// type has no state and a trivial destructor (spec "zero-instruction").
    class Null_span {
    public:
        [[nodiscard]] bool set_attribute(std::string_view, int) const noexcept {
            return false;
        }
        [[nodiscard]] bool set_attribute(std::string_view, std::int64_t) const noexcept {
            return false;
        }
        [[nodiscard]] bool set_attribute(std::string_view, std::uint64_t) const noexcept {
            return false;
        }
        [[nodiscard]] bool set_attribute(std::string_view, double) const noexcept {
            return false;
        }
        [[nodiscard]] bool set_attribute(std::string_view, bool) const noexcept {
            return false;
        }
        [[nodiscard]] bool set_attribute(std::string_view, std::string_view)
            const noexcept {
            return false;
        }
        [[nodiscard]] bool set_status(SpanStatus) const noexcept { return false; }
        [[nodiscard]] bool end() const noexcept { return false; }
        [[nodiscard]] SpanContext context() const noexcept { return {}; }
        /// Cold-path diagnostic (mandate 9).
        [[nodiscard]] std::string to_string() const { return "NullTracer::Null_span"; }
    };

    /// Returns the state-free null guard; the arguments are discarded
    /// without evaluation side effects (no ids, no stamps, no TLS).
    [[nodiscard]] Null_span start_span(std::string_view, SpanKind,
                                       const SpanContext* = nullptr,
                                       std::source_location = std::source_location::current())
        const noexcept {
        return {};
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const { return "NullTracer()"; }
};

/// Free helpers (mandate 9): mirror the members.
[[nodiscard]] inline std::string to_string(const NullTracer&) { return "NullTracer()"; }
[[nodiscard]] inline std::string to_string(const NullTracer::Null_span& span) {
    return span.to_string();
}

/// Diagnostic JSON (cold path); no from_json by design — null objects are
/// not reconstructed from data (spec "Null types are not deserializable").
inline void to_json(nlohmann::json& j, const NullTracer&) {
    j = nlohmann::json{{"type", "NullTracer"}};
}
inline void to_json(nlohmann::json& j, const NullTracer::Null_span&) {
    j = nlohmann::json{{"type", "NullTracer::Null_span"}};
}

static_assert(concepts::Tracer<NullTracer>,
              "NullTracer must satisfy the Tracer contract");
static_assert(std::is_trivially_destructible_v<NullTracer::Null_span>,
              "the null guard must be trivially destructible (zero-instruction path)");

} // namespace nexigen::tracing

/// The null tracer and its guard format as their diagnostic strings
/// (mandate 11).
template <>
struct std::formatter<nexigen::tracing::NullTracer> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::NullTracer&, Ctx& ctx) const {
        return std::formatter<std::string_view>::format("NullTracer()", ctx);
    }
};
template <>
struct std::formatter<nexigen::tracing::NullTracer::Null_span>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::NullTracer::Null_span& span, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(span.to_string(), ctx);
    }
};
