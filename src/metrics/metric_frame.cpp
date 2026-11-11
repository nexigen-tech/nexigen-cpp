// nexigen::metrics — cold-path JSON for MetricFrame (spec metrics/metric-frame).
//
// Bodies compiled once per module (spec quality/code-structure,
// "Cold-path serialization of compiled modules is built once").
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <cstddef>
#include <cstdint>
#include <string>

#include <nexigen/metrics/metric_frame.hpp>

namespace nexigen::metrics {

void to_json(nlohmann::json& j, const MetricFrame& frame) {
    j = nlohmann::json{
        {"app_id", std::string(frame.app_id_text())},
        {"pid", frame.pid},
        {"timestamp_ns", frame.timestamp_ns},
        {"record", frame.record},
    };
}

void from_json(const nlohmann::json& j, MetricFrame& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    MetricFrame parsed{};
    const auto app_it = j.find("app_id");
    if (app_it != j.end() && app_it->is_string()) {
        const std::string app = app_it->get<std::string>();
        const std::size_t take =
            app.size() < sizeof(parsed.app_id) ? app.size() : sizeof(parsed.app_id);
        for (std::size_t i = 0; i < take; ++i) {
            parsed.app_id[i] = app[i];
        }
    }
    const auto pid_it = j.find("pid");
    if (pid_it != j.end() && pid_it->is_number_unsigned()) {
        parsed.pid = pid_it->get<std::uint32_t>();
    }
    const auto ts_it = j.find("timestamp_ns");
    if (ts_it != j.end() && ts_it->is_number_integer()) {
        parsed.timestamp_ns = ts_it->get<std::int64_t>();
    }
    const auto rec_it = j.find("record");
    if (rec_it != j.end()) {
        from_json(*rec_it, parsed.record);
    }
    out = parsed;
}

} // namespace nexigen::metrics
