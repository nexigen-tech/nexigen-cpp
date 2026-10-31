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
struct Command_line_diagnostic {
    std::string option{};  ///< Offending option or argument, e.g. "--global-settings".
    std::string message{}; ///< Human-readable explanation.

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Command_line_diagnostic& diagnostic) {
    return diagnostic.to_string();
}

/// Parsed command line. Empty directories mean "source disabled"; an empty
/// log level means "no CLI override".
struct Command_line_options {
    std::string global_settings_dir{};
    std::string local_settings_dir{};
    std::optional<logging::LogLevel> log_level{};
    bool help{false};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Command_line_options& options) {
    return options.to_string();
}

/// Failure payload: an error code plus every diagnostic collected while
/// parsing. The parser never prints or exits; the host owns that policy.
struct Command_line_error {
    core::ErrorCode code{core::ErrorCode::InvalidArgument};
    std::vector<Command_line_diagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Command_line_error& error) {
    return error.to_string();
}

using Command_line_result = core::Result<Command_line_options, Command_line_error>;

/// Parse process arguments. `argv[0]` (the program name) is skipped.
[[nodiscard]] Command_line_result parse_command_line(
    int argc, const char* const argv[]) noexcept;

/// Parse already-split arguments. Every element is treated as an argument.
[[nodiscard]] Command_line_result parse_command_line(
    std::span<const std::string_view> args) noexcept;

/// Map the two directories to the four fixed global/local file pairs. An
/// empty directory leaves that source pair empty (disabled).
[[nodiscard]] App_settings_paths make_app_settings_paths(
    const Command_line_options& options) noexcept;

/// Apply the CLI log level to already-loaded settings. The CLI wins over
/// global/local/environment sources; an absent level preserves the loaded one.
void apply_to(const Command_line_options& options, App_settings& settings) noexcept;

/// Map paths, load all four subsystems, then apply the CLI log level even when
/// loading reports a diagnostic. Returns InvalidArgument when both directories
/// are empty, so a manually built options value cannot load built-in defaults.
[[nodiscard]] core::Status load_app_settings(
    const Command_line_options& options, App_settings& out) noexcept;

/// Static usage text naming every option, the accepted level spellings and the
/// four fixed file names. The caller prints it; this function never fails.
[[nodiscard]] std::string_view command_line_usage() noexcept;

// --- cold-path ADL serialization (bodies in src/app/command_line.cpp)
void to_json(nlohmann::json& j, const Command_line_diagnostic& diagnostic);
void from_json(const nlohmann::json& j, Command_line_diagnostic& diagnostic) noexcept;

void to_json(nlohmann::json& j, const Command_line_options& options);
void from_json(const nlohmann::json& j, Command_line_options& options) noexcept;

void to_json(nlohmann::json& j, const Command_line_error& error);
void from_json(const nlohmann::json& j, Command_line_error& error) noexcept;

} // namespace nexigen::app

template <>
struct std::formatter<nexigen::app::Command_line_diagnostic>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::Command_line_diagnostic& diagnostic,
                          Ctx& ctx) const {
        return std::formatter<std::string_view>::format(diagnostic.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::app::Command_line_options>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::Command_line_options& options,
                          Ctx& ctx) const {
        return std::formatter<std::string_view>::format(options.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::app::Command_line_error>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::Command_line_error& error, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(error.to_string(), ctx);
    }
};
