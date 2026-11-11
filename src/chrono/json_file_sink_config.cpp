// nexigen::chrono — cold-path JSON for JsonFileSinkConfig.
//
// Bodies compiled once per module (spec quality/code-structure,
// "Cold-path serialization of compiled modules is built once").
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <string>

#include <nexigen/chrono/json_file_sink_config.hpp>

namespace nexigen::chrono {

void to_json(nlohmann::json& j, const JsonFileSinkConfig& config) {
    j = nlohmann::json{{"path", config.path}, {"append", config.append}};
}

void from_json(const nlohmann::json& j, JsonFileSinkConfig& out) noexcept {
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
    const auto append_it = j.find("append");
    if (append_it != j.end() && append_it->is_boolean()) {
        out.append = append_it->get<bool>();
    }
}

} // namespace nexigen::chrono
