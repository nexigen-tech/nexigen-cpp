#include <nexigen/hw/can/com/can_transmitter.hpp>

#include <nexigen/hw/can/can_event_code.hpp>
#include <nexigen/hw/can/com/can_transport.hpp>

#include <utility>

namespace nexigen::hw::can {

CanTransmitter::CanTransmitter(
    CanDriver& driver, std::span<const CanChannelConfig> configs,
    std::span<com::EventProducer<CanChannelStatus>*> status_producers) noexcept
    : driver_(&driver) {
    channels_.reserve(configs.size());
    for (std::size_t i = 0; i < configs.size(); ++i) {
        auto channel = std::make_unique<Channel>();
        channel->config = configs[i];
        if (i < status_producers.size()) {
            channel->status_producer = status_producers[i];
        }
        channels_.push_back(std::move(channel));
    }
}

core::Status CanTransmitter::start() noexcept {
    if (driver_ == nullptr) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    for (auto& channel : channels_) {
        if (const core::Status opened = driver_->open_channel(channel->config, tracking_sink_);
            !opened) {
            stop();
            return opened;
        }
        channel->opened = true;
        workers_.emplace_back(
            [this, raw = channel.get()](std::stop_token token) { submit_loop(*raw, token); });
    }
    return {};
}

void CanTransmitter::stop() noexcept {
    for (auto& worker : workers_) {
        worker.request_stop();
    }
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
    if (driver_ != nullptr) {
        for (auto& channel : channels_) {
            if (channel->opened) {
                static_cast<void>(driver_->close_channel(channel->config.name));
                channel->opened = false;
            }
        }
    }
}

CanTransmitter::Channel* CanTransmitter::find(std::string_view channel) noexcept {
    for (auto& candidate : channels_) {
        if (candidate->config.name == channel) {
            return candidate.get();
        }
    }
    return nullptr;
}

void CanTransmitter::on_driver_message(const CanMessage& message) noexcept {
    for (auto& channel : channels_) {
        if (channel->config.channel_id != message.channel_id) {
            continue;
        }
        channel->tracker.observe(message);
        const bool state_changed = channel->tracker.consume_state_changed();
        const std::uint64_t now = message.hardware_timestamp_ns;
        const std::uint64_t interval = channel->config.status_interval_ns;
        if (channel->status_producer != nullptr &&
            (state_changed || interval == 0U || now - channel->last_status_ns >= interval)) {
            publish_status(*channel);
        }
        return;
    }
}

void CanTransmitter::publish_status(Channel& channel) noexcept {
    if (channel.status_producer == nullptr) {
        return;
    }
    const auto now = nexigen::chrono::Timestamp::now();
    const std::uint64_t timestamp = now ? now->nanoseconds() : 0U;
    channel.last_status_ns = timestamp;
    static_cast<void>(channel.status_producer->publish(
        channel.tracker.status(), event_status,
        com::EventTime{timestamp, com::TimestampDomain::Hardware}, com::HardwareMetadata{}));
}

core::Status CanTransmitter::enqueue(std::string_view channel, const CanMessage& message) noexcept {
    CanMessageBatch packet{};
    packet.channel_id = message.channel_id;
    packet.count = 1U;
    packet.messages[0] = message;
    return enqueue_batch(channel, packet);
}

core::Status CanTransmitter::validate_message(Channel& channel, CanMessage& message,
                                              std::uint64_t& fallback_ns) noexcept {
    if (const core::Status valid = message.validate(); !valid) {
        return valid;
    }
    if (message.kind == CanMessageKind::Error || message.kind == CanMessageKind::Overload ||
        message.kind == CanMessageKind::BusIdle) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (message.channel_id != channel.config.channel_id) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (message.kind == CanMessageKind::FdData && !channel.config.fd_enabled) {
        return std::unexpected(core::ErrorCode::UnsupportedFormat);
    }
    if (message.hardware_timestamp_ns == 0U) {
        if (fallback_ns == 0U) {
            const auto now = nexigen::chrono::Timestamp::now();
            if (!now) {
                return std::unexpected(core::ErrorCode::SystemError);
            }
            fallback_ns = now->nanoseconds();
        }
        message.hardware_timestamp_ns = fallback_ns;
        message.timestamp_source = CanTimestampSource::Simulated;
    }
    if (message.origin == CanMessageOrigin::Unknown) {
        message.origin = CanMessageOrigin::LocalTransmit;
    }
    return {};
}

core::Status CanTransmitter::enqueue_batch(std::string_view channel,
                                           const CanMessageBatch& batch) noexcept {
    Channel* target = find(channel);
    if (target == nullptr) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    if (batch.count == 0U || batch.count > CanMessageBatch::max_messages || batch.reserved != 0U ||
        batch.first_sequence > UINT64_MAX - batch.count) {
        target->rejected.fetch_add(batch.count, std::memory_order_relaxed);
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (batch.channel_id != target->config.channel_id) {
        target->rejected.fetch_add(batch.count, std::memory_order_relaxed);
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    CanMessageBatch pending = batch;
    std::uint64_t fallback_ns = 0U;
    for (std::size_t index = 0; index < pending.count; ++index) {
        if (const core::Status valid =
                validate_message(*target, pending.messages[index], fallback_ns);
            !valid) {
            target->rejected.fetch_add(batch.count, std::memory_order_relaxed);
            return valid;
        }
    }
    if (!target->ring.try_push(pending)) {
        target->dropped.fetch_add(batch.count, std::memory_order_relaxed);
        return std::unexpected(core::ErrorCode::BufferFull);
    }
    return {};
}

std::uint64_t CanTransmitter::dropped() const noexcept {
    std::uint64_t total = 0U;
    for (const auto& channel : channels_) {
        total += channel->dropped.load(std::memory_order_relaxed);
    }
    return total;
}

std::uint64_t CanTransmitter::rejected() const noexcept {
    std::uint64_t total = 0U;
    for (const auto& channel : channels_) {
        total += channel->rejected.load(std::memory_order_relaxed);
    }
    return total;
}

std::uint64_t CanTransmitter::failed() const noexcept {
    std::uint64_t total = 0U;
    for (const auto& channel : channels_) {
        total += channel->failed.load(std::memory_order_relaxed);
    }
    return total;
}

void CanTransmitter::submit_packet(Channel& channel, CanMessageBatch& packet) noexcept {
    if (packet.count == 0U) {
        return;
    }
    core::Status status = core::Status{};
    if (packet.count == 1U) {
        status = driver_->submit(channel.config.name, packet.messages[0]);
        for (int attempt = 0; !status.has_value() &&
                              (status.error() == core::ErrorCode::BufferFull ||
                               status.error() == core::ErrorCode::SystemError) &&
                              attempt < 8;
             ++attempt) {
            std::this_thread::yield();
            status = driver_->submit(channel.config.name, packet.messages[0]);
        }
    } else {
        status = driver_->submit_batch(channel.config.name, packet);
    }
    if (status.has_value()) {
        submitted_.fetch_add(packet.count, std::memory_order_relaxed);
    } else {
        channel.failed.fetch_add(packet.count, std::memory_order_relaxed);
        if (status.error() == core::ErrorCode::SystemError) {
            channel.tracker.set_state(CanChannelState::Warning);
            publish_status(channel);
        }
    }
}

void CanTransmitter::submit_loop(Channel& channel, std::stop_token token) noexcept {
    CanMessageBatch packet{};
    while (!token.stop_requested()) {
        if (!channel.ring.try_pop(packet)) {
            std::this_thread::sleep_for(std::chrono::microseconds{100});
            continue;
        }
        submit_packet(channel, packet);
    }
}

void CanTransmitter::TrackingSink::on_message(const CanMessage& message) noexcept {
    owner_->on_driver_message(message);
}

} // namespace nexigen::hw::can
