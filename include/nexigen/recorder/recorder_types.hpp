// nexigen::recorder — public storage vocabulary (change add-recorder-raw-format).
//
// Cohesive ABI/configuration vocabulary for the raw recorder and the on-demand
// converter. Every enum supplies constexpr to_string/from_string, non-throwing
// ADL JSON, and the project-wide std::formatter delegate.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::recorder {

enum class RecorderFormat : std::uint8_t { Raw };
enum class TimeDomain : std::uint8_t { MonotonicRaw, Utc };
enum class RecordType : std::uint16_t {
    ChannelDeclare = 1,
    SchemaDeclare = 2,
    LogFrame = 3,
    MetricFrame = 4,
    SpanFrame = 5,
    ComPayload = 6,
    VideoFrameRef = 7,
    VideoStreamConfig = 8,
    GapMarker = 9,
    TimeSync = 10,
};
enum class GapReason : std::uint16_t {
    Unknown = 0,
    IoxRingOverflow = 1,
    EncoderOverload = 2,
    IoBackpressure = 3,
    StreamDiscontinuity = 4,
};
enum class VideoCodec : std::uint16_t {
    Unknown = 0,
    RawRgb24 = 1,
    RawBgr24 = 2,
    RawNv12 = 3,
    RawYuv420p = 4,
    RawBayer8 = 5,
    JpegXs = 10,
    H264 = 11,
    H265 = 12,
    Av1 = 13,
};
enum class BitstreamFormat : std::uint16_t {
    Unknown = 0,
    AnnexB = 1,
    Avcc = 2,
    Obu = 3,
    Raw = 4,
};
enum class VideoCrcMode : std::uint8_t { Off, PerFrame };
enum class DatasetKind : std::uint8_t { Telemetry, Opaque, Video };
enum class RetentionPolicy : std::uint8_t { Circular, Stop };
enum class ExportFormat : std::uint8_t { Jsonl, Mcap, Perfetto, Elementary };
enum class VideoExportFormat : std::uint8_t { Elementary, Mp4, Mkv };

/// @brief Spec-facing alias for the video codec tag vocabulary.
using VideoCodecTag = VideoCodec;
/// @brief Spec-facing alias for the video gap-reason vocabulary.
using VideoGapReason = GapReason;

