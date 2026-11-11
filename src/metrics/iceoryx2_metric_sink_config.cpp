// nexigen::metrics — cold-path JSON for Iceoryx2MetricSinkConfig.
//
// Bodies compiled once per module (spec quality/code-structure,
// "Cold-path serialization of compiled modules is built once").
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <cstdint>
#include <string>
#include <utility>

#include <nexigen/metrics/iceoryx2_metric_sink_config.hpp>

namespace nexigen::metrics {

void to_json(nlohmann::json& j, const Iceoryx2MetricSinkConfig& config) {
    j = nlohmann::json{
        {"app_id", config.app_id},
        {"service_name", config.service_name},
        {"subscriber_max_buffer_size", config.subscriber_max_buffer_size},
    };
}

void from_json(const nlohmann::json& j, Iceoryx2MetricSinkConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    Iceoryx2MetricSinkConfig parsed{};
    const auto app_it = j.find("app_id");
    if (app_it != j.end() && app_it->is_string()) {
        parsed.app_id = app_it->get<std::string>();
    }
    const auto svc_it = j.find("service_name");
    if (svc_it != j.end() && svc_it->is_string()) {
        parsed.service_name = svc_it->get<std::string>();
    }
    const auto buf_it = j.find("subscriber_max_buffer_size");
    if (buf_it != j.end() && buf_it->is_number_unsigned()) {
        parsed.subscriber_max_buffer_size = buf_it->get<std::uint64_t>();
    }
    out = std::move(parsed);
}

} // namespace nexigen::metrics
