// nexigen::metrics — cold-path JSON for JsonFileMetricSink and its config.
//
// Bodies compiled once per module (spec quality/code-structure,
// "Cold-path serialization of compiled modules is built once").
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <string>

#include <nexigen/metrics/json_file_metric_sink.hpp>

namespace nexigen::metrics {

void to_json(nlohmann::json& j, const JsonFileMetricSinkConfig& config) {
    j = nlohmann::json{
        {"path", config.path}, {"app_name", config.app_name}, {"append", config.append}};
}

void from_json(const nlohmann::json& j, JsonFileMetricSinkConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto it = j.find("path");
    if (it != j.end() && it->is_string()) {
        const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
        if (text != nullptr) {
            out.path = std::string(*text);
        }
    }
    const auto app_it = j.find("app_name");
    if (app_it != j.end() && app_it->is_string()) {
        const auto* const text = app_it->get_ptr<const nlohmann::json::string_t*>();
        if (text != nullptr) {
            out.app_name = std::string(*text);
        }
    }
    const auto append_it = j.find("append");
    if (append_it != j.end() && append_it->is_boolean()) {
        out.append = append_it->get<bool>();
    }
}

/// The sink itself is not deserializable: rebinding it opens a file, which is
/// a construction-time action, not a deserialization.
void to_json(nlohmann::json& j, const JsonFileMetricSink& sink) {
    j = nlohmann::json{{"type", "JsonFileMetricSink"}, {"open", sink.is_open()}};
}

} // namespace nexigen::metrics
