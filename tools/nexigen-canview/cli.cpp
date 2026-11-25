#include "cli.hpp"

#include <charconv>
#include <cstdint>
#include <format>
#include <string_view>

namespace nexigen::canview {

namespace {

enum class ParseOutcome : std::uint8_t { NotHandled, Ok, Error };

bool take_string(int argc, char* argv[], int& index, std::string& out) {
    if (index + 1 >= argc) {
        return false;
    }
    out = argv[++index];
    return true;
}

bool take_u64(int argc, char* argv[], int& index, std::uint64_t& out) {
    if (index + 1 >= argc) {
        return false;
    }
    std::string_view text{argv[++index]};
    int base = 10;
    if (text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2U);
    }
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    out = value;
    return true;
}

bool take_float(int argc, char* argv[], int& index, float& out) {
    if (index + 1 >= argc) {
        return false;
    }
    const std::string_view text{argv[++index]};
    float value = 0.0F;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    out = value;
    return true;
}

ParseOutcome parse_flag(std::string_view arg, CliOptions& options) {
    if (arg == "--help") {
        options.help = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--version") {
        options.version = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--headless") {
        options.headless = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--json") {
        options.json = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--shared-timebase") {
        options.shared_timebase = true;
        options.shared_given = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--errors-only") {
        options.errors_only = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--show-bus-idle") {
        options.show_bus_idle = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--ui-smoke") {
        options.ui_smoke = true;
        options.headless = false;
        return ParseOutcome::Ok;
    }
    if (arg == "--ui-actions-test") {
        options.ui_actions_test = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--ui-input-test") {
        options.ui_input_test = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--ui-view-test") {
        options.ui_view_test = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--ui-focus-test") {
        options.ui_focus_test = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--ui-splash-test") {
        options.ui_splash_test = true;
        return ParseOutcome::Ok;
    }
    return ParseOutcome::NotHandled;
}

ParseOutcome parse_path_option(std::string_view arg, int argc, char* argv[], int& index,
                               CliOptions& options) {
    if (arg == "--config") {
        options.config_given =
            take_string(argc, argv, index, options.config_path) && !options.config_path.empty();
        return options.config_given ? ParseOutcome::Ok : ParseOutcome::Error;
    }
    if (arg == "--offline") {
        options.offline =
            take_string(argc, argv, index, options.offline_file) && !options.offline_file.empty();
        return options.offline ? ParseOutcome::Ok : ParseOutcome::Error;
    }
    if (arg == "--filter-text") {
        return take_string(argc, argv, index, options.filter_text) ? ParseOutcome::Ok
                                                                   : ParseOutcome::Error;
    }
    return ParseOutcome::NotHandled;
}

ParseOutcome parse_time_columns(int argc, char* argv[], int& index, CliOptions& options) {
    std::string value;
    if (!take_string(argc, argv, index, value)) {
        return ParseOutcome::Error;
    }
    if (value == "hw") {
        options.time_columns = TimeColumnMode::HwOnly;
    } else if (value == "utc") {
        options.time_columns = TimeColumnMode::UtcOnly;
    } else if (value == "both") {
        options.time_columns = TimeColumnMode::Both;
    } else {
        return ParseOutcome::Error;
    }
    options.columns_given = true;
    return ParseOutcome::Ok;
}

ParseOutcome parse_timestamp_mode(int argc, char* argv[], int& index, CliOptions& options) {
    std::string value;
    if (!take_string(argc, argv, index, value)) {
        return ParseOutcome::Error;
    }
    if (value == "driver") {
        options.timestamp_mode = TimestampMode::Driver;
    } else if (value == "since-session") {
        options.timestamp_mode = TimestampMode::SinceSessionStart;
    } else if (value == "t0") {
        options.timestamp_mode = TimestampMode::RelativeT0;
    } else {
        return ParseOutcome::Error;
    }
    options.timestamp_given = true;
    return ParseOutcome::Ok;
}

ParseOutcome parse_delta_mode(int argc, char* argv[], int& index, CliOptions& options) {
    std::string value;
    if (!take_string(argc, argv, index, value)) {
        return ParseOutcome::Error;
    }
    if (value == "previous") {
        options.delta_mode = DeltaMode::PreviousVisible;
    } else if (value == "per-id") {
        options.delta_mode = DeltaMode::PerCanId;
    } else if (value == "t0") {
        options.delta_mode = DeltaMode::FromT0;
    } else {
        return ParseOutcome::Error;
    }
    options.delta_given = true;
    return ParseOutcome::Ok;
}

ParseOutcome parse_time_option(std::string_view arg, int argc, char* argv[], int& index,
                               CliOptions& options) {
    if (arg == "--time-columns") {
        return parse_time_columns(argc, argv, index, options);
    }
    if (arg == "--timestamp-mode") {
        return parse_timestamp_mode(argc, argv, index, options);
    }
    if (arg == "--delta-mode") {
        return parse_delta_mode(argc, argv, index, options);
    }
    return ParseOutcome::NotHandled;
}

ParseOutcome parse_string_option(std::string_view arg, int argc, char* argv[], int& index,
                                 CliOptions& options) {
    const ParseOutcome path = parse_path_option(arg, argc, argv, index, options);
    if (path != ParseOutcome::NotHandled) {
        return path;
    }
    return parse_time_option(arg, argc, argv, index, options);
}

ParseOutcome parse_channel(int argc, char* argv[], int& index, CliOptions& options) {
    std::string value;
    if (!take_string(argc, argv, index, value)) {
        return ParseOutcome::Error;
    }
    CanViewChannel channel{};
    const std::size_t separator = value.find(':');
    if (separator == std::string::npos) {
        channel.name = value;
    } else {
        channel.name = value.substr(0U, separator);
        std::uint64_t id = 0;
        const std::string_view text{value.data() + separator + 1U, value.size() - separator - 1U};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), id);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || id > 0xFFFFU) {
            return ParseOutcome::Error;
        }
        channel.channel_id = static_cast<std::uint16_t>(id);
    }
    if (channel.name.empty()) {
        return ParseOutcome::Error;
    }
    channel.enabled = true;
    options.channels.push_back(std::move(channel));
    return ParseOutcome::Ok;
}

