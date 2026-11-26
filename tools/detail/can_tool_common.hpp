#pragma once

#include <span>
#include <vector>

#include <nexigen/com/event_producer.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_channel_config.hpp>
#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_service_names.hpp>
#include <nexigen/hw/can/com/can_transmitter.hpp>
#include <nexigen/hw/can/com/can_transport.hpp>

namespace nexigen::tools::can {

/// @brief Create one rx producer per channel for a receiver tool.
template <class Runtime>
[[nodiscard]] core::Result<std::vector<com::EventProducer<hw::can::CanMessageBatch>*>>
make_rx_producers(Runtime& runtime, std::span<const hw::can::CanChannelConfig> channels) {
    std::vector<com::EventProducer<hw::can::CanMessageBatch>*> producers;
    producers.reserve(channels.size());
    for (const auto& channel : channels) {
        auto service = hw::can::rx_service(channel.name);
        if (!service) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        auto* producer = runtime.template publish<hw::can::CanMessageBatch>(
            *service, hw::can::batch_channel_config());
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
        auto* producer = runtime.template publish<hw::can::CanChannelStatus>(
            *service, hw::can::status_channel_config());
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
        const bool attached = runtime.template subscribe_with_config<hw::can::CanMessageBatch>(
            *service,
            [&transmitter, name = channel.name](const hw::can::CanMessageBatch& batch) {
                static_cast<void>(transmitter.enqueue_batch(name, batch));
            },
            hw::can::batch_channel_config());
        if (!attached) {
            return false;
        }
    }
    return true;
}

} // namespace nexigen::tools::can
