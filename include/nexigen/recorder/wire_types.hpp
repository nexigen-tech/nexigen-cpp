/// @file wire_types.hpp
/// @brief Fixed-size binary wire structs of the raw session format.
///
/// Fixed-size, little-endian, trivially copyable records shared by the recorder
/// writer, reader, and converter. Every struct is pinned by `static_assert`, so
/// a field reorder or size drift fails the build instead of corrupting files.
///
/// @note Spec `recorder/raw-storage`, requirements "Record framing and CRC-32C",
///       "Sparse timestamp index", "Session manifest and segment triplet",
///       "Time and sequence mapping", and "Crash recovery"; spec
///       `recorder/video-storage`, requirements "`.nxvid` blob layout",
///       "Video frame references", "Video stream declarations", and
///       "Video gap reporting".
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/domain_coefficient.hpp>
#include <nexigen/chrono/service_flags.hpp>
#include <nexigen/chrono/sync_mode.hpp>
#include <nexigen/chrono/time_base_status.hpp>
#include <nexigen/chrono/time_snapshot.hpp>

namespace nexigen::recorder {

/// @brief On-disk format major version written into the manifest and footer.
inline constexpr std::uint16_t kFormatVersion = 2;
/// @brief Magic at the start of every `.nxrec` record header: "RCD1".
inline constexpr std::uint32_t kRecordMagic = 0x52434431U; // "RCD1"
/// @brief Magic at both ends of a clean `SegmentFooter`: "FTR1".
inline constexpr std::uint32_t kFooterMagic = 0x46545231U; // "FTR1"
/// @brief Alignment of every `.nxvid` file and every frame offset.
/// @note Spec `recorder/video-storage`, scenario "Every frame offset is aligned".
inline constexpr std::size_t kVideoAlignment = 4096U;
/// @brief Maximum embedded schema descriptor size (256 KiB).
/// @note Spec `recorder/raw-storage`, requirement "Embedded schema and channel
///       registry".
inline constexpr std::size_t kMaxSchemaBytes = 256U * 1024U;
/// @brief Maximum number of declared channels in one session.
inline constexpr std::size_t kMaxChannels = 64U;
/// @brief Maximum number of declared schemas in one session.
inline constexpr std::size_t kMaxSchemas = 64U;
/// @brief Capacity of `VideoStreamConfig::extradata`.
/// @note Spec `recorder/video-storage`, requirement "Self-contained video codec
///       configuration".
inline constexpr std::size_t kVideoExtradataBytes = 1024U;

/// @brief 48-byte prefix of every `.nxrec` record.
///
/// The header carries the record type, channel, payload size, commit time,
/// source sequence, publisher identity, and both CRC-32C values. The writer
/// computes the header CRC over all fields before `header_crc32c`; the payload
/// CRC covers the bytes that follow the header.
///
/// @note Spec `recorder/raw-storage`, requirement "Record framing and CRC-32C";
///       the reader rejects a record when either CRC does not match.
struct alignas(8) RecordHeader {
    std::uint32_t magic{kRecordMagic}; ///< `kRecordMagic` ("RCD1").
    std::uint16_t type{0};             ///< `RecordType` tag.
    std::uint16_t flags{0};            ///< Reserved record flags; zero in v1.
    std::uint32_t channel_id{0};       ///< Declared channel id; 0 for metadata.
    std::uint32_t payload_size{0};     ///< Payload bytes following the header.
    std::uint64_t commit_mono_ns{0};   ///< Writer commit time, monotonic ns.
    std::uint64_t sequence{0};         ///< Publisher sequence for this record.
    std::uint64_t publisher_id{0};     ///< Publisher identity/epoch source.
    std::uint32_t header_crc32c{0};    ///< CRC-32C over bytes before this field.
    std::uint32_t payload_crc32c{0};   ///< CRC-32C over the payload bytes.

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable header summary.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(RecordHeader) == 48U);
static_assert(alignof(RecordHeader) == 8U);
static_assert(std::is_trivially_copyable_v<RecordHeader>);

/// @brief 64-byte sparse index entry in `.nxidx`.
///
/// One entry bounds the `.nxrec` byte range `[rec_offset, end_rec_offset)` and
/// the min/max event time of the records inside it. `channel_mask` has bit
/// `channel_id` set for every non-zero channel observed in the interval.
///
/// @note Spec `recorder/raw-storage`, requirement "Sparse timestamp index";
///       entries are emitted by the configured byte stride or time interval and
///       can be rebuilt from `.nxrec` after a crash.
struct alignas(8) IndexEntry {
    std::uint64_t commit_ns{0};      ///< Commit time of the interval's first record.
    std::uint64_t rec_offset{0};     ///< First record's byte offset in `.nxrec`.
    std::uint64_t min_event_ns{0};   ///< Minimum event time in the interval.
    std::uint64_t max_event_ns{0};   ///< Maximum event time in the interval.
    std::uint64_t channel_mask{0};   ///< Bit `id` for every channel `id` in 1..63.
    std::uint64_t end_rec_offset{0}; ///< One-past-last byte offset of the interval.
    std::uint32_t record_count{0};   ///< Records covered by the interval.
    std::uint32_t flags{0};          ///< Reserved flags; zero in v1.
    std::uint32_t entry_crc32c{0};   ///< CRC-32C over bytes before this field.
    std::uint32_t reserved{0};       ///< Reserved; zero in v1.

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable index-entry summary.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(IndexEntry) == 64U);
static_assert(alignof(IndexEntry) == 8U);
static_assert(std::is_trivially_copyable_v<IndexEntry>);

/// @brief 64-byte reference from `.nxrec` into `.nxvid`.
///
/// The reference names the video segment sequence, aligned byte offset, byte
/// length, optional CRC, codec/bitstream/dimensions, capture time, source
/// sequence, and configuration epoch. Storage never renumbers frames.
///
/// @note Spec `recorder/video-storage`, requirements "Video frame references",
///       "Optional per-frame CRC", and "Capture time and source sequence
///       preservation".
struct alignas(8) VideoFrameRef {
    std::uint64_t vid_offset{0};      ///< 4 KiB-aligned offset in `.nxvid`.
    std::uint64_t capture_mono_ns{0}; ///< Encoder capture time, monotonic ns.
    std::uint64_t source_sequence{0}; ///< Encoder sequence, preserved verbatim.
    std::uint64_t config_epoch{0};    ///< Encoder configuration epoch.
    std::uint32_t vid_size{0};        ///< Frame length in bytes.
    std::uint32_t vid_crc32c{0};      ///< CRC-32C of the frame, or 0 when disabled.
    std::uint32_t width{0};           ///< Frame width in pixels.
    std::uint32_t height{0};          ///< Frame height in pixels.
    std::uint16_t codec{0};           ///< `VideoCodec` tag.
    std::uint16_t bitstream{0};       ///< `BitstreamFormat` tag.
    std::uint32_t vid_segment_seq{0}; ///< Manifest sequence of the `.nxvid` segment.
    std::uint64_t reserved{0};        ///< Reserved; zero in v1.

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable frame-reference summary.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(VideoFrameRef) == 64U);
static_assert(alignof(VideoFrameRef) == 8U);
static_assert(std::is_trivially_copyable_v<VideoFrameRef>);

/// @brief 1056-byte video stream declaration with bounded extradata.
///
/// Everything needed to interpret or remux the stream travels with the session:
/// codec, framing, dimensions, timebase, configuration epoch, and codec
/// extradata. The recorder never decodes, re-encodes, or parses the bitstream.
///
/// @note Spec `recorder/video-storage`, requirements "Video stream declarations"
///       and "Self-contained video codec configuration".
struct alignas(8) VideoStreamConfig {
    std::uint16_t codec{0};                                     ///< `VideoCodec` tag.
    std::uint16_t bitstream{0};                                 ///< `BitstreamFormat` tag.
    std::uint32_t width{0};                                     ///< Coded width in pixels.
    std::uint32_t height{0};                                    ///< Coded height in pixels.
    std::uint32_t timebase_num{1};                              ///< Timebase numerator (default 1).
    std::uint32_t timebase_den{1'000'000'000};                  ///< Timebase denominator (ns).
    std::uint16_t extradata_size{0};                            ///< Valid bytes in `extradata`.
    std::uint16_t reserved0{0};                                 ///< Reserved; zero in v1.
    std::uint64_t config_epoch{0};                              ///< Encoder configuration epoch.
    std::array<std::uint8_t, kVideoExtradataBytes> extradata{}; ///< Codec extradata.

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable stream-configuration summary.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(VideoStreamConfig) == 1056U);
static_assert(alignof(VideoStreamConfig) == 8U);
static_assert(std::is_trivially_copyable_v<VideoStreamConfig>);

/// @brief 24-byte gap marker payload.
///
/// Emitted when a publisher sequence discontinuity is detected within one
/// `config_epoch`. `first_lost_sequence` plus `lost_count` identify the missing
/// range without storing the missing frames.
///
/// @note Spec `recorder/video-storage`, requirement "Video gap reporting"; the
///       reason tag is a `GapReason` value.
struct alignas(8) GapMarkerPayload {
    std::uint64_t first_lost_sequence{0}; ///< First sequence that was not stored.
    std::uint64_t lost_count{0};          ///< Number of missing sequences.
    std::uint16_t reason{0};              ///< `GapReason` tag.
    std::uint16_t reserved0{0};           ///< Reserved; zero in v1.
    std::uint32_t reserved1{0};           ///< Reserved; zero in v1.

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable gap-marker summary.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(GapMarkerPayload) == 24U);
static_assert(alignof(GapMarkerPayload) == 8U);
static_assert(std::is_trivially_copyable_v<GapMarkerPayload>);

/// @brief 16-byte monotonic-to-UTC calibration record.
///
/// `utc_offset_ns` is `utc_epoch_ns - mono_ns` at the instant the record was
/// written. The converter derives UTC timestamps from the embedded calibration
/// and never reads the local clock.
///
/// @note Spec `recorder/raw-storage`, requirement "Time and sequence mapping";
///       spec `recorder/export`, scenario "UTC timestamps".
struct alignas(8) TimeSyncPayload {
    std::uint64_t mono_ns{0};      ///< Monotonic instant of the calibration.
    std::int64_t utc_offset_ns{0}; ///< Signed offset added to monotonic ns.

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable calibration summary.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(TimeSyncPayload) == 16U);
static_assert(alignof(TimeSyncPayload) == 8U);
static_assert(std::is_trivially_copyable_v<TimeSyncPayload>);

/// @brief Maximum number of domain rows in one `TimeBasePayload`.
inline constexpr std::size_t kTimeBaseMaxRows = 32U;
/// @brief Layout magic of the current `TimeBasePayload` header: "TBP2".
///
/// The retired pre-contract payload shared the same 4224-byte size and record
/// type, so a session written by the old runtime cannot be told apart by shape.
/// This guard makes such a payload detectable: a `TimeBase` record either
/// carries this magic or is treated as a legacy entry.
/// @note Spec `recorder/raw-storage`, requirement "Time-base history
///       persistence"; design D-I.
inline constexpr std::uint32_t kTimeBasePayloadLayout = 0x32504254U; // "TBP2"

/// @brief One published time-base change with the full domain table.
///
/// Mirrors the shared-memory block's header and row layout so a session can be
/// resolved on another machine without the original daemon. `generation` is the
/// daemon's seqlock generation, `boot_token`/`boot_uuid` bind the entry to a
/// kernel boot, `time_base_epoch` binds it to one daemon lifetime, and `gap`
/// marks an interval where no base was available. Rows reuse
/// `chrono::DomainCoefficient` verbatim, including the hub and native validity
/// intervals; a `TimeSnapshot` record carries the 1 Hz values for the same
/// generation.
///
/// Wire layout (4224 bytes, `alignas(64)`):
///
/// ```text
/// offset  size  field
/// 0       8     generation
/// 8       8     time_base_epoch
/// 16      8     boot_token
/// 24      4     layout (kTimeBasePayloadLayout)
/// 28      1     status (chrono::TimeBaseStatus)
/// 29      1     sync_mode (chrono::SyncMode)
/// 30      1     gap
/// 31      1     reserved
/// 32      4     service_flags (chrono::ServiceFlags)
/// 36      4     reserved
/// 40      8     tai_offset_ns (TAI - UTC)
/// 48      8     utc_offset_ns (hub -> UTC)
/// 56      8     utc_unc_ns
/// 64      2     domain_count
/// 66      2     reserved
/// 68      37    boot_uuid
/// 105     23    reserved
/// 128     4096  rows[32] (chrono::DomainCoefficient)
/// ```
///
/// @note Spec `recorder/raw-storage`, requirement "Time-base history
///       persistence"; design D-G and D-I.
struct alignas(8) TimeBasePayload {
    std::uint64_t generation{0};                  ///< Daemon publication generation.
    std::uint64_t time_base_epoch{0};             ///< Daemon lifetime epoch binding.
    std::uint64_t boot_token{0};                  ///< 64-bit boot token.
    std::uint32_t layout{kTimeBasePayloadLayout}; ///< `kTimeBasePayloadLayout` guard.
    chrono::TimeBaseStatus status{chrono::TimeBaseStatus::NotReady}; ///< Aggregate status.
    chrono::SyncMode sync_mode{chrono::SyncMode::Standalone};        ///< Active sync mode.
    std::uint8_t gap{0};                      ///< 1 when this entry marks a gap.
    std::uint8_t reserved0{0};                ///< Reserved; zero in this version.
    chrono::ServiceFlags service_flags{};     ///< Observed service flags.
    std::uint32_t reserved1{0};               ///< Reserved; zero in this version.
    std::int64_t tai_offset_ns{0};            ///< TAI - UTC offset.
    std::int64_t utc_offset_ns{0};            ///< Hub -> UTC offset.
    std::uint64_t utc_unc_ns{0};              ///< UTC offset uncertainty.
    std::uint16_t domain_count{0};            ///< Valid rows in `rows`.
    std::uint16_t reserved2{0};               ///< Reserved; zero in this version.
    std::array<char, 37> boot_uuid{};         ///< Full kernel boot UUID.
    std::array<std::uint8_t, 23> reserved3{}; ///< Reserved; zero in this version.
    std::array<chrono::DomainCoefficient, kTimeBaseMaxRows> rows{}; ///< Domain table.