ParseOutcome parse_eds(int argc, char* argv[], int& index, CliOptions& options) {
    std::string value;
    if (!take_string(argc, argv, index, value)) {
        return ParseOutcome::Error;
    }
    const std::size_t first = value.find(':');
    const std::size_t second =
        first == std::string::npos ? std::string::npos : value.find(':', first + 1U);
    if (first == std::string::npos || second == std::string::npos || second + 1U >= value.size()) {
        return ParseOutcome::Error;
    }
    auto parse_part = [](std::string_view text, std::uint64_t& out) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    };
    std::uint64_t channel = 0;
    std::uint64_t node = 0;
    if (!parse_part(std::string_view(value.data(), first), channel) ||
        !parse_part(std::string_view(value.data() + first + 1U, second - first - 1U), node) ||
        channel > 0xFFFFU || node == 0U || node > 127U) {
        return ParseOutcome::Error;
    }
    SessionEdsBinding binding{};
    binding.channel_id = static_cast<std::uint16_t>(channel);
    binding.node_id = static_cast<std::uint8_t>(node);
    binding.path = value.substr(second + 1U);
    options.eds.push_back(std::move(binding));
    return ParseOutcome::Ok;
}

ParseOutcome parse_limit_option(std::string_view arg, int argc, char* argv[], int& index,
                                CliOptions& options) {
    std::uint64_t value = 0;
    if (arg == "--views") {
        if (!take_u64(argc, argv, index, value) || value == 0U || value > 8U) {
            return ParseOutcome::Error;
        }
        options.view_count = static_cast<std::uint32_t>(value);
        options.views_given = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--memory-mb") {
        if (!take_u64(argc, argv, index, value) || value < 16U || value > 16'384U) {
            return ParseOutcome::Error;
        }
        options.memory_mb = static_cast<std::uint32_t>(value);
        options.memory_given = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--ui-fps") {
        if (!take_u64(argc, argv, index, value) || value < 5U || value > 120U) {
            return ParseOutcome::Error;
        }
        options.ui_fps = static_cast<std::uint32_t>(value);
        return ParseOutcome::Ok;
    }
    return ParseOutcome::NotHandled;
}

ParseOutcome parse_run_option(std::string_view arg, int argc, char* argv[], int& index,
                              CliOptions& options) {
    std::uint64_t value = 0;
    if (arg == "--max-records") {
        return take_u64(argc, argv, index, options.max_records) ? ParseOutcome::Ok
                                                                : ParseOutcome::Error;
    }
    if (arg == "--mock") {
        return take_u64(argc, argv, index, options.mock_records) ? ParseOutcome::Ok
                                                                 : ParseOutcome::Error;
    }
    if (arg == "--soak") {
        return take_u64(argc, argv, index, options.soak_seconds) ? ParseOutcome::Ok
                                                                 : ParseOutcome::Error;
    }
    if (arg == "--rate") {
        if (!take_u64(argc, argv, index, value) || value == 0U) {
            return ParseOutcome::Error;
        }
        options.mock_rate = value;
        return ParseOutcome::Ok;
    }
    if (arg == "--ui-scale") {
        if (!take_float(argc, argv, index, options.ui_scale) || options.ui_scale < 0.5F ||
            options.ui_scale > 4.0F) {
            return ParseOutcome::Error;
        }
        return ParseOutcome::Ok;
    }
    return ParseOutcome::NotHandled;
}

ParseOutcome parse_range_option(std::string_view arg, int argc, char* argv[], int& index,
                                CliOptions& options) {
    const ParseOutcome limit = parse_limit_option(arg, argc, argv, index, options);
    if (limit != ParseOutcome::NotHandled) {
        return limit;
    }
    return parse_run_option(arg, argc, argv, index, options);
}

