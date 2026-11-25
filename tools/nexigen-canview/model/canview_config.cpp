#include "canview_config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/hw/can/can_channel_name.hpp>

#include "cli.hpp"

namespace nexigen::canview {

std::size_t CanViewConfig::enabled_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& channel : channels) {
        if (channel.enabled) {
            ++count;
        }
    }
    return count;
}

nexigen::core::Status CanViewConfig::validate() const {
    if (channels.size() > max_profiles || enabled_count() > max_enabled) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    for (std::size_t index = 0U; index < channels.size(); ++index) {
        const CanViewChannel& channel = channels[index];
        if (!nexigen::hw::can::is_valid_channel_name(channel.name)) {
            return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
        }
        for (std::size_t other = 0U; other < index; ++other) {
            if (channels[other].name == channel.name ||
                (channels[other].enabled && channel.enabled &&
                 channels[other].channel_id == channel.channel_id)) {
                return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
            }
        }
    }
    return {};
}

std::string default_config_path() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::string(xdg) + "/nexigen/canview.json";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::string(home) + "/.config/nexigen/canview.json";
    }
    return "nexigen-canview.json";
}

CanViewConfig load_config(const std::string& path, bool* corrupted) {
    CanViewConfig config{};
    if (corrupted != nullptr) {
        *corrupted = false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return config;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!nlohmann::json::accept(text)) {
        if (corrupted != nullptr) {
            *corrupted = true;
        }
        return config;
    }
    const nlohmann::json document = nlohmann::json::parse(text);
    if (!document.is_object()) {
        if (corrupted != nullptr) {
            *corrupted = true;
        }
        return config;
    }
    from_json(document, config);
    return config;
}

nexigen::core::Status save_config(const std::string& path, const CanViewConfig& config) {
    const std::filesystem::path target(path);
    std::error_code error;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), error);
    }
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return std::unexpected(nexigen::core::ErrorCode::SystemError);
        }
        file << nlohmann::json(config).dump() << '\n';
        if (!file.good()) {
            return std::unexpected(nexigen::core::ErrorCode::SystemError);
        }
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        static_cast<void>(std::remove(temporary.c_str()));
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    return {};
}

void apply_cli_overrides(CanViewConfig& config, const CliOptions& options) noexcept {
    if (!options.channels.empty()) {
        config.channels = options.channels;
    }
    for (const auto& binding : options.eds) {
        config.eds.push_back(binding);
    }
    if (options.views_given) {
        config.view_count = options.view_count;
    }
    if (options.memory_given) {
        config.memory_mb = options.memory_mb;
    }
    if (options.shared_given) {
        config.shared_timebase = options.shared_timebase;
    }
    if (options.columns_given) {
        config.defaults.time_columns = options.time_columns;
    }
    if (options.timestamp_given) {
        config.defaults.timestamp_mode = options.timestamp_mode;
    }
    if (options.delta_given) {
        config.defaults.delta_mode = options.delta_mode;
    }
    config.filter.errors_only = options.errors_only;
    config.filter.show_bus_idle = options.show_bus_idle;
    if (!options.filter_text.empty()) {
        config.filter.set_text(options.filter_text);
    }
    if (options.filter_id_given) {
        config.filter.id = options.filter_id;
        config.filter.id_mask = options.filter_mask;
        config.filter.id_filter_active = true;
    }
}

void to_json(nlohmann::json& j, const CanViewConfig& config) {
    nlohmann::json channels = nlohmann::json::array();
    for (const auto& channel : config.channels) {
        channels.push_back(channel);
    }
    nlohmann::json eds = nlohmann::json::array();
    for (const auto& binding : config.eds) {
        eds.push_back(nlohmann::json{{"channel_id", binding.channel_id},
                                     {"node_id", binding.node_id},
                                     {"alias", binding.alias},
                                     {"path", binding.path},
                                     {"hash", binding.hash},
                                     {"colour", binding.colour}});
    }
    j = nlohmann::json{{"version", config.version},
                       {"channels", std::move(channels)},
                       {"eds", std::move(eds)},
                       {"memory_mb", config.memory_mb},
                       {"view_count", config.view_count},
                       {"shared_timebase", config.shared_timebase},
                       {"defaults",
                        {{"table_mode", config.defaults.table_mode},
                         {"timestamp_mode", config.defaults.timestamp_mode},
                         {"delta_mode", config.defaults.delta_mode},
                         {"time_columns", config.defaults.time_columns},
                         {"auto_scroll", config.defaults.auto_scroll},
                         {"anomaly_threshold_ns", config.defaults.anomaly_threshold_ns}}},
                       {"filter", config.filter},
                       {"views", config.views}};
}

void from_json(const nlohmann::json& j, CanViewConfig& config) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "version", config.version));
    static_cast<void>(core::detail::read_unsigned(j, "memory_mb", config.memory_mb));
    static_cast<void>(core::detail::read_unsigned(j, "view_count", config.view_count));
    static_cast<void>(core::detail::read_bool(j, "shared_timebase", config.shared_timebase));
    if (const auto it = j.find("channels"); it != j.end() && it->is_array()) {
        config.channels.clear();
        for (const auto& entry : *it) {
            CanViewChannel channel{};
            from_json(entry, channel);
            config.channels.push_back(std::move(channel));
        }
    }
    if (const auto it = j.find("eds"); it != j.end() && it->is_array()) {
        config.eds.clear();
        for (const auto& entry : *it) {
            SessionEdsBinding binding{};
            static_cast<void>(core::detail::read_unsigned(entry, "channel_id", binding.channel_id));
            static_cast<void>(core::detail::read_unsigned(entry, "node_id", binding.node_id));
            static_cast<void>(core::detail::read_string(entry, "alias", binding.alias));
            static_cast<void>(core::detail::read_string(entry, "path", binding.path));
            static_cast<void>(core::detail::read_unsigned(entry, "hash", binding.hash));
            static_cast<void>(core::detail::read_unsigned(entry, "colour", binding.colour));
            config.eds.push_back(std::move(binding));
        }
    }
    if (const auto it = j.find("defaults"); it != j.end() && it->is_object()) {
        core::mandate::read_enum(*it, "table_mode", config.defaults.table_mode);
        core::mandate::read_enum(*it, "timestamp_mode", config.defaults.timestamp_mode);
        core::mandate::read_enum(*it, "delta_mode", config.defaults.delta_mode);
        core::mandate::read_enum(*it, "time_columns", config.defaults.time_columns);
        static_cast<void>(core::detail::read_bool(*it, "auto_scroll", config.defaults.auto_scroll));
        static_cast<void>(core::detail::read_signed(*it, "anomaly_threshold_ns",
                                                    config.defaults.anomaly_threshold_ns));
    }
    if (const auto it = j.find("filter"); it != j.end()) {
        from_json(*it, config.filter);
    }
    if (const auto it = j.find("views"); it != j.end()) {
        config.views = *it;
    }
}

} // namespace nexigen::canview
