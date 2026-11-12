// nexigen::app — parsed command line (spec app/command-line).
#pragma once
#include <optional>
#include <string>
#include <nlohmann/json.hpp>
#include <nexigen/core/log_level.hpp>
namespace nexigen::app {

/// @brief The accepted command line after parsing.
///
/// Holds only what the operator supplied: the two settings directories, the
/// telemetry application name, an optional log-level override, and the help
/// flag. Default-constructed members mean "option absent"; an empty directory
/// disables that source, and an empty `app_name` imposes no identity.
///
/// @note Spec `app/command-line`, requirement "Long and short option forms are
///       supported".
/// @note Spec `app/command-line`, requirement "CLI log level overrides loaded
///       settings".
/// @note Rendering, JSON and formatting follow project mandates 9, 10 and 11.
struct CommandLineOptions {
    std::string global_settings_dir{}; ///< Absolute global settings directory ("" = absent).
    std::string local_settings_dir{};  ///< Relative local settings directory ("" = absent).
    std::string app_name{};            ///< Telemetry application name ("" = absent).
    std::optional<core::LogLevel> log_level{}; ///< CLI log-level override; empty = keep loaded.
    bool help{false};                          ///< `-h`/`--help` was supplied.

    /// @brief Render the parsed options as a cold-path string.
    ///
    /// @return Text naming both directories, the app name, the level and the
    ///         help flag.
    /// @note Spec `app/command-line`, requirement "App name round-trips through
    ///       serialization"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};
/// @brief Free diagnostic helper mirroring `CommandLineOptions::to_string()`.
///
/// @param options Parsed options to render.
/// @return The same text the member returns.
/// @note Diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const CommandLineOptions& options) {
    return options.to_string();
}
/// @brief Write the parsed options as JSON (cold path).
///
/// @param j Output JSON object with every option field, including `app_name`.
/// @param options Parsed options to serialize.
/// @note Spec `app/command-line`, requirement "App name round-trips through
///       serialization"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const CommandLineOptions& options);
/// @brief Read parsed options from JSON (cold path).
///
/// @param j Input JSON; missing or wrong-typed fields leave the matching
///        member unchanged.
/// @param options Destination, never throwing under `JSON_NOEXCEPTION=1`.
/// @note Spec `app/command-line`, requirement "App name round-trips through
///       serialization"; project mandate 10.
void from_json(const nlohmann::json& j, CommandLineOptions& options) noexcept;
} // namespace nexigen::app
