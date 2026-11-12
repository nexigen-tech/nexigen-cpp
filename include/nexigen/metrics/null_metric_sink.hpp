// nexigen::metrics — null sink (design D13; spec metrics/metric-sinks
// "Null metric sink").
//
// The cheapest conforming sink: it accepts every batch and does nothing. No
// output, no allocation, no system call.
#pragma once

#include <string>

#include <nexigen/core/result.hpp>
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>

namespace nexigen::metrics {

/// @brief Cheapest conforming sink: accepts every batch and discards it.
///
/// `on_batch` never reads the snapshot, writes no output, allocates nothing and
/// makes no system call; `flush` succeeds unconditionally. The type is
/// stateless and trivially shareable, so it can be used as a placeholder sink or
/// as the neutral member of a `CompositeMetricSink`. Only `to_string`, the JSON
/// hooks and `std::format` touch the cold path.
///
/// @note Spec `metrics/metric-sinks`, requirement "Null metric sink".
class NullMetricSink {
public:
    /// @brief Accept one harvested batch and discard it.
    ///
    /// The parameter is intentionally unnamed and never read: nothing is
    /// inspected, written or allocated.
    ///
    /// @return Success status, always.
    /// @note Spec `metrics/metric-sinks`, requirement "Metric sink concept";
    ///       the callback stays allocation-free and makes no system call.
    [[nodiscard]] core::Status on_batch(const MetricBatchSnapshot&) noexcept { return {}; }

    /// @brief Complete a flush request without doing any work.
    ///
    /// @return Success status, always; the null sink holds no buffered bytes.
    /// @note Spec `metrics/metric-sinks`, requirement "Null metric sink".
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// @brief Render the fixed diagnostic string for this sink.
    ///
    /// @return The literal `"NullMetricSink()"`; allocates, cold path only.
    /// @note Spec `metrics/metric-sinks`, scenario "Null sink diagnostics
    ///       contract"; project mandate 9 (`to_string`).
    [[nodiscard]] std::string to_string() const { return "NullMetricSink()"; }
};

/// @brief Free `to_string` helper mirroring the member function.
///
/// The unnamed argument is unused because the sink is stateless.
///
/// @return The same string as `NullMetricSink::to_string`.
/// @note Spec `metrics/metric-sinks`, scenario "Null sink diagnostics
///       contract"; project mandate 9.
[[nodiscard]] inline std::string to_string(const NullMetricSink&) {
    return "NullMetricSink()";
}

/// @brief Serialize the null sink as a stateless diagnostic object.
///
/// @param j Destination JSON value, overwritten with `{"type":"NullMetricSink"}`.
/// @note Spec `metrics/metric-sinks`, scenario "Null sink diagnostics
///       contract"; project mandate 10 (JSON).
inline void to_json(nlohmann::json& j, const NullMetricSink&) {
    j = nlohmann::json{{"type", "NullMetricSink"}};
}

/// @brief Deserialize the null sink; every field is ignored.
///
/// The destination is left untouched because the type carries no state.
///
/// @note Spec `metrics/metric-sinks`, scenario "Null sink diagnostics
///       contract"; must stay non-throwing under `JSON_NOEXCEPTION=1`.
inline void from_json(const nlohmann::json&, NullMetricSink&) noexcept {
    // Stateless: nothing to restore.
}

static_assert(concepts::MetricSink<NullMetricSink>,
              "NullMetricSink must satisfy the sink contract");

} // namespace nexigen::metrics

/// @brief `std::formatter` for the null sink; renders its diagnostic string.
///
/// @note Spec `metrics/metric-sinks`, scenario "Null sink diagnostics
///       contract"; project mandate 11 (`std::formatter`).
template <>
struct std::formatter<nexigen::metrics::NullMetricSink> : std::formatter<std::string_view> {
    /// @brief Write the sink's diagnostic text through the string formatter.
    ///
    /// @tparam Ctx Formatting context type supplied by `std::format`.
    /// @param sink Sink being formatted; unused because the text is constant.
    /// @param ctx Destination formatting context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format([[maybe_unused]] const nexigen::metrics::NullMetricSink& sink, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(std::string_view("NullMetricSink()"), ctx);
    }
};
