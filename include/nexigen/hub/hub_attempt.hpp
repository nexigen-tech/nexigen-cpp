// nexigen::hub — HubAttempt DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/hub_stream.hpp>

namespace nexigen::hub {

/// @brief One launch attempt and the streams written under it.
///
/// Attempt directories are ordered by number, and each attempt owns a distinct
/// set of stream identities: a new attempt never appends to the previous one.
struct HubAttempt {
    std::uint32_t attempt{0};         ///< Launch attempt number, e.g. `2`.
    std::string artifact_dir{};       ///< Artifact directory of this attempt.
    std::vector<HubStream> streams{}; ///< Streams discovered under the attempt.

    /// @brief Render the attempt as a diagnostic string.
    ///
    /// @return Text naming the attempt number, artifact directory, and stream
    ///         count; allocates.
    /// @note Spec `hub/discovery`, requirement "Discovery types follow the
    ///       serialization contract".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `HubAttempt::to_string`.
///
/// @param value Attempt to render.
/// @return The member `to_string()` output.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const HubAttempt& value) {
    return value.to_string();
}

/// @brief ADL JSON writer for a launch attempt.
///
/// @param j Output JSON value replaced with the attempt object.
/// @param value Attempt to serialize.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const HubAttempt& value);

/// @brief ADL JSON reader for a launch attempt.
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
void from_json(const nlohmann::json& j, HubAttempt& value) noexcept;

} // namespace nexigen::hub
