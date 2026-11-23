#pragma once

#include <atomic>
#include <thread>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/com/event_producer.hpp>
#include <nexigen/com/publish_outcome.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/hw/can/com/can_transport.hpp>
#include <nexigen/hw/can/detail/can_message_ring.hpp>

namespace nexigen::hw::can {

/// @brief Cold-path publication counters for one CAN publisher.
struct CanPublisherStats {
    std::uint64_t sent{0};   ///< Successfully published messages.
    std::uint64_t failed{0}; ///< Rejected or failed publications.
};

/// @brief Borrowed CAN publisher for one channel.
class CanPublisher {
public:
    /// @brief Bind a borrowed producer and channel id.
    /// @param producer App-owned producer for the channel tx service.
    /// @param channel_id Numeric channel binding.
    CanPublisher(com::EventProducer<CanMessage>* producer, std::uint16_t channel_id) noexcept
        : producer_(producer), channel_id_(channel_id) {
        worker_ = std::jthread([this](std::stop_token token) { publish_loop(token); });
    }

    /// @brief Stop the publish worker.
    ~CanPublisher() {
        worker_.request_stop();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    CanPublisher(const CanPublisher&) = delete;
    CanPublisher& operator=(const CanPublisher&) = delete;
    CanPublisher(CanPublisher&&) = delete;
    CanPublisher& operator=(CanPublisher&&) = delete;

    /// @brief Enqueue one transmission request.
    ///
    /// Publication is asynchronous: a successful return means the message was
    /// accepted by the bounded publisher queue, not that the transport has
    /// already sent it. The worker counts actual send failures in `stats()`.
    ///
    /// @param message Message to publish; a zero timestamp is filled with host time.
    /// @return `Notified` when enqueued, `BufferFull` when the queue is full, or
    ///         a validation/time error.
    [[nodiscard]] core::Result<com::PublishOutcome> send(const CanMessage& message) noexcept {
        if (producer_ == nullptr) {
            failed_.fetch_add(1U, std::memory_order_relaxed);
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        CanMessage outgoing = message;
        if (outgoing.channel_id != channel_id_) {
            failed_.fetch_add(1U, std::memory_order_relaxed);
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (core::Status valid = outgoing.validate(); !valid) {
            failed_.fetch_add(1U, std::memory_order_relaxed);
            return std::unexpected(valid.error());
        }
        if (outgoing.kind == CanMessageKind::Error || outgoing.kind == CanMessageKind::Overload ||
            outgoing.kind == CanMessageKind::BusIdle) {
            failed_.fetch_add(1U, std::memory_order_relaxed);
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (outgoing.hardware_timestamp_ns == 0U) {
            const auto now = chrono::Timestamp::now();
            if (!now) {
                failed_.fetch_add(1U, std::memory_order_relaxed);
                return std::unexpected(now.error());
            }
            outgoing.hardware_timestamp_ns = now->nanoseconds();
            outgoing.timestamp_source = CanTimestampSource::Simulated;
            outgoing.origin = CanMessageOrigin::LocalTransmit;
        }
        if (!ring_.try_push(outgoing)) {
            failed_.fetch_add(1U, std::memory_order_relaxed);
            return std::unexpected(core::ErrorCode::BufferFull);
        }
        return com::PublishOutcome::Notified;
    }

    void publish_loop(std::stop_token token) noexcept {
        CanMessage message{};
        while (!token.stop_requested()) {
            if (!ring_.try_pop(message)) {
                std::this_thread::yield();
                continue;
            }
            const auto outcome = producer_->publish(message, event_tx, event_time(message),
                                                    hardware_metadata(message));
            if (outcome.has_value()) {
                sent_.fetch_add(1U, std::memory_order_relaxed);
            } else {
                failed_.fetch_add(1U, std::memory_order_relaxed);
            }
        }
    }

    /// @brief Publication counters.
    /// @return Snapshot of sent and failed counts.
    [[nodiscard]] CanPublisherStats stats() const noexcept {
        return CanPublisherStats{sent_.load(std::memory_order_relaxed),
                                 failed_.load(std::memory_order_relaxed)};
    }

    /// @brief Render the publisher for cold-path diagnostics.
    /// @return Human-readable summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("CanPublisher(channel_id={})", channel_id_);
    }

private:
    com::EventProducer<CanMessage>* producer_{nullptr};
    std::uint16_t channel_id_{0};
    detail::SpscRing<CanMessage, 1024> ring_{};
    std::atomic<std::uint64_t> sent_{0};
    std::atomic<std::uint64_t> failed_{0};
    std::jthread worker_{};
};

/// @brief Free helper mirroring `CanPublisher::to_string`.
/// @param publisher Publisher to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const CanPublisher& publisher) {
    return publisher.to_string();
}

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanPublisher`.
template <>
struct std::formatter<nexigen::hw::can::CanPublisher> : std::formatter<std::string_view> {
    /// @brief Format the publisher through `to_string`.
    /// @tparam Ctx Format context.
    /// @param publisher Publisher to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx>
    auto format(const nexigen::hw::can::CanPublisher& publisher, Ctx& ctx) const {
        const std::string text = publisher.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
