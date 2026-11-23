#include <nexigen/hw/can/com/virtual_can_driver.hpp>

#include <cstdio>
#include <functional>

namespace nexigen::hw::can {

VirtualCanDriver::VirtualCanDriver(std::string_view app_id) noexcept : app_id_(app_id) {
    auto node = com::Node::create();
    if (!node) {
        return;
    }
    node_ = std::make_unique<com::Node>(std::move(*node));
    set_.emplace(*node_);
    if (!*set_) {
        node_.reset();
        set_.reset();
        return;
    }
    worker_ = std::jthread([this](std::stop_token token) { wait_loop(token); });
}

VirtualCanDriver::~VirtualCanDriver() {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    channels_.clear();
    set_.reset();
    node_.reset();
}

core::Result<CanDriverCapabilities> VirtualCanDriver::capabilities() noexcept {
    if (!usable()) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    CanDriverCapabilities caps{};
    caps.supports_fd = true;
    caps.supports_remote = true;
    caps.hardware_timestamps = true;
    caps.hardware_sequence = true;
    caps.error_frames = true;
    caps.overload_frames = true;
    caps.heartbeats = true;
    caps.max_channels = 16;
    return caps;
}

core::Status VirtualCanDriver::open_channel(const CanChannelConfig& config,
                                            ICanMessageSink& sink) noexcept {
    if (!usable()) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    if (core::Status valid = config.validate(); !valid) {
        return valid;
    }
    if (find(config.name) != nullptr) {
        return std::unexpected(core::ErrorCode::Conflict);
    }
    auto rx_name = vbus_rx_service(config.name);
    auto tx_name = vbus_tx_service(config.name);
    if (!rx_name || !tx_name) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    auto rx_service = com::ServiceName::create(rx_name->c_str());
    auto tx_service = com::ServiceName::create(tx_name->c_str());
    if (!rx_service || !tx_service) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    com::ChannelConfig channel_config{};
    auto rx_channel = com::EventChannel<CanMessage>::create(*node_, *rx_service, channel_config);
    if (!rx_channel) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    using EventCb = std::function<void(const com::EventHeader&, const CanMessage&)>;
    using LossCb = std::function<void(const com::LossInfo&)>;
    EventCb on_event = [&sink](const com::EventHeader&, const CanMessage& message) {
        sink.on_message(message);
    };
    LossCb on_loss = [](const com::LossInfo&) {};
    auto consumer = rx_channel->consumer(std::move(on_event), std::move(on_loss));
    if (!consumer) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    auto endpoint = std::make_unique<Endpoint<EventCb, LossCb>>(
        *node_, *tx_service, channel_config, app_id_, std::move(*rx_channel), std::move(*consumer));
    if (auto attached = set_->attach(endpoint->consumer()); !attached) {
        std::fprintf(stderr, "vcan: attach failed\n");
        return std::unexpected(core::ErrorCode::SystemError);
    }
    channels_.emplace_back(config.name, std::move(endpoint));
    return {};
}

core::Status VirtualCanDriver::submit(std::string_view channel,
                                      const CanMessage& message) noexcept {
    EndpointBase* endpoint = find(channel);
    if (endpoint == nullptr) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    if (core::Status valid = message.validate(); !valid) {
        return valid;
    }
    auto* producer = endpoint->ensure_producer();
    if (producer == nullptr) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (!producer->publish(message, event_tx, event_time(message), hardware_metadata(message))
             .has_value()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

core::Status VirtualCanDriver::close_channel(std::string_view channel) noexcept {
    for (auto it = channels_.begin(); it != channels_.end(); ++it) {
        if (it->first != channel) {
            continue;
        }
        static_cast<void>(set_->detach(it->second->consumer()));
        channels_.erase(it);
        return {};
    }
    return std::unexpected(core::ErrorCode::NotFound);
}

VirtualCanDriver::EndpointBase* VirtualCanDriver::find(std::string_view channel) noexcept {
    for (auto& entry : channels_) {
        if (entry.first == channel) {
            return entry.second.get();
        }
    }
    return nullptr;
}

void VirtualCanDriver::wait_loop(std::stop_token token) noexcept {
    while (!token.stop_requested()) {
        if (set_) {
            static_cast<void>(set_->wait_for(std::chrono::milliseconds{100}));
        }
    }
}

} // namespace nexigen::hw::can
