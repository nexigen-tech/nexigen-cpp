// nexigen::hub — HubApp DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/hub_instance.hpp>
#include <nexigen/exec/app_state.hpp>

namespace nexigen::hub {

/// @brief One application and all of its instances.
///
/// The application state is the aggregate reported by the execution_manager
/// metadata; the hub never infers it from process names or command lines.
struct HubApp {
    std::string app{};                             ///< Application name.
    exec::AppState state{exec::AppState::Unknown}; ///< Aggregate application state.
    std::vector<HubInstance> instances{};          ///< Instances in discovery order.

    /// @brief Render the application as a diagnostic string.
    ///
    /// @return Text naming the application, state, and instance count;
    ///         allocates.
    /// @note Spec `hub/discovery`, requirement "Discovery types follow the
    ///       serialization contract".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `HubApp::to_string`.
///
/// @param value Application to render.
/// @return The member `to_string()` output.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const HubApp& value) {
    return value.to_string();
}

/// @brief ADL JSON writer for an application entry.
///
/// @param j Output JSON value replaced with the application object.
/// @param value Application to serialize.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const HubApp& value);

/// @brief ADL JSON reader for an application entry.
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
void from_json(const nlohmann::json& j, HubApp& value) noexcept;

} // namespace nexigen::hub
