// nexigen::app — cold-path command-line parser (change add-app-command-line;
// design D1–D5). Everything here runs once during startup: std::string,
// std::vector and nlohmann::json are allowed; no tick/producer path calls in.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <nexigen/app/command_line.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nexigen::app {
namespace {

enum class Value_kind { Global, Local, Log_level };

struct Option_spec {
    std::string_view long_name;
    std::string_view short_name;
    Value_kind kind;
};

// The registered short names are mutually non-prefixing, so declaration order is unambiguous.
constexpr Option_spec kOptions[] = {
    {"--global-settings", "-gs", Value_kind::Global},
    {"--local-settings", "-ls", Value_kind::Local},
    {"--log-level", "-lgl", Value_kind::Log_level},
};

struct Collected {
    std::string global_settings_dir{};
    std::string local_settings_dir{};
    std::string log_level{};
    bool global_supplied{false};
    bool local_supplied{false};
    bool log_level_supplied{false};
};

[[nodiscard]] bool looks_like_option(std::string_view token) noexcept {
    return token.size() > 1 && token.front() == '-';
}

void add_diagnostic(std::vector<Command_line_diagnostic>& diagnostics,
                    std::string_view option, std::string_view message) {
    diagnostics.push_back(Command_line_diagnostic{std::string(option), std::string(message)});
}

void store_value(Collected& collected, Value_kind kind, std::string_view value) {
    switch (kind) {
        case Value_kind::Global:
            collected.global_settings_dir = value;
            collected.global_supplied = true;
            break;
        case Value_kind::Local:
            collected.local_settings_dir = value;
            collected.local_supplied = true;
            break;
        case Value_kind::Log_level:
            collected.log_level = value;
            collected.log_level_supplied = true;
            break;
    }
}

[[nodiscard]] bool take_next_value(std::span<const std::string_view> args,
                                   std::size_t& index, std::string_view option,
                                   std::vector<Command_line_diagnostic>& diagnostics,
                                   std::string_view& value) {
    if (index + 1 >= args.size()) {
        add_diagnostic(diagnostics, option, "missing required value");
        return false;
    }
    const std::string_view candidate = args[index + 1];
    if (looks_like_option(candidate)) {
        add_diagnostic(diagnostics, option,
                       "missing required value (use --name=value for values starting with '-')");
        return false;
    }
    ++index;
    value = candidate;
    return true;
}

void parse_long(std::string_view token, std::span<const std::string_view> args,
                std::size_t& index, Collected& collected,
                std::vector<Command_line_diagnostic>& diagnostics) {
    const std::size_t equals = token.find('=');
    const std::string_view name =
        equals == std::string_view::npos ? token : token.substr(0, equals);
    for (const Option_spec& spec : kOptions) {
        if (name != spec.long_name) {
            continue;
        }
        if (equals != std::string_view::npos) {
            store_value(collected, spec.kind, token.substr(equals + 1));
            return;
        }
        std::string_view value;
        if (take_next_value(args, index, name, diagnostics, value)) {
            store_value(collected, spec.kind, value);
        }
        return;
    }
    add_diagnostic(diagnostics, name, "unknown option");
}

void parse_short(std::string_view token, std::span<const std::string_view> args,
                 std::size_t& index, Collected& collected,
                 std::vector<Command_line_diagnostic>& diagnostics) {
    for (const Option_spec& spec : kOptions) {
        if (token == spec.short_name) {
            std::string_view value;
            if (take_next_value(args, index, spec.short_name, diagnostics, value)) {
                store_value(collected, spec.kind, value);
            }
            return;
        }
        if (token.size() > spec.short_name.size() &&
            token.substr(0, spec.short_name.size()) == spec.short_name) {
            std::string_view value = token.substr(spec.short_name.size());
            if (!value.empty() && value.front() == '=') {
                value.remove_prefix(1);
            }
            store_value(collected, spec.kind, value);
            return;
        }
    }
    add_diagnostic(diagnostics, token, "unknown option");
}

[[nodiscard]] Command_line_result finish_parse(
    Collected&& collected, std::vector<Command_line_diagnostic>&& diagnostics) {
    Command_line_options options{};

    if (!collected.global_supplied && !collected.local_supplied) {
        add_diagnostic(diagnostics, "--global-settings/--local-settings",
                       "at least one settings directory is required");
    }
    if (collected.global_supplied) {
        if (collected.global_settings_dir.empty()) {
            add_diagnostic(diagnostics, "--global-settings",
                           "expected a non-empty absolute directory path");
        } else if (collected.global_settings_dir.front() != '/') {
            add_diagnostic(diagnostics, "--global-settings",
                           "expected an absolute directory path");
        }
    }
    if (collected.local_supplied) {
        if (collected.local_settings_dir.empty()) {
            add_diagnostic(diagnostics, "--local-settings",
                           "expected a non-empty relative directory path");
        } else if (collected.local_settings_dir.front() == '/') {
            add_diagnostic(diagnostics, "--local-settings",
                           "expected a relative directory path");
        }
    }
    if (collected.log_level_supplied) {
        logging::LogLevel level = logging::LogLevel::Info;
        if (logging::from_string(collected.log_level, level)) {
            options.log_level = level;
        } else {
            add_diagnostic(diagnostics, "--log-level",
                           "expected one of verbose/debug/info/warning/error/fatal/off");
        }
    }

    if (!diagnostics.empty()) {
        return std::unexpected(
            Command_line_error{core::ErrorCode::InvalidArgument, std::move(diagnostics)});
    }

    options.global_settings_dir = std::move(collected.global_settings_dir);
    options.local_settings_dir = std::move(collected.local_settings_dir);
    return options;
}

[[nodiscard]] std::string join_path(const std::string& directory,
                                    std::string_view file) {
    if (directory.empty()) {
        return {};
    }
    std::string path = directory;
    if (path.back() != '/') {
        path.push_back('/');
    }
    path.append(file);
    return path;
}

constexpr std::string_view kUsage =
    "Usage: <program> [options]\n"
    "  -gs, --global-settings <absolute-dir>  global settings directory\n"
    "  -ls, --local-settings <relative-dir>   local settings directory\n"
    "  -lgl, --log-level <level>              verbose|debug|info|warning|error|fatal|off\n"
    "  -h, --help                             show this help\n"
    "Value forms: --name value, --name=value, -name value, -namevalue, -name=value.\n"
    "Use -- to end option parsing; positional arguments are rejected.\n"
    "At least one of --global-settings or --local-settings is required.\n"
    "Each directory may contain logging.json, metrics.json, tracing.json and chrono.json.\n";

} // namespace

