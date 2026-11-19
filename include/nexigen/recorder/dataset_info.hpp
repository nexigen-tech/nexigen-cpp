/// @file dataset_info.hpp
/// @brief Dataset inventory and per-dataset information for the converter.
///
/// These cold-path value objects are produced by `SessionReader::datasets()` and
/// rendered by `nexigen-export list|info|inspect`. They contain sizes, time
/// ranges, segment breakdowns, schema fields, gap counts, and closed state, but
/// never payload or video bytes.
///
/// @note Spec `recorder/export`, requirements "Dataset inventory" and
///       "Per-dataset information"; video fields follow spec
///       `recorder/video-storage`, requirement "Video frame references".
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/recorder/recorder_types.hpp>

namespace nexigen::recorder {

/// @brief One schema field or array exposed by a dataset.
///
/// A field is a fixed-offset POD view parsed from the embedded JSON Schema; it
/// lets the converter decode a payload without linking the producer SDK.
///
/// @note Spec `recorder/export`, requirement "Schema-driven decoding".
struct DatasetField {
    std::string name{};            ///< field name
    std::string type{};            ///< scalar/array type spelling
    std::string units{};           ///< optional unit hint
    std::uint32_t offset{0};       ///< byte offset in the payload
    std::uint32_t element_size{0}; ///< element size in bytes
    std::uint32_t count{0};        ///< element count (1 for scalar)
    std::uint32_t stride{0};       ///< byte stride between elements

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable field summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief One segment's contribution to a dataset.
///
/// Aggregates the records, gaps, byte count, and event-time bounds observed in
/// one `.nxrec`/`.nxvid` pair for the selected dataset.
///
/// @note Spec `recorder/export`, requirement "Per-dataset information".
struct DatasetSegmentInfo {
    std::uint32_t seq{0};            ///< segment sequence
    std::string path{};              ///< `.nxrec` or `.nxvid` path
    std::uint64_t records{0};        ///< records or frames
    std::uint64_t gaps{0};           ///< gap markers attributed to this segment
    std::uint64_t bytes{0};          ///< payload bytes
    std::uint64_t start_event_ns{0}; ///< first event time
    std::uint64_t end_event_ns{0};   ///< last event time
    bool closed{false};              ///< segment finalized with a valid footer

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable field summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Inventory entry for one telemetry, opaque, or video dataset.
///
/// Identifies the channel, its schema and message encoding, its observed
/// records/gaps, byte and time bounds, video codec configuration, configuration
/// epochs, parsed fields, and per-segment contributions. `list` and `info`
/// render this object without reading payload bytes.
///
/// @note Spec `recorder/export`, requirements "Dataset inventory" and
///       "Per-dataset information"; spec `recorder/video-storage`,
///       requirement "Video gap reporting".
struct DatasetInfo {
    std::uint32_t channel_id{0};                         ///< Declared channel id.
    DatasetKind kind{DatasetKind::Telemetry};            ///< Telemetry, opaque, or video.
    std::string name{};                                  ///< topic or stream name
    std::uint32_t schema_id{0};                          ///< Referenced schema id.
    std::string schema_name{};                           ///< Embedded schema name.
    std::string schema_encoding{};                       ///< `jsonschema`/`protobuf`/...
    std::string message_encoding{};                      ///< Payload message encoding.
    std::string native_domain{"monotonic_raw"};          ///< Native time domain of the fields.
    std::uint16_t domain_id{0};                          ///< Time-base domain id.
    std::uint64_t domain_epoch{0};                       ///< Time-base domain epoch.
    VideoCodec codec{VideoCodec::Unknown};               ///< Video codec, or `Unknown`.
    BitstreamFormat bitstream{BitstreamFormat::Unknown}; ///< Video framing.
    std::uint32_t width{0};                              ///< video width (0 for non-video)
    std::uint32_t height{0};                             ///< video height (0 for non-video)
    std::uint32_t extradata_size{0};                     ///< declared codec extradata bytes
    std::uint64_t records{0};                            ///< Records observed on the channel.
    std::uint64_t gaps{0};                               ///< gap markers on this channel
    std::uint64_t payload_bytes{0};                      ///< Sum of stored payload bytes.
    std::uint64_t first_event_ns{0};                     ///< Earliest event time observed.
    std::uint64_t last_event_ns{0};                      ///< Latest event time observed.
    std::uint64_t first_rec_offset{0};                   ///< First `.nxrec` offset.
    std::uint64_t last_rec_offset{0};                    ///< Last `.nxrec` offset.
    bool closed{true};                                   ///< every segment carrying it is closed
    std::vector<std::uint64_t> config_epochs{};          ///< observed video epochs
    std::vector<DatasetField> fields{};                  ///< Parsed schema field layout.
    std::vector<DatasetSegmentInfo> segments{};          ///< Per-segment contributions.

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable dataset summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `DatasetField::to_string`.
/// @param field Field to render.
/// @return Human-readable field summary.
[[nodiscard]] inline std::string to_string(const DatasetField& field) {
    return field.to_string();
}
/// @brief Free helper mirroring `DatasetSegmentInfo::to_string`.
/// @param segment Segment contribution to render.
/// @return Human-readable segment summary.
[[nodiscard]] inline std::string to_string(const DatasetSegmentInfo& segment) {
    return segment.to_string();
}
/// @brief Free helper mirroring `DatasetInfo::to_string`.
/// @param dataset Dataset inventory entry to render.
/// @return Human-readable dataset summary.
[[nodiscard]] inline std::string to_string(const DatasetInfo& dataset) {
    return dataset.to_string();
}

/// @brief Diagnostic JSON conversion for dataset inventory value objects.
/// @note Spec `recorder/export`, scenario "Dataset information round-trips".
/// @{
/// @brief Serialize a field descriptor to JSON.
/// @param j Destination JSON object.
/// @param field Field to serialize.
void to_json(nlohmann::json& j, const DatasetField& field);

/// @brief Parse a field descriptor from JSON.
/// @param j Source JSON object.
/// @param field Destination field.
void from_json(const nlohmann::json& j, DatasetField& field) noexcept;

/// @brief Serialize a per-segment contribution to JSON.
/// @param j Destination JSON object.
/// @param segment Segment contribution to serialize.
void to_json(nlohmann::json& j, const DatasetSegmentInfo& segment);

/// @brief Parse a per-segment contribution from JSON.
/// @param j Source JSON object.
/// @param segment Destination contribution.
void from_json(const nlohmann::json& j, DatasetSegmentInfo& segment) noexcept;

/// @brief Serialize a dataset inventory entry to JSON.
/// @param j Destination JSON object.
/// @param dataset Dataset entry to serialize.
void to_json(nlohmann::json& j, const DatasetInfo& dataset);

/// @brief Parse a dataset inventory entry from JSON.
/// @param j Source JSON object.
/// @param dataset Destination entry.
void from_json(const nlohmann::json& j, DatasetInfo& dataset) noexcept;
/// @}

} // namespace nexigen::recorder
