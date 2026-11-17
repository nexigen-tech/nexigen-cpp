// nexigen::recorder — session storage settings.
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/recorder/recorder_types.hpp>

namespace nexigen::recorder {

/// @brief Cold-path storage configuration validated before a session starts.
struct StorageSettings {
    std::string session_root{"/var/log/nexigen/sessions"}; ///< absolute session root
    std::string session_id{};                              ///< safe relative session id
    std::string session_layout{"{session_id}"};            ///< relative layout; {session_id}/{label} tokens
    RecorderFormat format{RecorderFormat::Raw};            ///< revision 1: Raw only
    TimeDomain time_domain{TimeDomain::MonotonicRaw};      ///< declared session time domain
    std::uint64_t max_segment_bytes{1ULL << 30U};          ///< 1 GiB
    std::uint64_t max_segment_duration_ns{60ULL * 1'000'000'000ULL}; ///< 60 s
    std::uint64_t max_segment_records{0};                  ///< 0 = no record-count cap
    std::uint64_t index_stride_bytes{256ULL * 1024ULL};    ///< 256 KiB
    std::uint64_t index_interval_ns{50ULL * 1'000'000ULL}; ///< 50 ms
    std::uint64_t write_buffer_bytes{1ULL << 20U};         ///< 1 MiB
    std::uint64_t index_buffer_bytes{64ULL * 1024ULL};     ///< 64 KiB
    std::uint64_t video_frame_buffer_bytes{4ULL << 20U};   ///< 4 MiB
    std::uint64_t max_record_bytes{1ULL << 20U};           ///< 1 MiB
    std::uint64_t retention_max_bytes{0};                  ///< 0 = unlimited
    std::uint64_t min_free_bytes{10ULL << 30U};            ///< 10 GiB
    RetentionPolicy retention{RetentionPolicy::Circular};  ///< closed-segment policy
    /// @brief Optional per-frame CRC. Off is the default; spec
    ///        `recorder/video-storage`, scenario "CRC off is the default".
    VideoCrcMode video_crc{VideoCrcMode::Off};

    /// @brief Validate every limit and the format before any file is created.
    /// @return `Ok`, or a named validation error.
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `StorageSettings::to_string`.
/// @param value Value to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const StorageSettings& settings) {
    return settings.to_string();
}

void to_json(nlohmann::json& j, const StorageSettings& settings);
void from_json(const nlohmann::json& j, StorageSettings& settings) noexcept;

} // namespace nexigen::recorder
