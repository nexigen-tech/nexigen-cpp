#include "cli.hpp"

#include <charconv>
#include <cstring>

#include "ui_scale.hpp"

namespace nexigen::logview {

std::string_view usage_text() noexcept {
    return "nexigen-logview - realtime LogFrame viewer\n"
           "Usage: nexigen-logview [options]\n"
           "  --config <path>       Configuration JSON (default: XDG config)\n"
           "  --service <name>      Log service (default: Nexigen/Log/nexigen)\n"
           "  --offline <file>      Open a JSONL/plain log without COM\n"
           "  --memory-mb <n>       Hard model memory budget in MiB\n"
           "  --headless            Dump records without opening a window\n"
           "  --max-records <n>     Stop headless after n records\n"
           "  --mock <n>            Generate n synthetic records (headless)\n"
           "  --soak <seconds>      Sustained 50k msg/s mock soak (headless)\n"
           "  --ui-smoke            Render one frame and exit (CI)\n"
           "  --ui-actions-test     Exercise copy/truncation/open actions under Xvfb\n"
           "  --ui-input-test       Drive scripted ImGui clicks under Xvfb\n"
           "  --ui-view-test        Exercise multi-view docking, clone, and limits\n"
           "  --ui-focus-test       Exercise keyboard focus routing under Xvfb\n"
           "  --ui-splash-test      Render the startup splash and exit\n"
           "  --ui-scale <factor>   UI/font scale (e.g. 2 for a 4K display)\n"
           "  --help                Show this help\n";
}

namespace {

bool parse_u64(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

bool next_value(int argc, char* argv[], int& index, std::string_view& out) {
    if (index + 1 >= argc) {
        return false;
    }
    out = argv[++index];
    return true;
}

bool take_string(int argc, char* argv[], int& index, std::string& out) {
    std::string_view value;
    if (!next_value(argc, argv, index, value)) {
        return false;
    }
    out = value;
    return true;
}

bool take_u64(int argc, char* argv[], int& index, std::uint64_t& out) {
    std::string_view value;
    return next_value(argc, argv, index, value) && parse_u64(value, out);
}

bool take_ui_scale(int argc, char* argv[], int& index, float& out) {
    std::string_view value;
    return next_value(argc, argv, index, value) && parse_ui_scale(value, out);
}

bool parse_flag(std::string_view arg, CliOptions& options) {
    if (arg == "--help" || arg == "-h") {
        options.help = true;
        return true;
    }
    if (arg == "--headless") {
        options.headless = true;
        return true;
    }
    if (arg == "--ui-smoke") {
        options.ui_smoke = true;
        return true;
    }
    if (arg == "--ui-actions-test") {
        options.ui_actions_test = true;
        return true;
    }
    if (arg == "--ui-input-test") {
        options.ui_input_test = true;
        return true;
    }
    if (arg == "--ui-view-test") {
        options.ui_view_test = true;
        return true;
    }
    if (arg == "--ui-focus-test") {
        options.ui_focus_test = true;
        return true;
    }
    if (arg == "--ui-splash-test") {
        options.ui_splash_test = true;
        return true;
    }
    return false;
}

enum class ParseOutcome : std::uint8_t { NotHandled, Ok, Error };

ParseOutcome parse_string_option(std::string_view arg, int argc, char* argv[], int& index,
                                 CliOptions& options) {
    if (arg == "--config") {
        if (!take_string(argc, argv, index, options.config_path)) {
            return ParseOutcome::Error;
        }
        options.config_given = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--service") {
        if (!take_string(argc, argv, index, options.service_name)) {
            return ParseOutcome::Error;
        }
        options.service_given = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--offline") {
        if (!take_string(argc, argv, index, options.offline_file)) {
            return ParseOutcome::Error;
        }
        options.offline = true;
        return ParseOutcome::Ok;
    }
    return ParseOutcome::NotHandled;
}

ParseOutcome parse_numeric_option(std::string_view arg, int argc, char* argv[], int& index,
                                  CliOptions& options) {
    if (arg == "--memory-mb") {
        std::uint64_t bytes = 0;
        if (!take_u64(argc, argv, index, bytes) || bytes == 0U || bytes > 16384U) {
            return ParseOutcome::Error;
        }
        options.memory_mb = static_cast<std::uint32_t>(bytes);
        options.memory_given = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--mock") {
        return take_u64(argc, argv, index, options.mock_records) ? ParseOutcome::Ok
                                                                 : ParseOutcome::Error;
    }
    if (arg == "--soak") {
        if (!take_u64(argc, argv, index, options.soak_seconds) || options.soak_seconds == 0U) {
            return ParseOutcome::Error;
        }
        options.headless = true;
        return ParseOutcome::Ok;
    }
    if (arg == "--ui-scale") {
        if (!take_ui_scale(argc, argv, index, options.ui_scale)) {
            return ParseOutcome::Error;
        }
        return ParseOutcome::Ok;
    }
    if (arg == "--max-records") {
        return take_u64(argc, argv, index, options.max_records) ? ParseOutcome::Ok
                                                                : ParseOutcome::Error;
    }
    return ParseOutcome::NotHandled;
}

ParseOutcome parse_option(std::string_view arg, int argc, char* argv[], int& index,
                          CliOptions& options) {
    if (parse_flag(arg, options)) {
        return ParseOutcome::Ok;
    }
    ParseOutcome outcome = parse_string_option(arg, argc, argv, index, options);
    if (outcome != ParseOutcome::NotHandled) {
        return outcome;
    }
    return parse_numeric_option(arg, argc, argv, index, options);
}

} // namespace

nexigen::core::Result<CliOptions> parse_cli(int argc, char* argv[]) {
    CliOptions options{};
    for (int i = 1; i < argc; ++i) {
        const ParseOutcome outcome = parse_option(argv[i], argc, argv, i, options);
        if (outcome == ParseOutcome::Error ||
            (outcome == ParseOutcome::NotHandled && options.help)) {
            return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
        }
        if (outcome == ParseOutcome::NotHandled) {
            return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
        }
    }
    if (options.offline && options.offline_file.empty()) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    return options;
}

} // namespace nexigen::logview
