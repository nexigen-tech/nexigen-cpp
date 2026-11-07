// nexigen::app — cold-path command-line contract (change
// add-app-command-line; spec app/command-line).
//
// COLD PATH ONLY. Parsing, validation, path mapping and diagnostics run once
// before Infrastructure::initialize; no producer, tick, worker or signal path
// calls into this unit. The parser is dependency-free and models the
// boost::program_options subset used by the application layer.
#pragma once

#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/app/app_settings.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::app {

/// One command-line finding: the offending option/argument and why it failed.
struct CommandLineDiagnostic {
    std::string option{};  ///< Offending option or argument, e.g. "--global-settings".
    std::string message{}; ///< Human-readable explanation.

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const CommandLineDiagnostic& diagnostic) {
    return diagnostic.to_string();
}

/// Parsed command line. Empty directories mean "source disabled"; an empty
/// log level means "no CLI override".
struct CommandLineOptions {
    std::string global_settings_dir{};
    std::string local_settings_dir{};
    std::string app_name{};
    std::optional<logging::LogLevel> log_level{};
    bool help{false};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const CommandLineOptions& options) {
    return options.to_string();
}

/// Failure payload: an error code plus every diagnostic collected while
/// parsing. The parser never prints or exits; the host owns that policy.
struct CommandLineError {
    core::ErrorCode code{core::ErrorCode::InvalidArgument};
    std::vector<CommandLineDiagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const CommandLineError& error) {
    return error.to_string();
}

using CommandLineResult = core::Result<CommandLineOptions, CommandLineError>;

/// Parse process arguments. `argv[0]` (the program name) is skipped.
[[nodiscard]] CommandLineResult parse_command_line(int argc, const char* const argv[]) noexcept;

/// Parse already-split arguments. Every element is treated as an argument.
[[nodiscard]] CommandLineResult parse_command_line(std::span<const std::string_view> args) noexcept;

/// Map the two directories to the four fixed global/local file pairs. An
/// empty directory leaves that source pair empty (disabled).
[[nodiscard]] AppSettingsPaths make_app_settings_paths(const CommandLineOptions& options) noexcept;

/// Apply the CLI log level to already-loaded settings. The CLI wins over
/// global/local/environment sources; an absent level preserves the loaded one.
void apply_to(const CommandLineOptions& options, Settings& settings) noexcept;

/// Map paths, load all four subsystems, then apply the CLI log level even when
/// loading reports a diagnostic. Returns InvalidArgument when both directories
/// are empty, so a manually built options value cannot load built-in defaults.
[[nodiscard]] core::Status load_app_settings(const CommandLineOptions& options,
                                             Settings& out) noexcept;

/// Static usage text naming every option, the accepted level spellings and the
/// four fixed file names. The caller prints it; this function never fails.
[[nodiscard]] std::string_view command_line_usage() noexcept;

// --- cold-path ADL serialization (bodies in src/app/command_line.cpp)
void to_json(nlohmann::json& j, const CommandLineDiagnostic& diagnostic);
void from_json(const nlohmann::json& j, CommandLineDiagnostic& diagnostic) noexcept;

void to_json(nlohmann::json& j, const CommandLineOptions& options);
void from_json(const nlohmann::json& j, CommandLineOptions& options) noexcept;

void to_json(nlohmann::json& j, const CommandLineError& error);
void from_json(const nlohmann::json& j, CommandLineError& error) noexcept;

} // namespace nexigen::app
