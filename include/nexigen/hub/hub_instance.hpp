// nexigen::hub — HubInstance DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/hub_attempt.hpp>
#include <nexigen/exec/app_state.hpp>

namespace nexigen::hub {

/// @brief One application instance: its control directory and attempts.
///
/// `instance_dir` is the directory the execution_manager records for the
/// instance; state is derived from `resource.json`/`exit.json` and reported as
/// `unknown` when no usable record exists.
struct HubInstance {
    std::string instance{};                        ///< Instance name, unique within the app.
    std::string instance_dir{};                    ///< Control directory reported by the suite.
    exec::AppState state{exec::AppState::Unknown}; ///< Derived application state.
    std::vector<HubAttempt> attempts{};            ///< Attempts in discovery order.

    /// @brief Render the instance as a diagnostic string.
    ///
    /// @return Text naming the instance, directory, state, and attempt count;
    ///         allocates.
    /// @note Spec `hub/discovery`, requirement "Discovery types follow the
    ///       serialization contract".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `HubInstance::to_string`.
///
/// @param value Instance to render.
/// @return The member `to_string()` output.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const HubInstance& value) {
    return value.to_string();
}

/// @brief ADL JSON writer for an application instance.
///
/// @param j Output JSON value replaced with the instance object.
/// @param value Instance to serialize.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const HubInstance& value);

/// @brief ADL JSON reader for an application instance.
///
/// Hostile or malformed input leaves `value` unchanged; nothing is thrown
/// under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON value to read.
/// @param value Destination, untouched unless the whole object parses.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract" (hostile tree JSON is ignored).
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, HubInstance& value) noexcept;

} // namespace nexigen::hub
