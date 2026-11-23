#include "logview_config.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nexigen::logview {

nexigen::core::Status LogViewConfig::validate() const noexcept {
    if (service_name.empty() || service_name.size() > 511U) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    if (max_memory_mb < 16U || max_memory_mb > 16384U) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    if (cpu_limit_percent == 0U || cpu_limit_percent > 100U) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    if (storm_rate == 0U) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    const auto capture = capture_thread.validate();
    if (!capture) {
        return capture;
    }
    return export_thread.validate();
}

std::string LogViewConfig::to_string() const {
    std::size_t hidden = hidden_apps.size();
    return std::format(
        "LogViewConfig(service={}, memory_mb={}, cpu_limit={}, storm_rate={}, timestamp={}, "
        "delta={}, hidden_apps={}, colors={})",
        service_name, max_memory_mb, cpu_limit_percent, storm_rate, timestamp_mode, delta_mode,
        hidden, colors);
}

void to_json(nlohmann::json& j, const LogViewConfig& config) {
    nlohmann::json levels = nlohmann::json::array();
    for (const bool visible : config.level_visible) {
        levels.push_back(visible);
    }
    j = nlohmann::json{{"service", config.service_name},
                       {"max_memory_mb", config.max_memory_mb},
                       {"cpu_limit_percent", config.cpu_limit_percent},
                       {"storm_rate", config.storm_rate},
                       {"anomaly_threshold_ns", config.anomaly_threshold_ns},
                       {"timestamp_mode", config.timestamp_mode},
                       {"delta_mode", config.delta_mode},
                       {"level_visible", levels},
                       {"text_filter", config.text_filter},
                       {"hidden_apps", config.hidden_apps},
                       {"colors", config.colors},
                       {"capture_thread", config.capture_thread},
                       {"export_thread", config.export_thread}};
}

namespace {

void read_text_field(const nlohmann::json& j, const char* key, std::string& out) {
    if (const auto it = j.find(key); it != j.end() && it->is_string()) {
        out = it->get<std::string>();
    }
}

void read_u32(const nlohmann::json& j, const char* key, std::uint32_t& out) noexcept {
    if (const auto it = j.find(key); it != j.end() && it->is_number_unsigned()) {
        const auto value = it->get<std::uint64_t>();
        if (value <= 0xFFFFFFFFULL) {
            out = static_cast<std::uint32_t>(value);
        }
    }
}

void read_u64(const nlohmann::json& j, const char* key, std::uint64_t& out) noexcept {
    if (const auto it = j.find(key); it != j.end() && it->is_number_unsigned()) {
        out = it->get<std::uint64_t>();
    }
}

void read_levels(const nlohmann::json& j, std::array<bool, 7>& levels) {
    const auto it = j.find("level_visible");
    if (it == j.end() || !it->is_array()) {
        return;
    }
    const std::size_t n = it->size() < levels.size() ? it->size() : levels.size();
    for (std::size_t i = 0; i < n; ++i) {
        if ((*it)[i].is_boolean()) {
            levels[i] = (*it)[i].get<bool>();
        }
    }
}

void read_hidden_apps(const nlohmann::json& j, std::vector<std::string>& apps) {
    const auto it = j.find("hidden_apps");
    if (it == j.end() || !it->is_array()) {
        return;
    }
    apps.clear();
    for (const auto& item : *it) {
        if (item.is_string()) {
            apps.push_back(item.get<std::string>());
        }
    }
}

void read_nested(const nlohmann::json& j, const char* key, auto& out) {
    if (const auto it = j.find(key); it != j.end()) {
        from_json(*it, out);
    }
}

} // namespace

void from_json(const nlohmann::json& j, LogViewConfig& config) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_text_field(j, "service", config.service_name);
    read_u32(j, "max_memory_mb", config.max_memory_mb);
    read_u32(j, "cpu_limit_percent", config.cpu_limit_percent);
    read_u64(j, "storm_rate", config.storm_rate);
    read_u64(j, "anomaly_threshold_ns", config.anomaly_threshold_ns);
    read_nested(j, "timestamp_mode", config.timestamp_mode);
    read_nested(j, "delta_mode", config.delta_mode);
    read_levels(j, config.level_visible);
    read_text_field(j, "text_filter", config.text_filter);
    read_hidden_apps(j, config.hidden_apps);
    read_nested(j, "colors", config.colors);
    read_nested(j, "capture_thread", config.capture_thread);
    read_nested(j, "export_thread", config.export_thread);
}

std::string default_config_path() {
    if (const char* const xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
        return std::string(xdg) + "/nexigen/logview.json";
    }
    if (const char* const home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::string(home) + "/.config/nexigen/logview.json";
    }
    return "logview.json";
}

nexigen::core::Status load_config(const std::string& path, LogViewConfig& config,
                                  bool& warning) noexcept {
    warning = false;
    std::ifstream file(path);
    if (!file) {
        return {};
    }
    nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        warning = true;
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    from_json(json, config);
    return {};
}

nexigen::core::Status save_config(const std::string& path, const LogViewConfig& config) noexcept {
    const std::size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        std::error_code error;
        std::filesystem::create_directories(path.substr(0, slash), error);
        if (error) {
            return std::unexpected(nexigen::core::ErrorCode::SystemError);
        }
    }
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) {
            return std::unexpected(nexigen::core::ErrorCode::SystemError);
        }
        file << nlohmann::json(config).dump(2) << '\n';
        if (!file) {
            return std::unexpected(nexigen::core::ErrorCode::SystemError);
        }
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        static_cast<void>(::unlink(temporary.c_str()));
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    return {};
}

} // namespace nexigen::logview
