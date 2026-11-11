// nexigen::metrics — cold-path JSON for ConsoleMetricSink.
//
// Bodies compiled once per module (spec quality/code-structure,
// "Cold-path serialization of compiled modules is built once").
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <cstdint>

#include <nexigen/metrics/console_metric_sink.hpp>

namespace nexigen::metrics {

void to_json(nlohmann::json& j, const ConsoleMetricSink& sink) {
    j = nlohmann::json{{"type", "ConsoleMetricSink"}, {"fd", sink.descriptor()}};
}

void from_json(const nlohmann::json& j, ConsoleMetricSink& sink) noexcept {
    const auto it = j.find("fd");
    if (it != j.end() && it->is_number_integer()) {
        const auto value = it->get<std::int64_t>();
        sink = ConsoleMetricSink{static_cast<int>(value)};
    }
}

} // namespace nexigen::metrics
