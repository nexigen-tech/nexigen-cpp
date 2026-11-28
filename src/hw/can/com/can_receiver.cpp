#include <nexigen/hw/can/com/can_receiver.hpp>

#include <utility>

namespace nexigen::hw::can {

CanReceiver::CanReceiver(CanDriver& driver, std::span<const CanChannelConfig> configs,
                         std::span<com::EventProducer<CanMessageBatch>*> producers,
                         std::span<com::EventProducer<CanChannelStatus>*> status_producers) noexcept
    : driver_(&driver) {
    const std::size_t count = configs.size() < producers.size() ? configs.size() : producers.size();
    channels_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto channel = std::make_unique<Channel>();
        channel->config = configs[i];
        channel->producer = producers[i];
        if (i < status_producers.size()) {
            channel->status_producer = status_producers[i];
        }
        channels_.push_back(std::move(channel));
    }
}

core::Status CanReceiver::start() noexcept {
    if (driver_ == nullptr) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    for (auto& channel : channels_) {
        if (const core::Status opened = driver_->open_channel(channel->config, *this); !opened) {
            stop();
            return opened;
        }
        channel->opened = true;
        workers_.emplace_back(
            [this, raw = channel.get()](std::stop_token token) { publish_loop(*raw, token); });
    }
    return {};
}

void CanReceiver::stop() noexcept {
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

void CanReceiver::on_message(const CanMessage& message) noexcept {
    for (auto& channel : channels_) {
        if (channel->config.channel_id != message.channel_id) {
            continue;
        }
        if (!channel->ring.try_push(message)) {
            if (channel->config.drop_oldest) {
                CanMessage discarded{};
                static_cast<void>(channel->ring.try_pop(discarded));
                if (channel->ring.try_push(message)) {
                    return;
                }
            }
            channel->dropped.fetch_add(1U, std::memory_order_relaxed);
        }
        return;
    }
}

void CanReceiver::flush_batch(Channel& channel, CanMessageBatch& batch) noexcept {
    if (batch.count == 0U || channel.producer == nullptr) {
        return;
    }
    const CanMessage& first = batch.messages[0];
    const auto outcome =
        channel.producer->publish(batch, event_rx, event_time(first), hardware_metadata(first));
    if (outcome.has_value()) {
        published_.fetch_add(batch.count, std::memory_order_relaxed);
    } else {
        channel.failed.fetch_add(batch.count, std::memory_order_relaxed);
    }
    batch.clear();
    batch.channel_id = channel.config.channel_id;
}

void CanReceiver::publish_loop(Channel& channel, std::stop_token token) noexcept {
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
        channel.tracker.observe(message);
        if (channel.status_producer != nullptr) {
            const bool state_changed = channel.tracker.consume_state_changed();
            const std::uint64_t now = message.hardware_timestamp_ns;
            const std::uint64_t interval = channel.config.status_interval_ns;
            if (state_changed || interval == 0U || now - channel.last_status_ns >= interval) {
                channel.last_status_ns = now;
                static_cast<void>(channel.status_producer->publish(
                    channel.tracker.status(), event_status,
                    com::EventTime{now, com::TimestampDomain::Hardware}, com::HardwareMetadata{}));
            }
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

} // namespace nexigen::hw::can
