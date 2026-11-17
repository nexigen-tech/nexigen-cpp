/// @file storage_settings.hpp
/// @brief Session storage limits and policy validated before recording starts.
///
/// The settings are copied into `SessionManifest::settings`, so a copied session
/// carries the limits that produced it. `SessionWriter::create()` rejects an
/// invalid configuration before creating any file.
///
/// @note Spec `recorder/raw-storage`, requirements "Session manifest and segment
///       triplet", "Preallocated allocation-free write path", "Rotation and
///       retention", and "Safe session identifiers and paths"; spec
///       `recorder/video-storage`, requirement "Optional per-frame CRC".
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/recorder/recorder_types.hpp>

namespace nexigen::recorder {

/// @brief Cold-path storage configuration validated before a session starts.
///
/// Every buffer/limit is fixed at `create()`; the write loop never grows a
/// container. The byte cap is a rotation trigger, not a hard per-record limit:
/// a single large record may overshoot it and is never split across segments.
///
/// @note Spec `recorder/raw-storage`, requirement "Rotation and retention";
///       `session_layout` is limited to `{session_id}`/`{label}` tokens and no
///       `..` traversal per requirement "Safe session identifiers and paths".
struct StorageSettings {
    std::string session_root{"/var/log/nexigen/sessions"}; ///< Absolute session root.
    std::string session_id{};                              ///< Safe relative session id.
    std::string session_layout{"{session_id}"};            ///< Relative layout; {session_id}/{label} tokens.
    RecorderFormat format{RecorderFormat::Raw};            ///< revision 1: Raw only
    TimeDomain time_domain{TimeDomain::MonotonicRaw};      ///< declared session time domain
    std::uint64_t max_segment_bytes{1ULL << 30U};          ///< Byte cap; `.nxrec` + `.nxvid`.
    std::uint64_t max_segment_duration_ns{60ULL * 1'000'000'000ULL}; ///< Wall-clock cap.
    std::uint64_t max_segment_records{0};                  ///< 0 = no record-count cap
    std::uint64_t index_stride_bytes{256ULL * 1024ULL};    ///< 256 KiB index stride.
    std::uint64_t index_interval_ns{50ULL * 1'000'000ULL}; ///< 50 ms index interval.
    std::uint64_t write_buffer_bytes{1ULL << 20U};         ///< Preallocated record buffer.
    std::uint64_t index_buffer_bytes{64ULL * 1024ULL};     ///< Preallocated index buffer.
    std::uint64_t video_frame_buffer_bytes{4ULL << 20U};   ///< Max accepted frame size.
    std::uint64_t max_record_bytes{1ULL << 20U};           ///< Max accepted record payload.
    std::uint64_t retention_max_bytes{0};                  ///< 0 = unlimited retained bytes.
    std::uint64_t min_free_bytes{10ULL << 30U};            ///< Free-space floor for a new segment.
    RetentionPolicy retention{RetentionPolicy::Circular};  ///< closed-segment policy
    /// @brief Optional per-frame CRC. Off is the default; spec
    ///        `recorder/video-storage`, scenario "CRC off is the default".
    VideoCrcMode video_crc{VideoCrcMode::Off};

    /// @brief Validate every limit and the format before any file is created.
    /// @return `Ok`, or `InvalidArgument`/`UnsupportedFormat` for a rejected value.
    /// @note Spec `recorder/raw-storage`, requirement "Safe session identifiers
    ///       and paths"; `session_root` must be absolute and `session_layout`
    ///       relative without `..`.
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable summary of the limits and policy.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `StorageSettings::to_string`.
/// @param settings Value to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const StorageSettings& settings) {
    return settings.to_string();
}

/// @brief Serialize storage settings to `session.json` JSON.
/// @param j Destination JSON object.
/// @param settings Settings to serialize.
/// @note Spec `recorder/raw-storage`, requirement "Storage type serialization".
void to_json(nlohmann::json& j, const StorageSettings& settings);

/// @brief Parse storage settings from `session.json` JSON.
/// @param j Source JSON object.
/// @param settings Destination settings; fields absent from `j` keep defaults.
void from_json(const nlohmann::json& j, StorageSettings& settings) noexcept;

} // namespace nexigen::recorder