    /// @brief Borrow the boot UUID as a view.
    /// @return Text up to the first NUL in `boot_uuid`.
    [[nodiscard]] std::string_view boot_uuid_view() const noexcept;

    /// @brief Whether this entry uses the current (post-contract) layout.
    /// @return `true` when the magic and the header enumerations are in range.
    /// @note A legacy entry is rejected instead of being reinterpreted.
    [[nodiscard]] bool has_current_layout() const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable entry summary.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(TimeBasePayload) == 4224U, "time-base payload is a wire constant");
static_assert(alignof(TimeBasePayload) == 64U, "time-base payload starts on a cache line");
static_assert(offsetof(TimeBasePayload, generation) == 0U, "generation offset is a wire constant");
static_assert(offsetof(TimeBasePayload, time_base_epoch) == 8U,
              "time-base epoch offset is a wire constant");
static_assert(offsetof(TimeBasePayload, boot_token) == 16U, "boot token offset is a wire constant");
static_assert(offsetof(TimeBasePayload, layout) == 24U, "layout offset is a wire constant");
static_assert(offsetof(TimeBasePayload, status) == 28U, "status offset is a wire constant");
static_assert(offsetof(TimeBasePayload, sync_mode) == 29U, "sync mode offset is a wire constant");
static_assert(offsetof(TimeBasePayload, gap) == 30U, "gap offset is a wire constant");
static_assert(offsetof(TimeBasePayload, service_flags) == 32U,
              "service flags offset is a wire constant");
static_assert(offsetof(TimeBasePayload, tai_offset_ns) == 40U,
              "TAI offset offset is a wire constant");
static_assert(offsetof(TimeBasePayload, utc_offset_ns) == 48U,
              "UTC offset offset is a wire constant");
static_assert(offsetof(TimeBasePayload, utc_unc_ns) == 56U,
              "UTC uncertainty offset is a wire constant");
static_assert(offsetof(TimeBasePayload, domain_count) == 64U,
              "domain count offset is a wire constant");
static_assert(offsetof(TimeBasePayload, boot_uuid) == 68U, "boot UUID offset is a wire constant");
static_assert(offsetof(TimeBasePayload, rows) == 128U, "row table offset is a wire constant");
static_assert(sizeof(TimeBasePayload::status) == 1U, "status is one byte on the wire");
static_assert(sizeof(TimeBasePayload::sync_mode) == 1U, "sync mode is one byte on the wire");
static_assert(sizeof(TimeBasePayload::service_flags) == 4U,
              "service flags are four bytes on the wire");
static_assert(std::is_trivially_copyable_v<TimeBasePayload>);
static_assert(std::is_trivially_copyable_v<chrono::DomainCoefficient>);
static_assert(std::is_trivially_copyable_v<chrono::TimeSnapshot>);

/// @brief 64-byte footer written at clean segment close.
///
/// The footer carries the record/gap/byte counters and a CRC over its own
/// metadata. Its presence is the definition of a closed triplet; a missing or
/// truncated footer is treated as crash recovery.
///
/// @note Spec `recorder/raw-storage`, requirements "Crash recovery" and
///       "Session manifest and segment triplet"; spec `recorder/video-storage`,
///       scenario "Footer remains covered".
struct alignas(8) SegmentFooter {
    std::uint32_t magic{kFooterMagic};     ///< `kFooterMagic` ("FTR1") at the start.
    std::uint32_t version{kFormatVersion}; ///< On-disk format version.
    std::uint64_t total_records{0};        ///< Records written in the segment.
    std::uint64_t total_gaps{0};           ///< Gap-marker records written.
    std::uint64_t rec_bytes{0};            ///< Bytes in `.nxrec`, excluding footer.
    std::uint64_t vid_bytes{0};            ///< Bytes in `.nxvid`.
    std::uint64_t index_entries{0};        ///< `.nxidx` entries written.
    std::uint64_t end_commit_ns{0};        ///< Commit time of the last record.
    std::uint32_t footer_crc32c{0};        ///< CRC-32C over bytes before this field.
    std::uint32_t magic_end{kFooterMagic}; ///< `kFooterMagic` ("FTR1") at the end.

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable footer summary.
    [[nodiscard]] std::string to_string() const;
};
static_assert(sizeof(SegmentFooter) == 64U);
static_assert(alignof(SegmentFooter) == 8U);
static_assert(std::is_trivially_copyable_v<SegmentFooter>);

/// @brief Cold-path JSON serialization for the binary wire structs.
///
/// These conversions exist for diagnostics, tests, and `session.json`-adjacent
/// tooling; the on-disk `.nxrec`/`.nxidx`/`.nxvid` representation is the binary
/// layout above. They are defined in `src/recorder/recorder_format.cpp`.
/// @note Spec `recorder/raw-storage`, requirement "Storage type serialization".
/// @{

/// @brief Serialize a record header to diagnostic JSON.
/// @param j Destination JSON object.
/// @param value Header to serialize.
void to_json(nlohmann::json& j, const RecordHeader& value);

/// @brief Parse a record header from diagnostic JSON.
/// @param j Source JSON object.
/// @param value Destination header.
void from_json(const nlohmann::json& j, RecordHeader& value);

/// @brief Serialize an index entry to diagnostic JSON.
/// @param j Destination JSON object.
/// @param value Index entry to serialize.
void to_json(nlohmann::json& j, const IndexEntry& value);

/// @brief Parse an index entry from diagnostic JSON.
/// @param j Source JSON object.
/// @param value Destination index entry.
void from_json(const nlohmann::json& j, IndexEntry& value);

/// @brief Serialize a video frame reference to diagnostic JSON.
/// @param j Destination JSON object.
/// @param value Reference to serialize.
void to_json(nlohmann::json& j, const VideoFrameRef& value);

/// @brief Parse a video frame reference from diagnostic JSON.
/// @param j Source JSON object.
/// @param value Destination reference.
void from_json(const nlohmann::json& j, VideoFrameRef& value);

/// @brief Serialize a video stream configuration to diagnostic JSON.
/// @param j Destination JSON object.
/// @param value Configuration to serialize.
void to_json(nlohmann::json& j, const VideoStreamConfig& value);

/// @brief Parse a video stream configuration from diagnostic JSON.
/// @param j Source JSON object.
/// @param value Destination configuration.
void from_json(const nlohmann::json& j, VideoStreamConfig& value);

/// @brief Serialize a gap-marker payload to diagnostic JSON.
/// @param j Destination JSON object.
/// @param value Gap marker to serialize.
void to_json(nlohmann::json& j, const GapMarkerPayload& value);

/// @brief Parse a gap-marker payload from diagnostic JSON.
/// @param j Source JSON object.
/// @param value Destination gap marker.
void from_json(const nlohmann::json& j, GapMarkerPayload& value);

/// @brief Serialize a time-calibration payload to diagnostic JSON.
/// @param j Destination JSON object.
/// @param value Calibration to serialize.
void to_json(nlohmann::json& j, const TimeSyncPayload& value);

/// @brief Parse a time-calibration payload from diagnostic JSON.
/// @param j Source JSON object.
/// @param value Destination calibration.
void from_json(const nlohmann::json& j, TimeSyncPayload& value);

/// @brief Serialize a time-base history entry to diagnostic JSON.
/// @param j Destination JSON object.
/// @param value Entry to serialize.
void to_json(nlohmann::json& j, const TimeBasePayload& value);

/// @brief Parse a time-base history entry from diagnostic JSON.
/// @param j Source JSON object.
/// @param value Destination entry.
void from_json(const nlohmann::json& j, TimeBasePayload& value) noexcept;

/// @brief Serialize a segment footer to diagnostic JSON.
/// @param j Destination JSON object.
/// @param value Footer to serialize.
void to_json(nlohmann::json& j, const SegmentFooter& value);

/// @brief Parse a segment footer from diagnostic JSON.
/// @param j Source JSON object.
/// @param value Destination footer.
void from_json(const nlohmann::json& j, SegmentFooter& value);

/// @}

} // namespace nexigen::recorder
