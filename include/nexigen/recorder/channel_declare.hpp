// nexigen::recorder — self-contained channel declaration.
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/recorder/recorder_types.hpp>

namespace nexigen::recorder {

/// @brief A channel's stable identity and payload contract.
struct ChannelDeclare {
    std::uint32_t id{0};             ///< session-scoped channel id
    std::uint32_t schema_id{0};      ///< referenced schema id
    std::string topic{};             ///< service name, topic, or stream name
    DatasetKind kind{DatasetKind::Telemetry}; ///< telemetry, opaque, or video
    std::string message_encoding{};  ///< json | protobuf | flatbuffer | nexigen.pod | raw
    std::string metadata{};          ///< optional JSON metadata (units, app, labels)

    /// @brief Validate id, topic, kind, and encoding.
    /// @return `Ok`, or a named validation error.
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `ChannelDeclare::to_string`.
/// @param value Value to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const ChannelDeclare& channel) {
    return channel.to_string();
}

void to_json(nlohmann::json& j, const ChannelDeclare& channel);
void from_json(const nlohmann::json& j, ChannelDeclare& channel) noexcept;

} // namespace nexigen::recorder
