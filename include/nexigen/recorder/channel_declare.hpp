/// @file channel_declare.hpp
/// @brief Self-contained channel declaration.
///
/// @note Spec `recorder/raw-storage`, requirements "Embedded schema and channel
///       registry" and "Storage type serialization"; a channel declares its
///       topic, kind, schema reference, and message encoding before the first
///       record that uses it.
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/recorder/recorder_types.hpp>

namespace nexigen::recorder {

/// @brief A channel's stable identity and payload contract.
///
/// A channel id is unique within the session and stable across segments. A
/// schema change creates a new channel rather than reusing the old declaration.
///
/// @note Spec `recorder/raw-storage`, requirement "Embedded schema and channel
///       registry"; `metadata` is optional JSON and never interpreted by the
///       recorder hot path.
struct ChannelDeclare {
    std::uint32_t id{0};             ///< session-scoped channel id
    std::uint32_t schema_id{0};      ///< referenced schema id
    std::string topic{};             ///< service name, topic, or stream name
    DatasetKind kind{DatasetKind::Telemetry}; ///< telemetry, opaque, or video
    std::string message_encoding{};  ///< json | protobuf | flatbuffer | nexigen.pod | raw
    std::string metadata{};          ///< optional JSON metadata (units, app, labels)

    /// @brief Validate id, topic, kind, and encoding.
    /// @return `Ok`, or `InvalidArgument` for an empty/unsafe field.
    /// @note The channel id must fit `kMaxChannels` and the topic must be
    ///       non-empty and free of path separators.
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable channel summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `ChannelDeclare::to_string`.
/// @param channel Value to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const ChannelDeclare& channel) {
    return channel.to_string();
}

/// @brief Serialize a channel declaration to `session.json`.
/// @param j Destination JSON object.
/// @param channel Channel declaration to serialize.
void to_json(nlohmann::json& j, const ChannelDeclare& channel);

/// @brief Parse a channel declaration from `session.json`.
/// @param j Source JSON object.
/// @param channel Destination declaration.
void from_json(const nlohmann::json& j, ChannelDeclare& channel) noexcept;

} // namespace nexigen::recorder
