// nexigen::recorder — dataset inventory and per-dataset information.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/recorder/recorder_types.hpp>

namespace nexigen::recorder {

/// @brief One schema field or array exposed by a dataset.
struct DatasetField {
    std::string name{};        ///< field name
    std::string type{};        ///< scalar/array type spelling
    std::string units{};       ///< optional unit hint
    std::uint32_t offset{0};   ///< byte offset in the payload
    std::uint32_t element_size{0}; ///< element size in bytes
    std::uint32_t count{0};    ///< element count (1 for scalar)
    std::uint32_t stride{0};   ///< byte stride between elements

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable field summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief One segment's contribution to a dataset.
struct DatasetSegmentInfo {
    std::uint32_t seq{0};      ///< segment sequence
    std::string path{};        ///< `.nxrec` or `.nxvid` path
    std::uint64_t records{0};  ///< records or frames
    std::uint64_t gaps{0};     ///< gap markers attributed to this segment
    std::uint64_t bytes{0};    ///< payload bytes
    std::uint64_t start_event_ns{0}; ///< first event time
    std::uint64_t end_event_ns{0};   ///< last event time
    bool closed{false};        ///< segment finalized with a valid footer

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable field summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Inventory entry for one telemetry, opaque, or video dataset.
struct DatasetInfo {
    std::uint32_t channel_id{0};
    DatasetKind kind{DatasetKind::Telemetry};
    std::string name{};              ///< topic or stream name
    std::uint32_t schema_id{0};
    std::string schema_name{};
    std::string schema_encoding{};
    std::string message_encoding{};
    VideoCodec codec{VideoCodec::Unknown};
    BitstreamFormat bitstream{BitstreamFormat::Unknown};
    std::uint32_t width{0};          ///< video width (0 for non-video)
    std::uint32_t height{0};         ///< video height (0 for non-video)
    std::uint32_t extradata_size{0}; ///< declared codec extradata bytes
    std::uint64_t records{0};
    std::uint64_t gaps{0};           ///< gap markers on this channel
    std::uint64_t payload_bytes{0};
    std::uint64_t first_event_ns{0};
    std::uint64_t last_event_ns{0};
    std::uint64_t first_rec_offset{0};
    std::uint64_t last_rec_offset{0};
    bool closed{true};               ///< every segment carrying it is closed
    std::vector<std::uint64_t> config_epochs{}; ///< observed video epochs
    std::vector<DatasetField> fields{};
    std::vector<DatasetSegmentInfo> segments{};

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable field summary.
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

void to_json(nlohmann::json& j, const DatasetField& field);
void from_json(const nlohmann::json& j, DatasetField& field) noexcept;
void to_json(nlohmann::json& j, const DatasetSegmentInfo& segment);
void from_json(const nlohmann::json& j, DatasetSegmentInfo& segment) noexcept;
void to_json(nlohmann::json& j, const DatasetInfo& dataset);
void from_json(const nlohmann::json& j, DatasetInfo& dataset) noexcept;

} // namespace nexigen::recorder
