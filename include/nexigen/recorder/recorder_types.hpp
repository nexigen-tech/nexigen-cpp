/// @file recorder_types.hpp
/// @brief Public storage vocabulary shared by the raw recorder and the converter.
///
/// Cohesive ABI/configuration vocabulary for the raw recorder and the on-demand
/// converter. Every enum supplies constexpr `to_string`/`from_string`,
/// non-throwing ADL JSON, and the project-wide `std::formatter` delegate, so the
/// same spellings travel through `session.json`, `.nxrec` declarations, the
/// converter CLI, and logs.
///
/// @note Spec `recorder/raw-storage`, requirement "Storage type serialization";
///       spec `recorder/video-storage`, requirement "Video storage type
///       serialization"; spec `recorder/export`, requirement "Export type
///       serialization".
/// @note The spellings are part of the on-disk contract: changing an enumerator
///       name requires a format-version change and a migration.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::recorder {

/// @brief Storage format tag recorded in `session.json`.
/// @note Spec `recorder/raw-storage`, requirement "Session manifest and segment
///       triplet"; revision 1 defines only the raw triplet format.
enum class RecorderFormat : std::uint8_t {
    Raw, ///< `.nxrec`/`.nxidx`/`.nxvid` triplet with a JSON manifest.
};

/// @brief Time domain of the session's event and commit timestamps.
/// @note Spec `recorder/raw-storage`, requirement "Time and sequence mapping";
///       `MonotonicRaw` is the default and `Utc` is derived through `TimeSync`.
enum class TimeDomain : std::uint8_t {
    MonotonicRaw, ///< `CLOCK_MONOTONIC_RAW` nanoseconds.
    Utc,          ///< UTC epoch nanoseconds derived from a calibration record.
};

/// @brief Kind of a `.nxrec` record, stored in `RecordHeader::type`.
/// @note Spec `recorder/raw-storage`, requirements "Record framing and CRC-32C"
///       and "Time and sequence mapping"; declarations carry no payload.
enum class RecordType : std::uint16_t {
    ChannelDeclare = 1,    ///< `ChannelDeclare` JSON payload.
    SchemaDeclare = 2,     ///< `SchemaDeclare` JSON payload.
    LogFrame = 3,          ///< `nexigen.logging::LogFrame` bytes.
    MetricFrame = 4,       ///< `nexigen.metrics::MetricFrame` bytes.
    SpanFrame = 5,         ///< `nexigen.tracing::SpanFrame` bytes.
    ComPayload = 6,        ///< Opaque/telemetry payload bytes.
    VideoFrameRef = 7,     ///< `VideoFrameRef` pointing into `.nxvid`.
    VideoStreamConfig = 8, ///< `VideoStreamConfig` codec declaration.
    GapMarker = 9,         ///< `GapMarkerPayload` for a sequence discontinuity.
    TimeSync = 10,         ///< `TimeSyncPayload` monotonic-to-UTC calibration.
    TimeBase = 11,         ///< `TimeBasePayload` published domain-history entry.
    TimeSnapshot = 12,     ///< `chrono::TimeSnapshot` 1 Hz service snapshot.
};

/// @brief Reason attached to a `GapMarker` record.
/// @note Spec `recorder/raw-storage`, requirement "Time and sequence mapping";
///       spec `recorder/video-storage`, requirement "Video gap reporting".
enum class GapReason : std::uint16_t {
    Unknown = 0,             ///< No specific reason was supplied.
    IoxRingOverflow = 1,     ///< The iceoryx2 receive ring overflowed.
    EncoderOverload = 2,     ///< The encoder could not keep up with the source.
    IoBackpressure = 3,      ///< A producer shard was full and dropped records.
    StreamDiscontinuity = 4, ///< The source sequence skipped frames.
};

/// @brief Codec tag of a stored video stream.
/// @note Spec `recorder/video-storage`, requirements "`.nxvid` blob layout" and
///       "Self-contained video codec configuration"; the recorder never decodes
///       or re-encodes the bytes.
enum class VideoCodec : std::uint16_t {
    Unknown = 0,    ///< No codec was declared; extraction must reject it.
    RawRgb24 = 1,   ///< Uncompressed RGB24.
    RawBgr24 = 2,   ///< Uncompressed BGR24.
    RawNv12 = 3,    ///< Uncompressed NV12.
    RawYuv420p = 4, ///< Uncompressed YUV 4:2:0 planar.
    RawBayer8 = 5,  ///< Uncompressed 8-bit Bayer mosaic.
    JpegXs = 10,    ///< JPEG XS compressed stream.
    H264 = 11,      ///< H.264/AVC elementary stream.
    H265 = 12,      ///< H.265/HEVC elementary stream.
    Av1 = 13,       ///< AV1 OBU stream.
};

