// nexigen::hub — HubRun DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/hub_instance.hpp>
#include <nexigen/hub/hub_suite.hpp>

namespace nexigen::hub {

/// @brief One run root and everything discovered below it.
///
/// A run holds suites from `.suite/<suite_id>` plus instances that are not
/// attached to any suite. `run_key` is the stable root identity assigned by
/// `Discovery` and is part of every stream identity below the run.
struct HubRun {
    std::string run_key{};                           ///< Stable root key (`run-1`, ...).
    std::string run_dir{};                           ///< Canonical run-root directory.
    std::vector<HubSuite> suites{};                  ///< Suites in discovery order.
    std::vector<HubInstance> standalone_instances{}; ///< Instances outside a suite.

    /// @brief Render the run as a diagnostic string.
    ///
    /// @return Text naming the root and its suite/instance counts; allocates.
    /// @note Spec `hub/discovery`, requirement "Discovery types follow the
    ///       serialization contract".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `HubRun::to_string`.
///
/// @param value Run to render.
/// @return The member `to_string()` output.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const HubRun& value) {
    return value.to_string();
}

/// @brief ADL JSON writer for a run entry.
///
/// @param j Output JSON value replaced with the run object.
/// @param value Run to serialize.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const HubRun& value);

/// @brief ADL JSON reader for a run entry.
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
void from_json(const nlohmann::json& j, HubRun& value) noexcept;

} // namespace nexigen::hub
