// nexigen::app — failure payload for command-line parsing (spec app/command-line).
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <nexigen/app/command_line_diagnostic.hpp>
#include <nexigen/core/result.hpp>
namespace nexigen::app {

/// @brief Failure payload of `CommandLineResult`.
///
/// Carries the aggregate error code plus every diagnostic collected for the
/// rejected invocation. Parsing failures never lose the offending spelling, so
/// the caller can print all findings before exiting non-zero.
///
/// @note Spec `app/command-line`, requirement "Unknown, missing, repeated, and
///       positional arguments are handled".
/// @note Rendering, JSON and formatting follow project mandates 9, 10 and 11.
struct CommandLineError {
    core::ErrorCode code{core::ErrorCode::InvalidArgument}; ///< Aggregate failure code.
    std::vector<CommandLineDiagnostic> diagnostics{};       ///< Every finding, in parse order.

    /// @brief Render the failure as a cold-path string.
    ///
    /// @return Text naming the error code and every diagnostic.
    /// @note Spec `app/command-line`, requirement "Unknown, missing, repeated,
    ///       and positional arguments are handled"; diagnostic only (project
    ///       mandate 9).
    [[nodiscard]] std::string to_string() const;
};
/// @brief Free diagnostic helper mirroring `CommandLineError::to_string()`.
///
/// @param error Failure payload to render.
/// @return The same text the member returns.
/// @note Diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const CommandLineError& error) {
    return error.to_string();
}
/// @brief Write the failure as JSON (cold path).
///
/// @param j Output JSON object with the error code and the diagnostics array.
/// @param error Failure payload to serialize.
/// @note JSON only (project mandate 10).
void to_json(nlohmann::json& j, const CommandLineError& error);
/// @brief Read a failure payload from JSON (cold path).
///
/// @param j Input JSON; malformed or missing fields leave the matching member
///        unchanged.
/// @param error Destination, never throwing under `JSON_NOEXCEPTION=1`.
/// @note Project mandate 10: hostile JSON must not throw or abort.
void from_json(const nlohmann::json& j, CommandLineError& error) noexcept;
} // namespace nexigen::app
