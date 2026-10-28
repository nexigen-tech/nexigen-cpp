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

/// A sink that discards every batch.
class NullMetricSink {
public:
    [[nodiscard]] core::Status on_batch(const MetricBatchSnapshot&) noexcept { return {}; }

    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const { return "NullMetricSink()"; }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const NullMetricSink&) { return "NullMetricSink()"; }

/// Diagnostic JSON (cold path): the sink is stateless, so it round-trips.
inline void to_json(nlohmann::json& j, const NullMetricSink&) {
    j = nlohmann::json{{"type", "NullMetricSink"}};
}

inline void from_json(const nlohmann::json&, NullMetricSink&) noexcept {
    // Stateless: nothing to restore.
}

static_assert(concepts::MetricSink<NullMetricSink>, "NullMetricSink must satisfy the sink contract");

} // namespace nexigen::metrics

/// The null sink formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::NullMetricSink> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::NullMetricSink& sink, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(std::string_view("NullMetricSink()"), ctx);
    }
};
