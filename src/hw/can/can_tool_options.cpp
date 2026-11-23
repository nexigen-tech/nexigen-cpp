#include <nexigen/hw/can/can_tool_options.hpp>

#include <nexigen/hw/can/can_channel_set.hpp>

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::hw::can {
namespace {

bool read_file(const std::string& path, nlohmann::json& out) {
    std::ifstream input{path};
    if (!input) {
        return false;
    }
    out = nlohmann::json::parse(input, nullptr, false);
    return !out.is_discarded();
}

core::Status add_channel(CanToolOptions& options, std::string_view spec) {
    const std::size_t equals = spec.find('=');
    if (equals == std::string_view::npos || equals == 0U || equals + 1U >= spec.size()) {
        std::fprintf(stderr, "can: --channel expects name=driver_channel\n");
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    CanChannelConfig config{};
    config.name = std::string(spec.substr(0, equals));
    config.channel_id = static_cast<std::uint16_t>(options.channels.size() + 1U);
    config.driver_channel = std::string(spec.substr(equals + 1U));
    config.bit_rate = 500'000U;
    if (core::Status valid = config.validate(); !valid) {
        std::fprintf(stderr, "can: invalid channel '%s'\n", config.name.c_str());
        return valid;
    }
    options.channels.push_back(std::move(config));
    return {};
}

core::Status load_config(CanToolOptions& options, const std::string& path) {
    nlohmann::json json{};
    if (!read_file(path, json) || !json.is_object()) {
        std::fprintf(stderr, "can: cannot read config '%s'\n", path.c_str());
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const auto it = json.find("channels");
    if (it == json.end() || !it->is_array()) {
        std::fprintf(stderr, "can: config has no channels array\n");
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    for (const auto& entry : *it) {
        CanChannelConfig config{};
        from_json(entry, config);
        if (core::Status valid = config.validate(); !valid) {
            std::fprintf(stderr, "can: invalid channel in config\n");
            return valid;
        }
        options.channels.push_back(std::move(config));
    }
    return {};
}

} // namespace

std::string_view can_tool_usage() noexcept {
    return "Usage: <program> --app-id <id> --channel <name>=<driver> [--channel ...]\n"
           "       [--config <file>] [--backend hardware|virtual (adapter required)]\n"
           "       [--embedded] [--help]\n";
}

namespace {

core::Status apply_monitor_option(std::string_view token, const char* value,
                                  CanToolOptions& options) {
    if (token == "--filter-channel") {
        options.filter_channel = value;
        return {};
    }
    if (token == "--filter-kind") {
        options.filter_kind = value;
        return {};
    }
    if (token == "--filter-origin") {
        options.filter_origin = value;
        return {};
    }
    if (token == "--filter-id") {
        options.filter_id = static_cast<std::uint32_t>(std::strtoul(value, nullptr, 0));
        options.has_filter_id = true;
        return {};
    }
    return std::unexpected(core::ErrorCode::InvalidArgument);
}

core::Status apply_virtual_option(std::string_view token, const char* value,
                                  CanToolOptions& options) {
    if (token == "--seed") {
        options.seed = std::strtoull(value, nullptr, 0);
        options.has_seed = true;
        return {};
    }
    if (token == "--duration-ms") {
        options.duration_ms = std::strtoull(value, nullptr, 0);
        return {};
    }
    if (token == "--latency-ns") {
        options.latency_ns = std::strtoull(value, nullptr, 0);
        return {};
    }
    if (token == "--jitter-ns") {
        options.jitter_ns = std::strtoull(value, nullptr, 0);
        return {};
    }
    return std::unexpected(core::ErrorCode::InvalidArgument);
}

core::Status apply_value_option(std::string_view token, const char* value,
                                CanToolOptions& options) {
    if (token == "--app-id" || token == "-ai") {
        options.app_id = value;
        return {};
    }
    if (token == "--app-name" || token == "-an") {
        options.app_name = value;
        return {};
    }
    if (token == "--backend") {
        options.backend = value;
        if (options.backend != "hardware" && options.backend != "virtual") {
            std::fprintf(stderr, "can: --backend must be hardware or virtual\n");
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        return {};
    }
    if (token == "--channel") {
        return add_channel(options, value);
    }
    if (token == "--config") {
        return load_config(options, value);
    }
    if (token.starts_with("--filter-")) {
        return apply_monitor_option(token, value, options);
    }
    return apply_virtual_option(token, value, options);
}

[[nodiscard]] bool option_value(int argc, const char* const argv[], int& i,
                                std::string_view token) noexcept {
    static constexpr std::string_view value_options[] = {
        "--app-id",    "-ai",      "--app-name",       "-an",           "--backend",
        "--channel",   "--config", "--filter-channel", "--filter-kind", "--filter-origin",
        "--filter-id", "--seed",   "--duration-ms",    "--latency-ns",  "--jitter-ns",
    };
    const auto* begin = std::begin(value_options);
    const auto* end = std::end(value_options);
    const bool takes_value = std::find(begin, end, token) != end;
    if (!takes_value) {
        return false;
    }
    if (i + 1 >= argc || argv[i + 1] == nullptr) {
        std::fprintf(stderr, "can: option %.*s requires a value\n", static_cast<int>(token.size()),
                     token.data());
        return false;
    }
    ++i;
    return true;
}

bool handle_virtual_flag(std::string_view token, CanToolOptions& options) noexcept {
    const auto set = [&](bool CanToolOptions::* field) { options.*field = true; };
    if (token == "--external") {
        set(&CanToolOptions::external);
        return true;
    }
    if (token == "--inject-errors") {
        set(&CanToolOptions::inject_errors);
        return true;
    }
    if (token == "--inject-overload") {
        set(&CanToolOptions::inject_overload);
        return true;
    }
    if (token == "--inject-bus-off") {
        set(&CanToolOptions::inject_bus_off);
        return true;
    }
    if (token == "--inject-warning") {
        set(&CanToolOptions::inject_warning);
        return true;
    }
    if (token == "--inject-passive") {
        set(&CanToolOptions::inject_passive);
        return true;
    }
    if (token == "--inject-drop") {
        set(&CanToolOptions::inject_drop);
        return true;
    }
    if (token == "--inject-duplicate") {
        set(&CanToolOptions::inject_duplicate);
        return true;
    }
    if (token == "--inject-reorder") {
        set(&CanToolOptions::inject_reorder);
        return true;
    }
    if (token == "--inject-burst") {
        set(&CanToolOptions::inject_burst);
        return true;
    }
    return false;
}

bool handle_flag(std::string_view token, CanToolOptions& options) noexcept {
    if (token == "-h" || token == "--help") {
        options.help = true;
        return true;
    }
    if (token == "--embedded") {
        options.embedded = true;
        return true;
    }
    if (token == "--json") {
        options.json = true;
        return true;
    }
    if (token == "--errors-only") {
        options.errors_only = true;
        return true;
    }
    return handle_virtual_flag(token, options);
}

} // namespace

core::Result<CanToolOptions> parse_tool_options(int argc, const char* const argv[]) noexcept {
    CanToolOptions options{};
    for (int i = 1; i < argc; ++i) {
        const std::string_view token = argv[i] == nullptr ? std::string_view{} : argv[i];
        if (handle_flag(token, options)) {
            continue;
        }
        if (!option_value(argc, argv, i, token)) {
            std::fprintf(stderr, "can: unknown option %.*s\n", static_cast<int>(token.size()),
                         token.data());
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (core::Status applied = apply_value_option(token, argv[i], options); !applied) {
            return std::unexpected(applied.error());
        }
    }
    if (options.help) {
        return options;
    }
    if (options.app_id.empty()) {
        std::fprintf(stderr, "can: --app-id is required\n");
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (options.channels.empty()) {
        std::fprintf(stderr, "can: at least one --channel or --config is required\n");
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (core::Status valid = validate_channels(options.channels); !valid) {
        std::fprintf(stderr, "can: invalid channel list\n");
        return std::unexpected(valid.error());
    }
    return options;
}

} // namespace nexigen::hw::can