/// @brief Bitstream framing of a stored video codec.
/// @note Spec `recorder/video-storage`, requirement "Self-contained video codec
///       configuration"; the tag travels with the stream declaration.
enum class BitstreamFormat : std::uint16_t {
    Unknown = 0, ///< No framing was declared; extraction must reject it.
    AnnexB = 1,  ///< Annex B start-code framing.
    Avcc = 2,    ///< Length-prefixed (AVCC/HVCC) framing.
    Obu = 3,     ///< AV1 OBU framing.
    Raw = 4,     ///< Unframed/raw codec or uncompressed bytes.
};

/// @brief Whether `.nxvid` frames carry a per-frame CRC-32C.
/// @note Spec `recorder/video-storage`, requirement "Optional per-frame CRC";
///       `Off` is the default and the benchmarked fast path.
enum class VideoCrcMode : std::uint8_t {
    Off,      ///< No per-frame CRC; the footer CRC still covers metadata.
    PerFrame, ///< `VideoFrameRef::vid_crc32c` covers the exact frame bytes.
};

/// @brief Semantic kind of a declared channel.
/// @note Spec `recorder/export`, requirements "Dataset inventory" and
///       "Schema-driven decoding"; the kind selects the decoder and CLI view.
enum class DatasetKind : std::uint8_t {
    Telemetry, ///< Structured telemetry decoded from an embedded schema.
    Opaque,    ///< Producer-encoded bytes passed through with their encoding.
    Video,     ///< `VideoFrameRef` records pointing into `.nxvid`.
};

/// @brief Policy applied when `retention_max_bytes` is exceeded.
/// @note Spec `recorder/raw-storage`, requirement "Rotation and retention";
///       circular deletes only closed triplets, stop finalizes and halts.
enum class RetentionPolicy : std::uint8_t {
    Circular, ///< Delete the oldest closed triplets until the limit is met.
    Stop,     ///< Finalize the active segment and stop accepting data.
};

/// @brief Output representation requested from `nexigen-export`.
/// @note Spec `recorder/export`, requirements "JSONL and Perfetto output",
///       "MCAP output", and "Video extraction to elementary streams".
enum class ExportFormat : std::uint8_t {
    Jsonl,      ///< Newline-delimited JSON records.
    Mcap,       ///< MCAP container with schemas/channels/messages.
    Perfetto,   ///< Chrome/Perfetto trace JSON for spans.
    Elementary, ///< Codec-native video stream plus timestamp sidecar.
};

/// @brief Video container requested from `nexigen-export`.
/// @note Spec `recorder/export`, requirement "Video extraction to elementary
///       streams"; `Mp4`/`Mkv` are deferred until `nexigen_media` exists and
///       fail with `UnsupportedFormat` in this revision.
enum class VideoExportFormat : std::uint8_t {
    Elementary, ///< Codec-native stream and JSONL timestamps sidecar.
    Mp4,        ///< Deferred container remux.
    Mkv,        ///< Deferred container remux.
};

/// @brief Spec-facing alias for the video codec tag vocabulary.
using VideoCodecTag = VideoCodec;
/// @brief Spec-facing alias for the video gap-reason vocabulary.
using VideoGapReason = GapReason;

/// @brief Generate non-throwing ADL `to_json`/`from_json` for a recorder enum.
/// @note Every recorder enum is serialized by its canonical PascalCase spelling,
///       matching the on-disk manifest and declaration records.
#define NEXIGEN_RECORDER_ENUM_JSON(Type)                                                           \
    inline void to_json(nlohmann::json& j, Type value) {                                           \
        core::mandate::enum_to_json(j, value);                                                     \
    }                                                                                              \
    inline void from_json(const nlohmann::json& j, Type& value) noexcept {                         \
        core::mandate::enum_from_json(j, value);                                                   \
    }

