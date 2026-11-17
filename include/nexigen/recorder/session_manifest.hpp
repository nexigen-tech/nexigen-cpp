/// @file session_manifest.hpp
/// @brief Self-description of one recorded session (`session.json`).
///
/// The manifest mirrors the effective settings, schema/channel registry, and
/// every segment's state, and is replaced atomically after each change.
///
/// @note Spec `recorder/raw-storage`, requirements "Session manifest and segment
///       triplet", "Embedded schema and channel registry", and "Self-contained
///       session portability".
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/recorder/channel_declare.hpp>
#include <nexigen/recorder/recorder_types.hpp>
#include <nexigen/recorder/schema_declare.hpp>
#include <nexigen/recorder/segment_info.hpp>
#include <nexigen/recorder/storage_settings.hpp>

namespace nexigen::recorder {

/// @brief The complete self-description of one recorded session.
///
/// A reader can list, inspect, and decode the session from this object plus the
/// embedded declarations; no external schema, SDK, or environment is required.
///
/// @note Spec `recorder/raw-storage`, requirement "Session manifest and segment
///       triplet"; `clean_close` is false until the final footer is written.
struct SessionManifest {
    RecorderFormat format{RecorderFormat::Raw}; ///< raw only in revision 1
    std::uint16_t version{1};                   ///< format major version
    std::string session_id{};                   ///< safe session id
    TimeDomain time_domain{TimeDomain::MonotonicRaw}; ///< timestamp domain
    StorageSettings settings{};                 ///< effective storage settings
    std::vector<SchemaDeclare> schemas{};       ///< full embedded schema table
    std::vector<ChannelDeclare> channels{};     ///< channel table
    std::vector<SegmentInfo> segments{};        ///< segment catalog
    bool clean_close{false};                    ///< final segment footer written

    /// @brief Validate the schema/channel tables and segment catalog.
    /// @return `Ok`, or `InvalidArgument`/`NotFound`/`Conflict`/`ResourceExhausted`.
    /// @note Cross-checks every channel's schema id and rejects duplicate ids
    ///       before any segment is opened.
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable manifest summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `SessionManifest::to_string`.
/// @param manifest Value to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const SessionManifest& manifest) {
    return manifest.to_string();
}

/// @brief Serialize a session manifest to `session.json`.
/// @param j Destination JSON object.
/// @param manifest Manifest to serialize.
/// @note Spec `recorder/raw-storage`, requirement "Storage type serialization".
void to_json(nlohmann::json& j, const SessionManifest& manifest);

/// @brief Parse a session manifest from `session.json`.
/// @param j Source JSON object.
/// @param manifest Destination manifest.
void from_json(const nlohmann::json& j, SessionManifest& manifest) noexcept;

} // namespace nexigen::recorder
