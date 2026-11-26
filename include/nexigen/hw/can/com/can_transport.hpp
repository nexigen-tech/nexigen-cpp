#pragma once

#include <cstdint>

#include <nexigen/com/channel_config.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/com/event_time.hpp>
#include <nexigen/com/hardware_metadata.hpp>
#include <nexigen/hw/can/can_event_code.hpp>
#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/hw/can/can_message_batch.hpp>

namespace nexigen::hw::can {

/// @brief Bounded transport settings for batch-capable virtual services.
/// @return Channel settings sized for ~11 KiB batch payloads.
[[nodiscard]] inline com::ChannelConfig batch_channel_config() noexcept {
    com::ChannelConfig config{};
    config.queue_size = 32U;
    config.history_size = 8U;
    return config;
}

/// @brief Channel configuration for one channel's shared `status` stream.
///
/// The receiver and the transmitter both publish status, so the service allows
/// two publishers and consumers track each application's sequence separately.
/// @return Status-channel configuration.
[[nodiscard]] inline com::ChannelConfig status_channel_config() noexcept {
    com::ChannelConfig config{};
    config.max_publishers = 2U;
    return config;
}

/// @brief Event code for one CAN message kind.
/// @param kind Message kind.
/// @return The transport event code.
[[nodiscard]] constexpr std::uint32_t event_code_for(CanMessageKind kind) noexcept {
    switch (kind) {
        case CanMessageKind::Error:       return event_error;
        case CanMessageKind::Overload:    return event_overload;
        case CanMessageKind::BusIdle:     return event_bus_idle;
        case CanMessageKind::ClassicData:
        case CanMessageKind::FdData:
        case CanMessageKind::Remote:
        default:                          return event_rx;
    }
}

/// @brief Build the transport event time from a CAN message.
/// @param message Source message.
/// @return Explicit time and its domain.
[[nodiscard]] inline com::EventTime event_time(const CanMessage& message) noexcept {
    const com::TimestampDomain domain =
        message.timestamp_source == CanTimestampSource::DriverFallback
            ? com::TimestampDomain::MonotonicRaw
            : com::TimestampDomain::Hardware;
    return com::EventTime{message.hardware_timestamp_ns, domain};
}

/// @brief Build optional hardware metadata from a CAN message.
/// @param message Source message.
/// @return Metadata preserving the hardware sequence and timestamp.
[[nodiscard]] inline com::HardwareMetadata hardware_metadata(const CanMessage& message) noexcept {
    com::HardwareMetadata metadata{};
    metadata.sequence = message.hardware_sequence;
    metadata.has_sequence = message.frame_flags.has(CanFrameFlags::has_hardware_sequence);
    metadata.native_ticks = message.hardware_timestamp_ns;
    metadata.has_ticks = message.frame_flags.has(CanFrameFlags::has_hardware_timestamp);
    metadata.canonical_ns = message.hardware_timestamp_ns;
    metadata.has_canonical = message.hardware_timestamp_ns != 0U;
    metadata.domain_id = 0;
    return metadata;
}

} // namespace nexigen::hw::can
