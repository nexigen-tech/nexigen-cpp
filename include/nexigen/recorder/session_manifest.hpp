// nexigen::recorder — session manifest (cold-path, atomic JSON).
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
    /// @return `Ok`, or a named validation error.
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `SessionManifest::to_string`.
/// @param value Value to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const SessionManifest& manifest) {
    return manifest.to_string();
}

void to_json(nlohmann::json& j, const SessionManifest& manifest);
void from_json(const nlohmann::json& j, SessionManifest& manifest) noexcept;

} // namespace nexigen::recorder
