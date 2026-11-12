// nexigen::hub — HubSuite DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/hub_app.hpp>
#include <nexigen/hub/hub_stream.hpp>
#include <nexigen/exec/suite_state.hpp>

namespace nexigen::hub {

/// @brief One suite, its execution_manager log, and its applications.
///
/// The suite metadata comes from `<run_dir>/.suite/<suite_id>/suite.json` and
/// the state vocabulary matches the execution_manager: `unknown` is reported
/// when no usable `suite-state.json`/`suite-exit.json` record exists. The
/// execution_manager log is the stream at
/// `<artifacts_path>/<suite_id>/execution_manager.jsonl` and stays unset when
/// the suite records no id or artifact path.
struct HubSuite {
    std::string suite_id{};       ///< Suite identifier from its metadata.
    std::string suite_dir{};      ///< `.suite/<suite_id>` metadata directory.
    std::string artifacts_path{}; ///< Contained artifact root from `suite.json`.
    exec::SuiteState state{exec::SuiteState::Unknown}; ///< Derived aggregate state.
    std::uint32_t suite_restarts_used{0};              ///< Restart budget already consumed.
    std::string reason{};                              ///< Stop/failure reason when recorded.
    HubStream execution_manager_log{};                 ///< Suite execution_manager JSONL stream.
    std::vector<HubApp> apps{};                        ///< Applications in discovery order.

    /// @brief Render the suite as a diagnostic string.
    ///
    /// @return Text naming the suite, state, restart usage, and app count;
    ///         allocates.
    /// @note Spec `hub/discovery`, requirement "Discovery types follow the
    ///       serialization contract".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `HubSuite::to_string`.
///
/// @param value Suite to render.
/// @return The member `to_string()` output.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const HubSuite& value) {
    return value.to_string();
}

/// @brief ADL JSON writer for a suite entry.
///
/// @param j Output JSON value replaced with the suite object.
/// @param value Suite to serialize.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const HubSuite& value);

/// @brief ADL JSON reader for a suite entry.
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
void from_json(const nlohmann::json& j, HubSuite& value) noexcept;

} // namespace nexigen::hub
