#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace nexigen::recorder {

// -----------------------------------------------------------------------------
// Format Constants & Magic Identifiers
// -----------------------------------------------------------------------------

inline constexpr std::uint16_t kFormatVersionMajor = 1;
inline constexpr std::uint16_t kFormatVersionMinor = 0;

inline constexpr std::array<char, 8> kMagicRec{'N', 'X', 'R', 'E', 'C', '0', '0', '1'};
inline constexpr std::array<char, 8> kMagicIdx{'N', 'X', 'I', 'D', 'X', '0', '0', '1'};
inline constexpr std::array<char, 8> kMagicVid{'N', 'X', 'V', 'I', 'D', '0', '0', '1'};

inline constexpr std::uint32_t kRecordMagic = 0x52434431U;  // "RCD1"
inline constexpr std::uint32_t kFooterMagic = 0x46545231U;  // "FTR1"
inline constexpr std::size_t kVideoPageAlignment = 4096U;   // 4 KiB NVMe page alignment

// -----------------------------------------------------------------------------
// Enumerations & Constexpr String Conversions
// -----------------------------------------------------------------------------

enum class SegmentFileKind : std::uint8_t {
  Record = 1,  // .nxrec
  Index = 2,   // .nxidx
  Video = 3,   // .nxvid
};

[[nodiscard]] constexpr std::string_view to_string(SegmentFileKind kind) noexcept {
  switch (kind) {
    case SegmentFileKind::Record:
      return "Record";
    case SegmentFileKind::Index:
      return "Index";
    case SegmentFileKind::Video:
      return "Video";
  }
  return "Unknown";
}

[[nodiscard]] constexpr std::optional<SegmentFileKind> segment_file_kind_from_string(
    std::string_view str) noexcept {
  if (str == "Record") return SegmentFileKind::Record;
  if (str == "Index") return SegmentFileKind::Index;
  if (str == "Video") return SegmentFileKind::Video;
  return std::nullopt;
}

enum class RecordType : std::uint16_t {
  ChannelDeclare = 1,  // Maps ChannelId -> topic/service name & schema tag
  LogFrame = 2,        // nexigen::logging::LogRecord / LogFrame
  MetricFrame = 3,     // nexigen::metrics::MetricFrame
  SpanFrame = 4,       // nexigen::tracing::SpanFrame (384 B)
  ComPayload = 5,      // Arbitrary pointer-free iceoryx2 payload
  VideoFrameRef = 6,   // Metadata pointing to a frame slice inside .nxvid
  GapMarker = 7,       // Explicit sequence discontinuity / drop notification
  TimeSync = 8,        // CLOCK_MONOTONIC_RAW <-> UTC offset calibration point
};

[[nodiscard]] constexpr std::string_view to_string(RecordType type) noexcept {
  switch (type) {
    case RecordType::ChannelDeclare:
      return "ChannelDeclare";
    case RecordType::LogFrame:
      return "LogFrame";
    case RecordType::MetricFrame:
      return "MetricFrame";
    case RecordType::SpanFrame:
      return "SpanFrame";
    case RecordType::ComPayload:
      return "ComPayload";
    case RecordType::VideoFrameRef:
      return "VideoFrameRef";
    case RecordType::GapMarker:
      return "GapMarker";
    case RecordType::TimeSync:
      return "TimeSync";
  }
  return "Unknown";
}

[[nodiscard]] constexpr std::optional<RecordType> record_type_from_string(
    std::string_view str) noexcept {
  if (str == "ChannelDeclare") return RecordType::ChannelDeclare;
  if (str == "LogFrame") return RecordType::LogFrame;
  if (str == "MetricFrame") return RecordType::MetricFrame;
  if (str == "SpanFrame") return RecordType::SpanFrame;
  if (str == "ComPayload") return RecordType::ComPayload;
  if (str == "VideoFrameRef") return RecordType::VideoFrameRef;
  if (str == "GapMarker") return RecordType::GapMarker;
  if (str == "TimeSync") return RecordType::TimeSync;
  return std::nullopt;
}