ParseOutcome parse_filter_option(std::string_view arg, int argc, char* argv[], int& index,
                                 CliOptions& options) {
    std::uint64_t value = 0;
    if (arg == "--filter-id") {
        if (!take_u64(argc, argv, index, value) || value > 0x1FFFFFFFU) {
            return ParseOutcome::Error;
        }
        options.filter_id = static_cast<std::uint32_t>(value);
        options.filter_id_given = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--filter-mask") {
        if (!take_u64(argc, argv, index, value) || value > 0x1FFFFFFFU) {
            return ParseOutcome::Error;
        }
        options.filter_mask = static_cast<std::uint32_t>(value);
        return ParseOutcome::Ok;
    }
    return ParseOutcome::NotHandled;
}

ParseOutcome parse_numeric_option(std::string_view arg, int argc, char* argv[], int& index,
                                  CliOptions& options) {
    if (arg == "--channel") {
        return parse_channel(argc, argv, index, options);
    }
    if (arg == "--eds") {
        return parse_eds(argc, argv, index, options);
    }
    const ParseOutcome range = parse_range_option(arg, argc, argv, index, options);
    if (range != ParseOutcome::NotHandled) {
        return range;
    }
    return parse_filter_option(arg, argc, argv, index, options);
}

ParseOutcome parse_option(std::string_view arg, int argc, char* argv[], int& index,
                          CliOptions& options) {
    ParseOutcome outcome = parse_flag(arg, options);
    if (outcome != ParseOutcome::NotHandled) {
        return outcome;
    }
    outcome = parse_string_option(arg, argc, argv, index, options);
    if (outcome != ParseOutcome::NotHandled) {
        return outcome;
    }
    return parse_numeric_option(arg, argc, argv, index, options);
}

} // namespace

std::string CliOptions::to_string() const {
    return std::format(
        "CliOptions(config={}, channels={}, views={}, memory_mb={}, shared={}, offline={}, "
        "headless={}, json={}, max_records={}, mock={}, soak={}, rate={}, scale={:.2f})",
        config_path, channels.size(), view_count, memory_mb, shared_timebase, offline_file,
        headless, json, max_records, mock_records, soak_seconds, mock_rate, ui_scale);
}

std::string_view usage_text() noexcept {
    return "nexigen-canview - read-only realtime CAN/CAN-FD monitor\n"
           "\n"
           "Usage: nexigen-canview [options]\n"
           "\n"
           "Configuration:\n"
           "  --config <path>          Configuration file (default: XDG).\n"
           "  --channel <name[:id]>    Enable a channel; repeatable.\n"
           "  --eds <ch>:<node>:<path> Add an EDS binding; repeatable.\n"
           "  --views <1..8>           Number of LIVE All views.\n"
           "  --memory-mb <16..16384>  Model memory budget.\n"
           "  --shared-timebase        Enable cross-channel deltas/T0.\n"
           "  --time-columns hw|utc|both\n"
           "  --timestamp-mode driver|since-session|t0\n"
           "  --delta-mode previous|per-id|t0\n"
           "\n"
           "Filters:\n"
           "  --filter-id <id>         Identifier filter.\n"
           "  --filter-mask <mask>     Identifier mask (default 0x7ff).\n"
           "  --filter-text <text>     Text/hex query.\n"
           "  --errors-only            Error/invalid/gap rows only.\n"
           "  --show-bus-idle          Show bus-idle heartbeats.\n"
           "\n"
           "Input/output:\n"
           "  --offline <path>         Open a JSONL session without COM.\n"
           "  --headless               Text/JSONL output without a window.\n"
           "  --json                   JSONL headless output.\n"
           "  --max-records <n>        Stop after n records.\n"
           "  --mock <n>               Generate n deterministic frames.\n"
           "  --soak <seconds>         Sustained-rate headless run.\n"
           "  --rate <frames/s>        Mock rate (default 50000).\n"
           "\n"
           "UI:\n"
           "  --ui-smoke --ui-actions-test --ui-input-test --ui-view-test\n"
           "  --ui-focus-test --ui-splash-test --ui-scale <0.5..4.0>\n"
           "  --ui-fps <5..120>        Render cap (default 20; ECO halves it)\n"
           "  --help                   Print this usage and exit.\n"
           "  --version                Print the version and exit.\n";
}

nexigen::core::Result<CliOptions> parse_cli(int argc, char* argv[]) {
    CliOptions options{};
    for (int index = 1; index < argc; ++index) {
        const ParseOutcome outcome = parse_option(argv[index], argc, argv, index, options);
        if (outcome == ParseOutcome::Error || outcome == ParseOutcome::NotHandled) {
            return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
        }
    }
    if (options.offline && options.offline_file.empty()) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    return options;
}

} // namespace nexigen::canview
