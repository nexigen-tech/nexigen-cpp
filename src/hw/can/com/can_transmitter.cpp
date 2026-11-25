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
    Channel* target = find(channel);
    if (target == nullptr) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    CanMessage normalized = message;
    if (core::Status valid = normalized.validate(); !valid) {
        return valid;
    }
    if (normalized.kind == CanMessageKind::Error || normalized.kind == CanMessageKind::Overload ||
        normalized.kind == CanMessageKind::BusIdle) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (normalized.channel_id != target->config.channel_id) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (normalized.kind == CanMessageKind::FdData && !target->config.fd_enabled) {
        return std::unexpected(core::ErrorCode::UnsupportedFormat);
    }
    if (normalized.hardware_timestamp_ns == 0U) {
        const auto now = nexigen::chrono::Timestamp::now();
        if (!now) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        normalized.hardware_timestamp_ns = now->nanoseconds();
        normalized.timestamp_source = CanTimestampSource::Simulated;
    }
    if (normalized.origin == CanMessageOrigin::Unknown) {
        normalized.origin = CanMessageOrigin::LocalTransmit;
    }
    if (!target->ring.try_push(normalized)) {
        target->dropped.fetch_add(1U, std::memory_order_relaxed);
        return std::unexpected(core::ErrorCode::BufferFull);
    }
    return {};
}

void CanTransmitter::flush_batch(Channel& channel, CanMessageBatch& batch) noexcept {
    if (batch.count == 0U) {
        return;
    }
    core::Status status = core::Status{};
    if (batch.count == 1U) {
        status = driver_->submit(channel.config.name, batch.messages[0]);
        for (int attempt = 0; !status.has_value() &&
                              (status.error() == core::ErrorCode::BufferFull ||
                               status.error() == core::ErrorCode::SystemError) &&
                              attempt < 8;
             ++attempt) {
            std::this_thread::yield();
            status = driver_->submit(channel.config.name, batch.messages[0]);
        }
    } else {
        status = driver_->submit_batch(channel.config.name, batch);
    }
    if (status.has_value()) {
        submitted_.fetch_add(batch.count, std::memory_order_relaxed);
    } else {
        channel.failed.fetch_add(batch.count, std::memory_order_relaxed);
        if (status.error() == core::ErrorCode::SystemError) {
            channel.tracker.set_state(CanChannelState::Warning);
            publish_status(channel);
        }
    }
    batch.clear();
    batch.channel_id = channel.config.channel_id;
}

void CanTransmitter::submit_loop(Channel& channel, std::stop_token token) noexcept {
    CanMessage message{};
    CanMessageBatch batch{};
    batch.channel_id = channel.config.channel_id;
    auto batch_started = std::chrono::steady_clock::now();
    constexpr auto flush_interval = std::chrono::milliseconds{10};
    while (!token.stop_requested()) {
        if (!channel.ring.try_pop(message)) {
            if (batch.count > 0U &&
                std::chrono::steady_clock::now() - batch_started >= flush_interval) {
                flush_batch(channel, batch);
            }
            std::this_thread::sleep_for(std::chrono::microseconds{100});
            continue;
        }
        if (batch.count == 0U) {
            batch_started = std::chrono::steady_clock::now();
            batch.first_sequence = channel.next_sequence;
        }
        batch.messages[batch.count++] = message;
        ++channel.next_sequence;
        if (batch.count == CanMessageBatch::max_messages) {
            flush_batch(channel, batch);
        }
    }
    flush_batch(channel, batch);
}

void CanTransmitter::TrackingSink::on_message(const CanMessage& message) noexcept {
    owner_->on_driver_message(message);
}

} // namespace nexigen::hw::can
