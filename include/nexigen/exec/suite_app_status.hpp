// nexigen::exec — one application state in `suite-state.json` and `status`
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/exec/app_state.hpp>

namespace nexigen::exec {

/// @brief One application state in `suite-state.json` and `status`.
///
/// Records the live attempt, artifact directory, and restart consumption of
/// one application as observed by the suite supervisor. It is a value type
/// owned by `SuiteStatus` and is rewritten on the cold supervision path
/// whenever an application transitions.
///
/// @note Spec `exec/app-suite`, requirement "Suite status reports aggregate and
///       application state".
/// @note Spec `exec/app-suite`, requirement "Each attempt writes to a new
///       artifact directory".
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
struct SuiteAppStatus {
    std::string app{};                 ///< Application name.
    std::string instance{};            ///< Instance id.
    std::string instance_dir{};        ///< Control directory shared by all attempts.
    std::string artifact_dir{};        ///< Directory of the current attempt.
    std::uint32_t attempt{0};          ///< 1-based attempt number.
    AppState state{AppState::Unknown}; ///< Current recorded application state.
    std::uint32_t restarts_used{0};    ///< Per-application retries already consumed.

    /// @brief Cold-path diagnostic rendering of the application status.
    ///
    /// @return Human-readable text naming the application, state, and attempt.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper (project mandate 9): mirrors the member.
///
/// @param value Application status to render.
/// @return The same text as `value.to_string()`.
[[nodiscard]] inline std::string to_string(const SuiteAppStatus& value) {
    return value.to_string();
}

/// @brief Serialize an application status into `suite-state.json`.
///
/// @param j Destination JSON value.
/// @param status Status to serialize.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void to_json(nlohmann::json& j, const SuiteAppStatus& status);

/// @brief Parse an application status from JSON without throwing.
///
/// Hostile or wrong-typed input leaves `status` unchanged.
///
/// @param j Source JSON value.
/// @param status Target status.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void from_json(const nlohmann::json& j, SuiteAppStatus& status) noexcept;

} // namespace nexigen::exec
