// nexigen::recorder — binary wire structs of the raw session format.
//
// Fixed-size, little-endian, trivially copyable records shared by the recorder
// writer, reader, and converter. Every struct is pinned by static_assert.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace nexigen::recorder {

inline constexpr std::uint16_t kFormatVersion = 1;
inline constexpr std::uint32_t kRecordMagic = 0x52434431U; // "RCD1"
inline constexpr std::uint32_t kFooterMagic = 0x46545231U; // "FTR1"
inline constexpr std::size_t kVideoAlignment = 4096U;
inline constexpr std::size_t kMaxSchemaBytes = 256U * 1024U;
inline constexpr std::size_t kMaxChannels = 64U;
inline constexpr std::size_t kMaxSchemas = 64U;
inline constexpr std::size_t kVideoExtradataBytes = 1024U;

/// @brief 48-byte prefix of every `.nxrec` record.
struct alignas(8) RecordHeader {
    std::uint32_t magic{kRecordMagic};
    std::uint16_t type{0};
    std::uint16_t flags{0};
    std::uint32_t channel_id{0};
    std::uint32_t payload_size{0};
    std::uint64_t commit_mono_ns{0};
    std::uint64_t sequence{0};
    std::uint64_t publisher_id{0};
    std::uint32_t header_crc32c{0};
    std::uint32_t payload_crc32c{0};

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(RecordHeader) == 48U);
static_assert(alignof(RecordHeader) == 8U);
static_assert(std::is_trivially_copyable_v<RecordHeader>);

/// @brief 64-byte sparse index entry.
struct alignas(8) IndexEntry {
    std::uint64_t commit_ns{0};
    std::uint64_t rec_offset{0};
    std::uint64_t min_event_ns{0};
    std::uint64_t max_event_ns{0};
    std::uint64_t channel_mask{0};
    std::uint64_t end_rec_offset{0};
    std::uint32_t record_count{0};
    std::uint32_t flags{0};
    std::uint32_t entry_crc32c{0};
    std::uint32_t reserved{0};

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(IndexEntry) == 64U);
static_assert(alignof(IndexEntry) == 8U);
static_assert(std::is_trivially_copyable_v<IndexEntry>);

/// @brief 64-byte reference from `.nxrec` into `.nxvid`.
struct alignas(8) VideoFrameRef {
    std::uint64_t vid_offset{0};
    std::uint64_t capture_mono_ns{0};
    std::uint64_t source_sequence{0};
    std::uint64_t config_epoch{0};
    std::uint32_t vid_size{0};
    std::uint32_t vid_crc32c{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint16_t codec{0};
    std::uint16_t bitstream{0};
    std::uint32_t vid_segment_seq{0};
    std::uint64_t reserved{0};

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(VideoFrameRef) == 64U);
static_assert(alignof(VideoFrameRef) == 8U);
static_assert(std::is_trivially_copyable_v<VideoFrameRef>);

/// @brief 1056-byte video stream declaration with bounded extradata.
struct alignas(8) VideoStreamConfig {
    std::uint16_t codec{0};
    std::uint16_t bitstream{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t timebase_num{1};
    std::uint32_t timebase_den{1'000'000'000};
    std::uint16_t extradata_size{0};
    std::uint16_t reserved0{0};
    std::uint64_t config_epoch{0};
    std::array<std::uint8_t, kVideoExtradataBytes> extradata{};

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(VideoStreamConfig) == 1056U);
static_assert(alignof(VideoStreamConfig) == 8U);
static_assert(std::is_trivially_copyable_v<VideoStreamConfig>);

/// @brief 24-byte gap marker payload.
struct alignas(8) GapMarkerPayload {
    std::uint64_t first_lost_sequence{0};
    std::uint64_t lost_count{0};
    std::uint16_t reason{0};
    std::uint16_t reserved0{0};
    std::uint32_t reserved1{0};

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(GapMarkerPayload) == 24U);
static_assert(alignof(GapMarkerPayload) == 8U);
static_assert(std::is_trivially_copyable_v<GapMarkerPayload>);

/// @brief 16-byte monotonic-to-UTC calibration record.
struct alignas(8) TimeSyncPayload {
    std::uint64_t mono_ns{0};
    std::int64_t utc_offset_ns{0};

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(TimeSyncPayload) == 16U);
static_assert(alignof(TimeSyncPayload) == 8U);
static_assert(std::is_trivially_copyable_v<TimeSyncPayload>);

/// @brief 64-byte footer written at clean segment close.
struct alignas(8) SegmentFooter {
    std::uint32_t magic{kFooterMagic};
    std::uint32_t version{kFormatVersion};
    std::uint64_t total_records{0};
    std::uint64_t total_gaps{0};
    std::uint64_t rec_bytes{0};
    std::uint64_t vid_bytes{0};
    std::uint64_t index_entries{0};
    std::uint64_t end_commit_ns{0};
    std::uint32_t footer_crc32c{0};
    std::uint32_t magic_end{kFooterMagic};

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(SegmentFooter) == 64U);
static_assert(alignof(SegmentFooter) == 8U);
static_assert(std::is_trivially_copyable_v<SegmentFooter>);

// Cold-path JSON for the binary structs. Defined in src/recorder/recorder_format.cpp.
void to_json(nlohmann::json& j, const RecordHeader& value);
void from_json(const nlohmann::json& j, RecordHeader& value);
void to_json(nlohmann::json& j, const IndexEntry& value);
void from_json(const nlohmann::json& j, IndexEntry& value);
void to_json(nlohmann::json& j, const VideoFrameRef& value);
void from_json(const nlohmann::json& j, VideoFrameRef& value);
void to_json(nlohmann::json& j, const VideoStreamConfig& value);
void from_json(const nlohmann::json& j, VideoStreamConfig& value);
void to_json(nlohmann::json& j, const GapMarkerPayload& value);
void from_json(const nlohmann::json& j, GapMarkerPayload& value);
void to_json(nlohmann::json& j, const TimeSyncPayload& value);
void from_json(const nlohmann::json& j, TimeSyncPayload& value);
void to_json(nlohmann::json& j, const SegmentFooter& value);
void from_json(const nlohmann::json& j, SegmentFooter& value);

} // namespace nexigen::recorder
