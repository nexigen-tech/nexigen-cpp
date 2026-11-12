// nexigen::app — one command-line finding (spec app/command-line).
#pragma once
#include <string>
#include <nlohmann/json.hpp>
namespace nexigen::app {

/// @brief One diagnostic produced while parsing the command line.
///
/// Pairs the offending option spelling with a human-readable message. Every
/// parse failure carries at least one of these; the parser never reports an
/// unnamed option or argument.
///
/// @note Spec `app/command-line`, requirement "Unknown, missing, repeated, and
///       positional arguments are handled".
/// @note Rendering, JSON and formatting follow project mandates 9, 10 and 11.
struct CommandLineDiagnostic {
    std::string option{};  ///< Offending option or argument spelling, as supplied.
    std::string message{}; ///< Human-readable explanation for the caller.

    /// @brief Render the diagnostic as a cold-path string.
    ///
    /// @return Text naming the option and the message.
    /// @note Spec `app/command-line`, requirement "Unknown, missing, repeated,
    ///       and positional arguments are handled"; diagnostic only (project
    ///       mandate 9).
    [[nodiscard]] std::string to_string() const;
};
/// @brief Free diagnostic helper mirroring `CommandLineDiagnostic::to_string()`.
///
/// @param diagnostic Finding to render.
/// @return The same text the member returns.
/// @note Diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const CommandLineDiagnostic& diagnostic) {
    return diagnostic.to_string();
}
/// @brief Write the finding as JSON (cold path).
///
/// @param j Output JSON object with the `option` and `message` fields.
/// @param diagnostic Finding to serialize.
/// @note JSON only (project mandate 10).
void to_json(nlohmann::json& j, const CommandLineDiagnostic& diagnostic);
/// @brief Read a finding from JSON (cold path).
///
/// @param j Input JSON; a non-object or wrong-typed field leaves the matching
///        member unchanged.
/// @param diagnostic Destination, never throwing under `JSON_NOEXCEPTION=1`.
/// @note Project mandate 10: hostile JSON must not throw or abort.
void from_json(const nlohmann::json& j, CommandLineDiagnostic& diagnostic) noexcept;
} // namespace nexigen::app
