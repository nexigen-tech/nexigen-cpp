// nexigen::tracing — null trace sink (design D2/D11; spec
// tracing/tracer-concepts "NullTraceSink zero-work sink").
//
// The cheapest conforming sink: it accepts every span and does nothing. No
// output, no allocation, no system call — on_span discards the record
// without reading it. start/flush succeed as no-ops; stop does nothing.
#pragma once

#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// @brief Cheapest conforming sink: accepts every span and discards it.
///
/// `on_span` never reads the record, writes no output, allocates nothing and
/// makes no system call; `start()`/`flush()` succeed unconditionally and
/// `stop()` does nothing. The type is stateless and trivially shareable, so it
/// can serve as a placeholder sink or as the neutral member of a
/// `CompositeTraceSink`. Only `to_string()`, the JSON hook, and `std::format`
/// touch the cold path.
///
/// @note Spec `tracing/tracer-concepts`, requirement "NullTraceSink zero-work
///       sink".
class NullTraceSink {
public:
    /// @brief Complete a start request without doing any work.
    ///
    /// @return Success status, always; the null sink has nothing to open.
    /// @note Spec `tracing/tracer-concepts`, requirement "NullTraceSink
    ///       zero-work sink".
    [[nodiscard]] core::Status start() noexcept { return {}; }

    /// @brief Accept and discard the record without reading it.
    ///
    /// @return Success status, always; nothing is inspected, written, or
    ///         allocated.
    /// @note Spec `tracing/tracer-concepts`, requirement "NullTraceSink
    ///       zero-work sink".
    [[nodiscard]] core::Status on_span(const SpanRecord&) noexcept { return {}; }

    /// @brief Complete a flush request without doing any work.
    ///
    /// @return Success status, always; the null sink holds no buffered bytes.
    /// @note Spec `tracing/tracer-concepts`, requirement "NullTraceSink
    ///       zero-work sink".
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// @brief Complete a stop request without doing any work.
    ///
    /// @note Spec `tracing/tracer-concepts`, requirement "NullTraceSink
    ///       zero-work sink".
    void stop() noexcept {}

    /// @brief Render the fixed diagnostic string for this sink.
    ///
    /// @return The literal `"NullTraceSink()"`; allocates, cold path only.
    /// @note Project mandate 9 (`to_string`).
    [[nodiscard]] std::string to_string() const { return "NullTraceSink()"; }
};

/// @brief Free diagnostic helper mirroring `NullTraceSink::to_string()`.
///
/// The unnamed argument is unused because the sink is stateless.
///
/// @return The literal `"NullTraceSink()"`.
/// @note Project mandate 9 (`to_string`); cold path only.
[[nodiscard]] inline std::string to_string(const NullTraceSink&) {
    return "NullTraceSink()";
}

/// @brief Serialize the null sink as a stateless diagnostic object.
///
/// There is deliberately no `from_json`: null objects are not reconstructed
/// from data (spec "Null types are not deserializable").
///
/// @param j Destination JSON value, overwritten with
///        `{"type":"NullTraceSink"}`.
/// @note Spec `tracing/tracer-concepts`, requirement "Null type serialization
///       surfaces"; project mandate 10 (JSON).
inline void to_json(nlohmann::json& j, const NullTraceSink&) {
    j = nlohmann::json{{"type", "NullTraceSink"}};
}

static_assert(concepts::TraceSink<NullTraceSink>, "NullTraceSink must satisfy the sink contract");

} // namespace nexigen::tracing

/// The null sink formats as its diagnostic string (mandate 11).