std::string Command_line_diagnostic::to_string() const {
    return "Command_line_diagnostic(option=\"" + option + "\", message=\"" + message +
           "\")";
}

std::string Command_line_options::to_string() const {
    std::string level = "none";
    if (log_level.has_value()) {
        level = std::string(logging::to_string(*log_level));
    }
    return "Command_line_options(global_settings_dir=\"" + global_settings_dir +
           "\", local_settings_dir=\"" + local_settings_dir + "\", log_level=" + level +
           ", help=" + std::string(help ? "true" : "false") + ")";
}

std::string Command_line_error::to_string() const {
    std::string out = "Command_line_error(code=" + std::string(core::to_string(code)) +
                      ", diagnostics=[";
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        if (i != 0) {
            out += "; ";
        }
        out += diagnostics[i].to_string();
    }
    out += "])";
    return out;
}

void to_json(nlohmann::json& j, const Command_line_diagnostic& diagnostic) {
    j = nlohmann::json{{"option", diagnostic.option}, {"message", diagnostic.message}};
}

void from_json(const nlohmann::json& j, Command_line_diagnostic& diagnostic) noexcept {
    if (!j.is_object()) {
        return;
    }
    Command_line_diagnostic parsed{};
    auto it = j.find("option");
    if (it != j.end()) {
        if (!it->is_string()) {
            return;
        }
        parsed.option = it->get<std::string>();
    }
    it = j.find("message");
    if (it != j.end()) {
        if (!it->is_string()) {
            return;
        }
        parsed.message = it->get<std::string>();
    }
    diagnostic = std::move(parsed);
}

void to_json(nlohmann::json& j, const Command_line_options& options) {
    nlohmann::json level = nullptr;
    if (options.log_level.has_value()) {
        level = std::string(logging::to_string(*options.log_level));
    }
    j = nlohmann::json{{"global_settings_dir", options.global_settings_dir},
                       {"local_settings_dir", options.local_settings_dir},
                       {"log_level", std::move(level)},
                       {"help", options.help}};
}

void from_json(const nlohmann::json& j, Command_line_options& options) noexcept {
    if (!j.is_object()) {
        return;
    }
    Command_line_options parsed{};
    parsed.log_level = logging::LogLevel::Info;
    auto it = j.find("global_settings_dir");
    if (it != j.end()) {
        if (!it->is_string()) {
            return;
        }
        parsed.global_settings_dir = it->get<std::string>();
    }
    it = j.find("local_settings_dir");
    if (it != j.end()) {
        if (!it->is_string()) {
            return;
        }
        parsed.local_settings_dir = it->get<std::string>();
    }
    it = j.find("log_level");
    if (it != j.end()) {
        if (it->is_null()) {
            parsed.log_level.reset();
        } else if (it->is_string()) {
            logging::LogLevel level = logging::LogLevel::Info;
            if (!logging::from_string(it->get<std::string>(), level)) {
                return;
            }
            parsed.log_level = level;
        } else {
            return;
        }
    }
    it = j.find("help");
    if (it != j.end()) {
        if (!it->is_boolean()) {
            return;
        }
        parsed.help = it->get<bool>();
    }
    options = std::move(parsed);
}