NEXIGEN_RECORDER_ENUM_JSON(RecorderFormat)
NEXIGEN_RECORDER_ENUM_JSON(TimeDomain)
NEXIGEN_RECORDER_ENUM_JSON(RecordType)
NEXIGEN_RECORDER_ENUM_JSON(GapReason)
NEXIGEN_RECORDER_ENUM_JSON(VideoCodec)
NEXIGEN_RECORDER_ENUM_JSON(BitstreamFormat)
NEXIGEN_RECORDER_ENUM_JSON(VideoCrcMode)
NEXIGEN_RECORDER_ENUM_JSON(DatasetKind)
NEXIGEN_RECORDER_ENUM_JSON(RetentionPolicy)
NEXIGEN_RECORDER_ENUM_JSON(ExportFormat)
NEXIGEN_RECORDER_ENUM_JSON(VideoExportFormat)

#undef NEXIGEN_RECORDER_ENUM_JSON

} // namespace nexigen::recorder

namespace nexigen::core {

/// @brief Canonical name table for a recorder enum (`core::enum_name`/`enum_value`).
/// @note The entries fix the PascalCase wire spelling used by `to_string`,
///       `from_string`, JSON, and the on-disk manifest.
template <> struct EnumTraits<nexigen::recorder::RecorderFormat> {
    /// @brief One canonical spelling row.
    struct Entry {
        nexigen::recorder::RecorderFormat value; ///< Enum value.
        std::string_view name;                   ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical spelling table.
    static constexpr std::array<Entry, 1> entries{
        {{nexigen::recorder::RecorderFormat::Raw, "Raw"}}};
};

/// @brief Canonical name table for a recorder enum (`core::enum_name`/`enum_value`).
/// @note The entries fix the PascalCase wire spelling used by `to_string`,
///       `from_string`, JSON, and the on-disk manifest.
template <> struct EnumTraits<nexigen::recorder::TimeDomain> {
    /// @brief One canonical spelling row.
    struct Entry {
        nexigen::recorder::TimeDomain value; ///< Enum value.
        std::string_view name;               ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical spelling table.
    static constexpr std::array<Entry, 2> entries{
        {{nexigen::recorder::TimeDomain::MonotonicRaw, "MonotonicRaw"},
         {nexigen::recorder::TimeDomain::Utc, "Utc"}}};
};

/// @brief Canonical name table for a recorder enum (`core::enum_name`/`enum_value`).
/// @note The entries fix the PascalCase wire spelling used by `to_string`,
///       `from_string`, JSON, and the on-disk manifest.
template <> struct EnumTraits<nexigen::recorder::RecordType> {
    /// @brief One canonical spelling row.
    struct Entry {
        nexigen::recorder::RecordType value; ///< Enum value.
        std::string_view name;               ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical spelling table.
    static constexpr std::array<Entry, 12> entries{
        {{nexigen::recorder::RecordType::ChannelDeclare, "ChannelDeclare"},
         {nexigen::recorder::RecordType::SchemaDeclare, "SchemaDeclare"},
         {nexigen::recorder::RecordType::LogFrame, "LogFrame"},
         {nexigen::recorder::RecordType::MetricFrame, "MetricFrame"},
         {nexigen::recorder::RecordType::SpanFrame, "SpanFrame"},
         {nexigen::recorder::RecordType::ComPayload, "ComPayload"},
         {nexigen::recorder::RecordType::VideoFrameRef, "VideoFrameRef"},
         {nexigen::recorder::RecordType::VideoStreamConfig, "VideoStreamConfig"},
         {nexigen::recorder::RecordType::GapMarker, "GapMarker"},
         {nexigen::recorder::RecordType::TimeSync, "TimeSync"},
         {nexigen::recorder::RecordType::TimeBase, "TimeBase"},
         {nexigen::recorder::RecordType::TimeSnapshot, "TimeSnapshot"}}};
};

/// @brief Canonical name table for a recorder enum (`core::enum_name`/`enum_value`).
/// @note The entries fix the PascalCase wire spelling used by `to_string`,
///       `from_string`, JSON, and the on-disk manifest.
template <> struct EnumTraits<nexigen::recorder::GapReason> {
    /// @brief One canonical spelling row.
    struct Entry {
        nexigen::recorder::GapReason value; ///< Enum value.
        std::string_view name;              ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical spelling table.
    static constexpr std::array<Entry, 5> entries{
        {{nexigen::recorder::GapReason::Unknown, "Unknown"},
         {nexigen::recorder::GapReason::IoxRingOverflow, "IoxRingOverflow"},
         {nexigen::recorder::GapReason::EncoderOverload, "EncoderOverload"},
         {nexigen::recorder::GapReason::IoBackpressure, "IoBackpressure"},
         {nexigen::recorder::GapReason::StreamDiscontinuity, "StreamDiscontinuity"}}};
};

/// @brief Canonical name table for a recorder enum (`core::enum_name`/`enum_value`).
/// @note The entries fix the PascalCase wire spelling used by `to_string`,
///       `from_string`, JSON, and the on-disk manifest.
template <> struct EnumTraits<nexigen::recorder::VideoCodec> {
    /// @brief One canonical spelling row.
    struct Entry {
        nexigen::recorder::VideoCodec value; ///< Enum value.
        std::string_view name;               ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical spelling table.
    static constexpr std::array<Entry, 10> entries{
        {{nexigen::recorder::VideoCodec::Unknown, "Unknown"},
         {nexigen::recorder::VideoCodec::RawRgb24, "RawRgb24"},
         {nexigen::recorder::VideoCodec::RawBgr24, "RawBgr24"},
         {nexigen::recorder::VideoCodec::RawNv12, "RawNv12"},
         {nexigen::recorder::VideoCodec::RawYuv420p, "RawYuv420p"},
         {nexigen::recorder::VideoCodec::RawBayer8, "RawBayer8"},
         {nexigen::recorder::VideoCodec::JpegXs, "JpegXs"},
         {nexigen::recorder::VideoCodec::H264, "H264"},
         {nexigen::recorder::VideoCodec::H265, "H265"},
         {nexigen::recorder::VideoCodec::Av1, "Av1"}}};
};

/// @brief Canonical name table for a recorder enum (`core::enum_name`/`enum_value`).
/// @note The entries fix the PascalCase wire spelling used by `to_string`,
///       `from_string`, JSON, and the on-disk manifest.
template <> struct EnumTraits<nexigen::recorder::BitstreamFormat> {
    /// @brief One canonical spelling row.
    struct Entry {
        nexigen::recorder::BitstreamFormat value; ///< Enum value.
        std::string_view name;                    ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical spelling table.
    static constexpr std::array<Entry, 5> entries{
        {{nexigen::recorder::BitstreamFormat::Unknown, "Unknown"},
         {nexigen::recorder::BitstreamFormat::AnnexB, "AnnexB"},
         {nexigen::recorder::BitstreamFormat::Avcc, "Avcc"},
         {nexigen::recorder::BitstreamFormat::Obu, "Obu"},
         {nexigen::recorder::BitstreamFormat::Raw, "Raw"}}};
};

/// @brief Canonical name table for a recorder enum (`core::enum_name`/`enum_value`).
/// @note The entries fix the PascalCase wire spelling used by `to_string`,
///       `from_string`, JSON, and the on-disk manifest.
template <> struct EnumTraits<nexigen::recorder::VideoCrcMode> {
    /// @brief One canonical spelling row.
    struct Entry {
        nexigen::recorder::VideoCrcMode value; ///< Enum value.
        std::string_view name;                 ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical spelling table.
    static constexpr std::array<Entry, 2> entries{
        {{nexigen::recorder::VideoCrcMode::Off, "Off"},
         {nexigen::recorder::VideoCrcMode::PerFrame, "PerFrame"}}};
};

/// @brief Canonical name table for a recorder enum (`core::enum_name`/`enum_value`).
/// @note The entries fix the PascalCase wire spelling used by `to_string`,
///       `from_string`, JSON, and the on-disk manifest.
template <> struct EnumTraits<nexigen::recorder::DatasetKind> {
    /// @brief One canonical spelling row.
    struct Entry {
        nexigen::recorder::DatasetKind value; ///< Enum value.
        std::string_view name;                ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical spelling table.
    static constexpr std::array<Entry, 3> entries{
        {{nexigen::recorder::DatasetKind::Telemetry, "Telemetry"},
         {nexigen::recorder::DatasetKind::Opaque, "Opaque"},
         {nexigen::recorder::DatasetKind::Video, "Video"}}};
};

/// @brief Canonical name table for a recorder enum (`core::enum_name`/`enum_value`).
/// @note The entries fix the PascalCase wire spelling used by `to_string`,
///       `from_string`, JSON, and the on-disk manifest.
template <> struct EnumTraits<nexigen::recorder::RetentionPolicy> {
    /// @brief One canonical spelling row.
    struct Entry {
        nexigen::recorder::RetentionPolicy value; ///< Enum value.
        std::string_view name;                    ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical spelling table.
    static constexpr std::array<Entry, 2> entries{
        {{nexigen::recorder::RetentionPolicy::Circular, "Circular"},
         {nexigen::recorder::RetentionPolicy::Stop, "Stop"}}};
};

/// @brief Canonical name table for a recorder enum (`core::enum_name`/`enum_value`).
/// @note The entries fix the PascalCase wire spelling used by `to_string`,
///       `from_string`, JSON, and the on-disk manifest.
template <> struct EnumTraits<nexigen::recorder::ExportFormat> {
    /// @brief One canonical spelling row.
    struct Entry {
        nexigen::recorder::ExportFormat value; ///< Enum value.
        std::string_view name;                 ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical spelling table.
    static constexpr std::array<Entry, 4> entries{
        {{nexigen::recorder::ExportFormat::Jsonl, "Jsonl"},
         {nexigen::recorder::ExportFormat::Mcap, "Mcap"},
         {nexigen::recorder::ExportFormat::Perfetto, "Perfetto"},
         {nexigen::recorder::ExportFormat::Elementary, "Elementary"}}};
};

/// @brief Canonical name table for a recorder enum (`core::enum_name`/`enum_value`).
/// @note The entries fix the PascalCase wire spelling used by `to_string`,
///       `from_string`, JSON, and the on-disk manifest.
template <> struct EnumTraits<nexigen::recorder::VideoExportFormat> {
    /// @brief One canonical spelling row.
    struct Entry {
        nexigen::recorder::VideoExportFormat value; ///< Enum value.
        std::string_view name;                      ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical spelling table.
    static constexpr std::array<Entry, 3> entries{
        {{nexigen::recorder::VideoExportFormat::Elementary, "Elementary"},
         {nexigen::recorder::VideoExportFormat::Mp4, "Mp4"},
         {nexigen::recorder::VideoExportFormat::Mkv, "Mkv"}}};
};

} // namespace nexigen::core

namespace nexigen::recorder {

/// @brief Canonical spelling of a recorder enum value.
/// @param value Enum value.
/// @return Stable PascalCase wire spelling.
/// @note Spec `recorder/raw-storage`, requirement "Storage type serialization".
[[nodiscard]] constexpr std::string_view to_string(RecorderFormat value) noexcept {
    return core::enum_name(value);
}

/// @brief Canonical spelling of a session time domain.
/// @param value Enum value.
/// @return `MonotonicRaw` or `Utc`.
[[nodiscard]] constexpr std::string_view to_string(TimeDomain value) noexcept {
    return core::enum_name(value);
}

/// @brief Canonical spelling of a record type.
/// @param value Enum value.
/// @return Stable PascalCase record-type name.
[[nodiscard]] constexpr std::string_view to_string(RecordType value) noexcept {
    return core::enum_name(value);
}

/// @brief Canonical spelling of a gap reason.
/// @param value Enum value.
/// @return Stable PascalCase gap-reason name.
[[nodiscard]] constexpr std::string_view to_string(GapReason value) noexcept {
    return core::enum_name(value);
}

/// @brief Canonical spelling of a video codec.
/// @param value Enum value.
/// @return Stable PascalCase codec name.
[[nodiscard]] constexpr std::string_view to_string(VideoCodec value) noexcept {
    return core::enum_name(value);
}

/// @brief Canonical spelling of a video bitstream format.
/// @param value Enum value.
/// @return Stable PascalCase bitstream name.
[[nodiscard]] constexpr std::string_view to_string(BitstreamFormat value) noexcept {
    return core::enum_name(value);
}

/// @brief Canonical spelling of a video CRC mode.
/// @param value Enum value.
/// @return `Off` or `PerFrame`.
[[nodiscard]] constexpr std::string_view to_string(VideoCrcMode value) noexcept {
    return core::enum_name(value);
}

/// @brief Canonical spelling of a dataset kind.
/// @param value Enum value.
/// @return `Telemetry`, `Opaque`, or `Video`.
[[nodiscard]] constexpr std::string_view to_string(DatasetKind value) noexcept {
    return core::enum_name(value);
}

/// @brief Canonical spelling of a retention policy.
/// @param value Enum value.
/// @return `Circular` or `Stop`.
[[nodiscard]] constexpr std::string_view to_string(RetentionPolicy value) noexcept {
    return core::enum_name(value);
}

/// @brief Canonical spelling of an export format.
/// @param value Enum value.
/// @return `Jsonl`, `Mcap`, `Perfetto`, or `Elementary`.
[[nodiscard]] constexpr std::string_view to_string(ExportFormat value) noexcept {
    return core::enum_name(value);
}

/// @brief Canonical spelling of a video export format.
/// @param value Enum value.
/// @return `Elementary`, `Mp4`, or `Mkv`.
[[nodiscard]] constexpr std::string_view to_string(VideoExportFormat value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical recorder format spelling.
/// @param text Candidate spelling.
/// @param out Destination, untouched when parsing fails.
/// @return `true` when `text` matched a known spelling.
[[nodiscard]] constexpr bool from_string(std::string_view text, RecorderFormat& out) noexcept {
    return core::enum_value(text, out);
}

/// @brief Parse a canonical time-domain spelling.
/// @param text Candidate spelling.
/// @param out Destination, untouched when parsing fails.
/// @return `true` when `text` matched a known spelling.
[[nodiscard]] constexpr bool from_string(std::string_view text, TimeDomain& out) noexcept {
    return core::enum_value(text, out);
}

/// @brief Parse a canonical record-type spelling.
/// @param text Candidate spelling.
/// @param out Destination, untouched when parsing fails.
/// @return `true` when `text` matched a known spelling.
[[nodiscard]] constexpr bool from_string(std::string_view text, RecordType& out) noexcept {
    return core::enum_value(text, out);
}

/// @brief Parse a canonical gap-reason spelling.
/// @param text Candidate spelling.
/// @param out Destination, untouched when parsing fails.
/// @return `true` when `text` matched a known spelling.
[[nodiscard]] constexpr bool from_string(std::string_view text, GapReason& out) noexcept {
    return core::enum_value(text, out);
}

/// @brief Parse a canonical video-codec spelling.
/// @param text Candidate spelling.
/// @param out Destination, untouched when parsing fails.
/// @return `true` when `text` matched a known spelling.
[[nodiscard]] constexpr bool from_string(std::string_view text, VideoCodec& out) noexcept {
    return core::enum_value(text, out);
}

/// @brief Parse a canonical bitstream-format spelling.
/// @param text Candidate spelling.
/// @param out Destination, untouched when parsing fails.
/// @return `true` when `text` matched a known spelling.
[[nodiscard]] constexpr bool from_string(std::string_view text, BitstreamFormat& out) noexcept {
    return core::enum_value(text, out);
}

/// @brief Parse a canonical video CRC-mode spelling.
/// @param text Candidate spelling.
/// @param out Destination, untouched when parsing fails.
/// @return `true` when `text` matched a known spelling.
[[nodiscard]] constexpr bool from_string(std::string_view text, VideoCrcMode& out) noexcept {
    return core::enum_value(text, out);
}

/// @brief Parse a canonical dataset-kind spelling.
/// @param text Candidate spelling.
/// @param out Destination, untouched when parsing fails.
/// @return `true` when `text` matched a known spelling.
[[nodiscard]] constexpr bool from_string(std::string_view text, DatasetKind& out) noexcept {
    return core::enum_value(text, out);
}

/// @brief Parse a canonical retention-policy spelling.
/// @param text Candidate spelling.
/// @param out Destination, untouched when parsing fails.
/// @return `true` when `text` matched a known spelling.
[[nodiscard]] constexpr bool from_string(std::string_view text, RetentionPolicy& out) noexcept {
    return core::enum_value(text, out);
}

/// @brief Parse a canonical export-format spelling.
/// @param text Candidate spelling.
/// @param out Destination, untouched when parsing fails.
/// @return `true` when `text` matched a known spelling.
[[nodiscard]] constexpr bool from_string(std::string_view text, ExportFormat& out) noexcept {
    return core::enum_value(text, out);
}

/// @brief Parse a canonical video-export-format spelling.
/// @param text Candidate spelling.
/// @param out Destination, untouched when parsing fails.
/// @return `true` when `text` matched a known spelling.
[[nodiscard]] constexpr bool from_string(std::string_view text, VideoExportFormat& out) noexcept {
    return core::enum_value(text, out);
}

} // namespace nexigen::recorder
