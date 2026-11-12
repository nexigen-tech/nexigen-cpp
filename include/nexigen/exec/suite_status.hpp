// nexigen::exec — aggregate + per-application suite state
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/exec/suite_app_status.hpp>
#include <nexigen/exec/suite_state.hpp>

namespace nexigen::exec {

/// @brief Aggregate plus per-application suite state.
///
/// The in-memory projection of `suite-state.json`: the aggregate state,
/// suite identity, restart consumption, and one `SuiteAppStatus` per
/// application. `suite_status` derives it from the suite directory, so any
/// process can build it without in-memory state from the starter. Cold path
/// only.
///
/// @note Spec `exec/app-suite`, requirement "Suite status reports aggregate and
///       application state".
/// @note Spec `exec/app-suite`, requirement "`suite_status` is split into
///       metadata/state loading and per-app merging".
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
struct SuiteStatus {
    SuiteState state{SuiteState::Unknown}; ///< Aggregate suite state.
    std::string suite_id{};                ///< Suite id.
    std::string run_dir{};                 ///< Absolute run directory.
    std::string artifacts_path{};          ///< Absolute artifact root.
    std::uint32_t suite_restarts_used{0};  ///< Whole-suite relaunches consumed.
    std::vector<SuiteAppStatus> apps{};    ///< Per-application states in manifest order.

    /// @brief Cold-path diagnostic rendering of the aggregate status.
    ///
    /// @return Human-readable text naming the suite state and applications.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper (project mandate 9): mirrors the member.
///
/// @param value Aggregate status to render.
/// @return The same text as `value.to_string()`.
[[nodiscard]] inline std::string to_string(const SuiteStatus& value) {
    return value.to_string();
}

/// @brief Serialize the aggregate status into `suite-state.json`.
///
/// @param j Destination JSON value.
/// @param status Status to serialize.
/// @note Spec `exec/app-suite`, requirement "Suite metadata lives in a
///       deterministic suite directory"; project mandate 10.
void to_json(nlohmann::json& j, const SuiteStatus& status);

/// @brief Parse the aggregate status from JSON without throwing.
///
/// Hostile or wrong-typed input leaves `status` unchanged.
///
/// @param j Source JSON value.
/// @param status Target status.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void from_json(const nlohmann::json& j, SuiteStatus& status) noexcept;

/// @brief Derive aggregate and per-application state from the suite directory.
///
/// Loads `suite.json`, applies a recorded `suite-state.json` when present,
/// derives the terminal aggregate state from `suite-exit.json`, and merges
/// per-application records with live `ExecutionManager::status` results.
/// Sticky recorded `Failed`/`Restarting` states are preserved; a live running
/// instance overrides an unknown recorded state. A directory without a
/// readable `suite.json` yields aggregate `unknown`.
///
/// @param suite_dir Absolute `<run_dir>/.suite/<suite_id>` directory.
/// @param out Receives the derived status; reset before loading.
/// @return Success, or `SystemError` when a required record cannot be read.
/// @note Spec `exec/app-suite`, requirement "Suite status reports aggregate and
///       application state".
/// @note Spec `exec/app-suite`, requirement "`suite_status` is split into
///       metadata/state loading and per-app merging".
[[nodiscard]] core::Status suite_status(std::string_view suite_dir, SuiteStatus& out) noexcept;

} // namespace nexigen::exec