void to_json(nlohmann::json& j, const Command_line_error& error) {
    nlohmann::json diagnostics = nlohmann::json::array();
    for (const Command_line_diagnostic& diagnostic : error.diagnostics) {
        nlohmann::json entry;
        to_json(entry, diagnostic);
        diagnostics.push_back(std::move(entry));
    }
    j = nlohmann::json{{"code", std::string(core::to_string(error.code))},
                       {"diagnostics", std::move(diagnostics)}};
}

void from_json(const nlohmann::json& j, Command_line_error& error) noexcept {
    if (!j.is_object()) {
        return;
    }
    Command_line_error parsed{};
    auto it = j.find("code");
    if (it != j.end()) {
        if (!it->is_string()) {
            return;
        }
        core::ErrorCode code = parsed.code;
        if (!core::from_string(it->get<std::string>(), code)) {
            return;
        }
        parsed.code = code;
    }
    it = j.find("diagnostics");
    if (it != j.end()) {
        if (!it->is_array()) {
            return;
        }
        std::vector<Command_line_diagnostic> parsed_diagnostics;
        parsed_diagnostics.reserve(it->size());
        for (const nlohmann::json& entry : *it) {
            if (!entry.is_object()) {
                return;
            }
            const auto option = entry.find("option");
            if (option != entry.end() && !option->is_string()) {
                return;
            }
            const auto message = entry.find("message");
            if (message != entry.end() && !message->is_string()) {
                return;
            }
            Command_line_diagnostic diagnostic{};
            from_json(entry, diagnostic);
            parsed_diagnostics.push_back(std::move(diagnostic));
        }
        parsed.diagnostics = std::move(parsed_diagnostics);
    }
    error = std::move(parsed);
}

Command_line_result parse_command_line(int argc, const char* const argv[]) noexcept {
    if (argc <= 1 || argv == nullptr) {
        return parse_command_line(std::span<const std::string_view>{});
    }
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc - 1));
    for (int index = 1; index < argc; ++index) {
        const char* const argument = argv[index];
        args.emplace_back(argument == nullptr ? std::string_view{}
                                              : std::string_view{argument});
    }
    return parse_command_line(std::span<const std::string_view>{args.data(), args.size()});
}

Command_line_result parse_command_line(
    std::span<const std::string_view> args) noexcept {
    // Help wins over every other argument and does not require a directory.
    for (const std::string_view token : args) {
        if (token == "--") {
            break;
        }
        if (token == "-h" || token == "--help") {
            Command_line_options options{};
            options.help = true;
            return options;
        }
    }

    Collected collected{};
    std::vector<Command_line_diagnostic> diagnostics;
    bool options_done = false;

    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string_view token = args[index];
        if (!options_done) {
            if (token == "--") {
                options_done = true;
                continue;
            }
            if (token == "-") {
                add_diagnostic(diagnostics, token, "unknown option");
                continue;
            }
            if (token.size() > 2 && token.front() == '-' && token[1] == '-') {
                parse_long(token, args, index, collected, diagnostics);
                continue;
            }
            if (token.size() > 1 && token.front() == '-') {
                parse_short(token, args, index, collected, diagnostics);
                continue;
            }
        }
        add_diagnostic(diagnostics, token, "unexpected positional argument");
    }

    return finish_parse(std::move(collected), std::move(diagnostics));
}

App_settings_paths make_app_settings_paths(
    const Command_line_options& options) noexcept {
    App_settings_paths paths{};
    paths.logging.global_file = join_path(options.global_settings_dir, "logging.json");
    paths.logging.local_file = join_path(options.local_settings_dir, "logging.json");
    paths.metrics.global_file = join_path(options.global_settings_dir, "metrics.json");
    paths.metrics.local_file = join_path(options.local_settings_dir, "metrics.json");
    paths.tracing.global_file = join_path(options.global_settings_dir, "tracing.json");
    paths.tracing.local_file = join_path(options.local_settings_dir, "tracing.json");
    paths.chrono.global_file = join_path(options.global_settings_dir, "chrono.json");
    paths.chrono.local_file = join_path(options.local_settings_dir, "chrono.json");
    return paths;
}

void apply_to(const Command_line_options& options, App_settings& settings) noexcept {
    if (options.log_level.has_value()) {
        settings.logging.options.initial_log_level = *options.log_level;
    }
}

core::Status load_app_settings(const Command_line_options& options,
                               App_settings& out) noexcept {
    if (options.global_settings_dir.empty() && options.local_settings_dir.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const App_settings_paths paths = make_app_settings_paths(options);
    const core::Status status = nexigen::app::load_app_settings(paths, out);
    apply_to(options, out);
    return status;
}

std::string_view command_line_usage() noexcept {
    return kUsage;
}

} // namespace nexigen::app