enum class VideoCodecTag : std::uint16_t {
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

[[nodiscard]] constexpr std::string_view to_string(VideoCodecTag codec) noexcept {
  switch (codec) {
    case VideoCodecTag::Unknown:
      return "Unknown";
    case VideoCodecTag::RawRgb24:
      return "RawRgb24";
    case VideoCodecTag::RawBgr24:
      return "RawBgr24";
    case VideoCodecTag::RawNv12:
      return "RawNv12";
    case VideoCodecTag::RawYuv420p:
      return "RawYuv420p";
    case VideoCodecTag::RawBayer8:
      return "RawBayer8";
    case VideoCodecTag::JpegXs:
      return "JpegXs";
    case VideoCodecTag::H264:
      return "H264";
    case VideoCodecTag::H265:
      return "H265";
    case VideoCodecTag::Av1:
      return "Av1";
  }
  return "Unknown";
}

[[nodiscard]] constexpr std::optional<VideoCodecTag> video_codec_tag_from_string(
    std::string_view str) noexcept {
  if (str == "RawRgb24") return VideoCodecTag::RawRgb24;
  if (str == "RawBgr24") return VideoCodecTag::RawBgr24;
  if (str == "RawNv12") return VideoCodecTag::RawNv12;
  if (str == "RawYuv420p") return VideoCodecTag::RawYuv420p;
  if (str == "RawBayer8") return VideoCodecTag::RawBayer8;
  if (str == "JpegXs") return VideoCodecTag::JpegXs;
  if (str == "H264") return VideoCodecTag::H264;
  if (str == "H265") return VideoCodecTag::H265;
  if (str == "Av1") return VideoCodecTag::Av1;
  return std::nullopt;
}

enum class GapReason : std::uint16_t {
  Unknown = 0,
  IoxRingOverflow = 1,   // Producer outpaced subscriber shared-memory buffer
  EncoderOverload = 2,   // Dropped before SW/HW video encoder due to queue limit
  IoBackpressure = 3,    // Dropped due to storage write saturation
  ChecksumMismatch = 4,  // Detected during offline recovery/verification
};

[[nodiscard]] constexpr std::string_view to_string(GapReason reason) noexcept {
  switch (reason) {
    case GapReason::Unknown:
      return "Unknown";
    case GapReason::IoxRingOverflow:
      return "IoxRingOverflow";
    case GapReason::EncoderOverload:
      return "EncoderOverload";
    case GapReason::IoBackpressure:
      return "IoBackpressure";
    case GapReason::ChecksumMismatch:
      return "ChecksumMismatch";
  }
  return "Unknown";
}

[[nodiscard]] constexpr std::optional<GapReason> gap_reason_from_string(
    std::string_view str) noexcept {
  if (str == "IoxRingOverflow") return GapReason::IoxRingOverflow;
  if (str == "EncoderOverload") return GapReason::EncoderOverload;
  if (str == "IoBackpressure") return GapReason::IoBackpressure;
  if (str == "ChecksumMismatch") return GapReason::ChecksumMismatch;
  return std::nullopt;
}


// -----------------------------------------------------------------------------
// Binary Wire Structures (Trivially Copyable, Explicit Padding, Little-Endian)
// -----------------------------------------------------------------------------

/// Fixed 64-byte header written at offset 0 of .nxrec, .nxidx, and .nxvid files.
struct alignas(8) SegmentFileHeader {
  std::array<char, 8> magic{};        // kMagicRec / kMagicIdx / kMagicVid
  std::uint16_t version_major{kFormatVersionMajor};
  std::uint16_t version_minor{kFormatVersionMinor};
  SegmentFileKind file_kind{SegmentFileKind::Record};
  std::uint8_t flags{0};              // Bit 0: little-endian (0), Bit 1: clean-closed (1)
  std::uint16_t reserved0{0};
  std::uint32_t segment_seq{0};       // 0-based segment index within the session
  std::uint32_t header_crc32c{0};     // CRC-32C of bytes [0..19] + [24..63]
  std::uint64_t session_id{0};        // Unique 64-bit session identifier
  std::uint64_t start_mono_ns{0};     // CLOCK_MONOTONIC_RAW timestamp at segment open
  std::int64_t utc_offset_ns{0};      // Offset to convert mono_ns -> UTC epoch ns
  std::array<std::uint8_t, 16> reserved1{};
};
static_assert(sizeof(SegmentFileHeader) == 64);
static_assert(std::is_trivially_copyable_v<SegmentFileHeader>);
static_assert(std::is_standard_layout_v<SegmentFileHeader>);

/// Fixed 40-byte framing header preceding every record payload inside .nxrec.
struct alignas(8) RecordHeader {
  std::uint32_t magic{kRecordMagic};  // kRecordMagic ("RCD1")
  RecordType type{RecordType::ComPayload};
  std::uint16_t flags{0};             // Reserved for compression/alignment flags
  std::uint32_t channel_id{0};        // Logical channel assigned via ChannelDeclare
  std::uint32_t payload_size{0};      // Byte size of payload immediately following header
  std::uint64_t mono_ns{0};           // Producer or ingestion CLOCK_MONOTONIC_RAW ns
  std::uint64_t sequence{0};          // Per-channel monotonic sequence number
  std::uint32_t header_crc32c{0};     // CRC-32C of bytes [0..31]
  std::uint32_t payload_crc32c{0};    // CRC-32C of payload bytes
};
static_assert(sizeof(RecordHeader) == 40);
static_assert(std::is_trivially_copyable_v<RecordHeader>);
static_assert(std::is_standard_layout_v<RecordHeader>);

/// Payload for RecordType::VideoFrameRef (48 bytes).
/// Points to the actual compressed/raw frame slice stored in the companion .nxvid file.
struct alignas(8) VideoFrameRef {
  std::uint64_t vid_offset{0};        // Byte offset in .nxvid (4096-byte aligned)
  std::uint32_t vid_size{0};          // Exact payload byte length in .nxvid
  std::uint32_t vid_crc32c{0};        // CRC-32C of the frame bytes in .nxvid
  VideoCodecTag codec{VideoCodecTag::Unknown};
  std::uint16_t frame_flags{0};       // Bit 0: IsKeyFrame (always 1 for All-Intra)
  std::uint32_t width{0};             // Frame width in pixels
  std::uint32_t height{0};            // Frame height in pixels
  std::uint32_t stride{0};            // Row stride in bytes (for RAW frames; 0 if compressed)
  std::uint64_t capture_mono_ns{0};   // Hardware/sensor capture timestamp if distinct from record ts
  std::uint64_t reserved{0};
};
static_assert(sizeof(VideoFrameRef) == 48);
static_assert(std::is_trivially_copyable_v<VideoFrameRef>);
static_assert(std::is_standard_layout_v<VideoFrameRef>);

/// Payload for RecordType::GapMarker (24 bytes).
struct alignas(8) GapMarkerPayload {
  std::uint64_t first_lost_seq{0};    // First missing sequence number
  std::uint64_t lost_count{0};        // Number of dropped/lost samples
  GapReason reason{GapReason::Unknown};
  std::uint16_t reserved0{0};
  std::uint32_t reserved1{0};
};
static_assert(sizeof(GapMarkerPayload) == 24);
static_assert(std::is_trivially_copyable_v<GapMarkerPayload>);
static_assert(std::is_standard_layout_v<GapMarkerPayload>);

/// Fixed 32-byte entry appended sequentially to .nxidx for O(log N) binary search.
struct alignas(8) IndexEntry {
  std::uint64_t mono_ns{0};           // CLOCK_MONOTONIC_RAW timestamp at index point
  std::uint64_t rec_offset{0};        // Exact byte offset of a RecordHeader inside .nxrec
  std::uint64_t channel_mask{0};      // Bitmask of channels (0..63) active since prev entry
  std::uint32_t record_count{0};      // Number of records written since previous index entry
  std::uint32_t entry_crc32c{0};      // CRC-32C of bytes [0..27]
};
static_assert(sizeof(IndexEntry) == 32);
static_assert(std::is_trivially_copyable_v<IndexEntry>);
static_assert(std::is_standard_layout_v<IndexEntry>);

/// Fixed 64-byte footer written at the end of .nxrec upon clean segment close.
struct alignas(8) SegmentFooter {
  std::uint32_t magic{kFooterMagic};  // kFooterMagic ("FTR1")
  std::uint32_t channel_count{0};     // Total declared channels in this segment
  std::uint64_t end_mono_ns{0};       // Last recorded CLOCK_MONOTONIC_RAW timestamp
  std::uint64_t total_records{0};     // Total records written to .nxrec
  std::uint64_t total_gaps{0};        // Total GapMarker events recorded
  std::uint64_t rec_bytes_written{0}; // Byte offset where this footer begins
  std::uint64_t vid_bytes_written{0}; // Total bytes written to companion .nxvid
  std::uint64_t reserved{0};
  std::uint32_t footer_crc32c{0};     // CRC-32C of bytes [0..59]
  std::uint32_t magic_end{kFooterMagic};
};
static_assert(sizeof(SegmentFooter) == 64);
static_assert(std::is_trivially_copyable_v<SegmentFooter>);
static_assert(std::is_standard_layout_v<SegmentFooter>);

// -----------------------------------------------------------------------------
// Cold-Path Serialization Declarations (to_string & nlohmann::json ADL)
// -----------------------------------------------------------------------------

[[nodiscard]] std::string to_string(const SegmentFileHeader& header);
[[nodiscard]] std::string to_string(const RecordHeader& header);
[[nodiscard]] std::string to_string(const VideoFrameRef& ref);
[[nodiscard]] std::string to_string(const GapMarkerPayload& gap);
[[nodiscard]] std::string to_string(const IndexEntry& entry);
[[nodiscard]] std::string to_string(const SegmentFooter& footer);

void to_json(nlohmann::json& j, const SegmentFileHeader& header);
void from_json(const nlohmann::json& j, SegmentFileHeader& header);

void to_json(nlohmann::json& j, const VideoFrameRef& ref);
void from_json(const nlohmann::json& j, VideoFrameRef& ref);

void to_json(nlohmann::json& j, const GapMarkerPayload& gap);
void from_json(const nlohmann::json& j, GapMarkerPayload& gap);

void to_json(nlohmann::json& j, const IndexEntry& entry);
void from_json(const nlohmann::json& j, IndexEntry& entry);

void to_json(nlohmann::json& j, const SegmentFooter& footer);
void from_json(const nlohmann::json& j, SegmentFooter& footer);

}  // namespace nexigen::recorder

