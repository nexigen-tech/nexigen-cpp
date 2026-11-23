#pragma once

#include <span>
#include <vector>

#include <nexigen/com/event_producer.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_channel_config.hpp>
#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_service_names.hpp>
#include <nexigen/hw/can/com/can_transmitter.hpp>

namespace nexigen::tools::can {

/// @brief Create one rx producer per channel for a receiver tool.
template <class Runtime>
[[nodiscard]] core::Result<std::vector<com::EventProducer<hw::can::CanMessage>*>>
make_rx_producers(Runtime& runtime, std::span<const hw::can::CanChannelConfig> channels) {
    std::vector<com::EventProducer<hw::can::CanMessage>*> producers;
    producers.reserve(channels.size());
    for (const auto& channel : channels) {
        auto service = hw::can::rx_service(channel.name);
        if (!service) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        auto* producer = runtime.template publish<hw::can::CanMessage>(*service);
        if (producer == nullptr) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        producers.push_back(producer);
    }
    return producers;
}

/// @brief Create one status producer per channel with two publishers allowed.
template <class Runtime>
[[nodiscard]] core::Result<std::vector<com::EventProducer<hw::can::CanChannelStatus>*>>
make_status_producers(Runtime& runtime, std::span<const hw::can::CanChannelConfig> channels) {
    std::vector<com::EventProducer<hw::can::CanChannelStatus>*> producers;
    producers.reserve(channels.size());
    for (const auto& channel : channels) {
        auto service = hw::can::status_service(channel.name);
        if (!service) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        com::ChannelConfig config{};
        config.max_publishers = 2U;
        auto* producer = runtime.template publish<hw::can::CanChannelStatus>(*service, config);
        if (producer == nullptr) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        producers.push_back(producer);
    }
    return producers;
}

/// @brief Subscribe a transmitter to every channel's tx service.
template <class Runtime>
bool subscribe_tx(Runtime& runtime, std::span<const hw::can::CanChannelConfig> channels,
                  hw::can::CanTransmitter& transmitter) {
    for (const auto& channel : channels) {
        auto service = hw::can::tx_service(channel.name);
        if (!service) {
            return false;
        }
        const bool attached = runtime.template subscribe<hw::can::CanMessage>(
            *service, [&transmitter, name = channel.name](const hw::can::CanMessage& message) {
                static_cast<void>(transmitter.enqueue(name, message));
            });
        if (!attached) {
            return false;
        }
    }
    return true;
}

} // namespace nexigen::tools::can
