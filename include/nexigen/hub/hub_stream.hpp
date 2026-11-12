// nexigen::hub — HubStream DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/hub/stream_kind.hpp>

namespace nexigen::hub {

/// @brief One tailed file and its stable stream identity.
///
/// `id` is derived from run, suite, application, instance, attempt, and stream
/// kind, so it stays stable while the file grows and changes only when a new
/// attempt appears or the source is replaced. `path` is the resolved filesystem
/// location; the descriptor itself never opens the file.
struct HubStream {
    std::string id{};                 ///< Stable identity used by the HTTP and SSE API.
    StreamKind kind{StreamKind::App}; ///< Which stream family the file carries.
    std::string path{};               ///< Filesystem location of the tailed file.

    /// @brief Render the descriptor as a diagnostic string.
    ///
    /// @return Text naming the identity, kind, and path; allocates.
    /// @note Spec `hub/discovery`, requirement "Discovery types follow the
    ///       serialization contract".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `HubStream::to_string`.
///
/// @param value Descriptor to render.
/// @return The member `to_string()` output.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const HubStream& value) {
    return value.to_string();
}

/// @brief ADL JSON writer for a stream descriptor.
///
/// @param j Output JSON value replaced with the descriptor object.
/// @param value Descriptor to serialize.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const HubStream& value);

/// @brief ADL JSON reader for a stream descriptor.
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
void from_json(const nlohmann::json& j, HubStream& value) noexcept;

} // namespace nexigen::hub