#define NEXIGEN_RECORDER_ENUM_JSON(Type)                                                           \
    inline void to_json(nlohmann::json& j, Type value) {                                          \
        core::mandate::enum_to_json(j, value);                                                    \
    }                                                                                             \
    inline void from_json(const nlohmann::json& j, Type& value) noexcept {                        \
        core::mandate::enum_from_json(j, value);                                                  \
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

template <> struct EnumTraits<nexigen::recorder::RecorderFormat> {
    struct Entry {
        nexigen::recorder::RecorderFormat value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 1> entries{{{nexigen::recorder::RecorderFormat::Raw, "Raw"}}};
};

template <> struct EnumTraits<nexigen::recorder::TimeDomain> {
    struct Entry {
        nexigen::recorder::TimeDomain value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 2> entries{
        {{nexigen::recorder::TimeDomain::MonotonicRaw, "MonotonicRaw"},
         {nexigen::recorder::TimeDomain::Utc, "Utc"}}};
};

template <> struct EnumTraits<nexigen::recorder::RecordType> {
    struct Entry {
        nexigen::recorder::RecordType value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 10> entries{
        {{nexigen::recorder::RecordType::ChannelDeclare, "ChannelDeclare"},
         {nexigen::recorder::RecordType::SchemaDeclare, "SchemaDeclare"},
         {nexigen::recorder::RecordType::LogFrame, "LogFrame"},
         {nexigen::recorder::RecordType::MetricFrame, "MetricFrame"},
         {nexigen::recorder::RecordType::SpanFrame, "SpanFrame"},
         {nexigen::recorder::RecordType::ComPayload, "ComPayload"},
         {nexigen::recorder::RecordType::VideoFrameRef, "VideoFrameRef"},
         {nexigen::recorder::RecordType::VideoStreamConfig, "VideoStreamConfig"},
         {nexigen::recorder::RecordType::GapMarker, "GapMarker"},
         {nexigen::recorder::RecordType::TimeSync, "TimeSync"}}};
};

template <> struct EnumTraits<nexigen::recorder::GapReason> {
    struct Entry {
        nexigen::recorder::GapReason value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 5> entries{
        {{nexigen::recorder::GapReason::Unknown, "Unknown"},
         {nexigen::recorder::GapReason::IoxRingOverflow, "IoxRingOverflow"},
         {nexigen::recorder::GapReason::EncoderOverload, "EncoderOverload"},
         {nexigen::recorder::GapReason::IoBackpressure, "IoBackpressure"},
         {nexigen::recorder::GapReason::StreamDiscontinuity, "StreamDiscontinuity"}}};
};

template <> struct EnumTraits<nexigen::recorder::VideoCodec> {
    struct Entry {
        nexigen::recorder::VideoCodec value;
        std::string_view name;
    };
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

template <> struct EnumTraits<nexigen::recorder::BitstreamFormat> {
    struct Entry {
        nexigen::recorder::BitstreamFormat value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 5> entries{
        {{nexigen::recorder::BitstreamFormat::Unknown, "Unknown"},
         {nexigen::recorder::BitstreamFormat::AnnexB, "AnnexB"},
         {nexigen::recorder::BitstreamFormat::Avcc, "Avcc"},
         {nexigen::recorder::BitstreamFormat::Obu, "Obu"},
         {nexigen::recorder::BitstreamFormat::Raw, "Raw"}}};
};

template <> struct EnumTraits<nexigen::recorder::VideoCrcMode> {
    struct Entry {
        nexigen::recorder::VideoCrcMode value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 2> entries{
        {{nexigen::recorder::VideoCrcMode::Off, "Off"},
         {nexigen::recorder::VideoCrcMode::PerFrame, "PerFrame"}}};
};

template <> struct EnumTraits<nexigen::recorder::DatasetKind> {
    struct Entry {
        nexigen::recorder::DatasetKind value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{
        {{nexigen::recorder::DatasetKind::Telemetry, "Telemetry"},
         {nexigen::recorder::DatasetKind::Opaque, "Opaque"},
         {nexigen::recorder::DatasetKind::Video, "Video"}}};
};

template <> struct EnumTraits<nexigen::recorder::RetentionPolicy> {
    struct Entry {
        nexigen::recorder::RetentionPolicy value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 2> entries{
        {{nexigen::recorder::RetentionPolicy::Circular, "Circular"},
         {nexigen::recorder::RetentionPolicy::Stop, "Stop"}}};
};

template <> struct EnumTraits<nexigen::recorder::ExportFormat> {
    struct Entry {
        nexigen::recorder::ExportFormat value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 4> entries{
        {{nexigen::recorder::ExportFormat::Jsonl, "Jsonl"},
         {nexigen::recorder::ExportFormat::Mcap, "Mcap"},
         {nexigen::recorder::ExportFormat::Perfetto, "Perfetto"},
         {nexigen::recorder::ExportFormat::Elementary, "Elementary"}}};
};

template <> struct EnumTraits<nexigen::recorder::VideoExportFormat> {
    struct Entry {
        nexigen::recorder::VideoExportFormat value;
        std::string_view name;
    };
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
[[nodiscard]] constexpr std::string_view to_string(RecorderFormat value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr std::string_view to_string(TimeDomain value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr std::string_view to_string(RecordType value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr std::string_view to_string(GapReason value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr std::string_view to_string(VideoCodec value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr std::string_view to_string(BitstreamFormat value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr std::string_view to_string(VideoCrcMode value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr std::string_view to_string(DatasetKind value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr std::string_view to_string(RetentionPolicy value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr std::string_view to_string(ExportFormat value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr std::string_view to_string(VideoExportFormat value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical recorder enum spelling.
/// @param text Candidate spelling.
/// @param out Destination, untouched when parsing fails.
/// @return `true` when `text` matched a known spelling.
[[nodiscard]] constexpr bool from_string(std::string_view text, RecorderFormat& out) noexcept {
    return core::enum_value(text, out);
}
[[nodiscard]] constexpr bool from_string(std::string_view text, TimeDomain& out) noexcept {
    return core::enum_value(text, out);
}
[[nodiscard]] constexpr bool from_string(std::string_view text, RecordType& out) noexcept {
    return core::enum_value(text, out);
}
[[nodiscard]] constexpr bool from_string(std::string_view text, GapReason& out) noexcept {
    return core::enum_value(text, out);
}
[[nodiscard]] constexpr bool from_string(std::string_view text, VideoCodec& out) noexcept {
    return core::enum_value(text, out);
}
[[nodiscard]] constexpr bool from_string(std::string_view text, BitstreamFormat& out) noexcept {
    return core::enum_value(text, out);
}
[[nodiscard]] constexpr bool from_string(std::string_view text, VideoCrcMode& out) noexcept {
    return core::enum_value(text, out);
}
[[nodiscard]] constexpr bool from_string(std::string_view text, DatasetKind& out) noexcept {
    return core::enum_value(text, out);
}
[[nodiscard]] constexpr bool from_string(std::string_view text, RetentionPolicy& out) noexcept {
    return core::enum_value(text, out);
}
[[nodiscard]] constexpr bool from_string(std::string_view text, ExportFormat& out) noexcept {
    return core::enum_value(text, out);
}
[[nodiscard]] constexpr bool from_string(std::string_view text, VideoExportFormat& out) noexcept {
    return core::enum_value(text, out);
}

} // namespace nexigen::recorder
