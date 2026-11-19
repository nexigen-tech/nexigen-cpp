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
#include <nexigen/chrono/device_access_mode.hpp>
#include <nexigen/chrono/time_domain.hpp>
#include <nexigen/chrono/time_domain_class.hpp>
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
    std::uint32_t id{0};                      ///< session-scoped channel id
    std::uint32_t schema_id{0};               ///< referenced schema id
    std::string topic{};                      ///< service name, topic, or stream name
    DatasetKind kind{DatasetKind::Telemetry}; ///< telemetry, opaque, or video
    std::string message_encoding{};           ///< json | protobuf | flatbuffer | nexigen.pod | raw
    std::string metadata{};                   ///< optional JSON metadata (units, app, labels)
    /// Native time domain of this channel's event fields: a `TimeDomain` name
    /// or `device` for a declared hardware row.
    std::string native_domain{"monotonic_raw"};
    /// Time-base domain id of the native fields (0 for the MonotonicRaw row).
    std::uint16_t domain_id{0};
    /// Time-base domain epoch the native fields belong to.
    std::uint64_t domain_epoch{0};
    /// Role of the native domain (`system_clock`, `cycle_counter`,
    /// `device_clock`).
    chrono::TimeDomainClass domain_class{chrono::TimeDomainClass::SystemClock};
    /// Pair-acquisition mode for a `device` native domain; ignored for host
    /// domains.
    chrono::DeviceAccessMode access_mode{chrono::DeviceAccessMode::RegisterPair};

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