// -----------------------------------------------------------------------------
// std::formatter Specializations (C++23)
// -----------------------------------------------------------------------------

template <>
struct std::formatter<nexigen::recorder::SegmentFileKind> : std::formatter<std::string_view> {
  auto format(nexigen::recorder::SegmentFileKind v, std::format_context& ctx) const {
    return std::formatter<std::string_view>::format(nexigen::recorder::to_string(v), ctx);
  }
};

template <>
struct std::formatter<nexigen::recorder::RecordType> : std::formatter<std::string_view> {
  auto format(nexigen::recorder::RecordType v, std::format_context& ctx) const {
    return std::formatter<std::string_view>::format(nexigen::recorder::to_string(v), ctx);
  }
};

template <>
struct std::formatter<nexigen::recorder::VideoCodecTag> : std::formatter<std::string_view> {
  auto format(nexigen::recorder::VideoCodecTag v, std::format_context& ctx) const {
    return std::formatter<std::string_view>::format(nexigen::recorder::to_string(v), ctx);
  }
};

template <>
struct std::formatter<nexigen::recorder::GapReason> : std::formatter<std::string_view> {
  auto format(nexigen::recorder::GapReason v, std::format_context& ctx) const {
    return std::formatter<std::string_view>::format(nexigen::recorder::to_string(v), ctx);
  }
};

template <>
struct std::formatter<nexigen::recorder::VideoFrameRef> : std::formatter<std::string> {
  auto format(const nexigen::recorder::VideoFrameRef& v, std::format_context& ctx) const {
    return std::formatter<std::string>::format(nexigen::recorder::to_string(v), ctx);
  }
};

template <>
struct std::formatter<nexigen::recorder::GapMarkerPayload> : std::formatter<std::string> {
  auto format(const nexigen::recorder::GapMarkerPayload& v, std::format_context& ctx) const {
    return std::formatter<std::string>::format(nexigen::recorder::to_string(v), ctx);
  }
};
