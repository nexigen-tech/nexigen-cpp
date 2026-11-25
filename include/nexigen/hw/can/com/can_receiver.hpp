#pragma once

#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nexigen/com/event_producer.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_channel_config.hpp>
#include <nexigen/hw/can/can_driver.hpp>
#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_channel_tracker.hpp>
#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/hw/can/can_message_batch.hpp>
#include <nexigen/hw/can/com/can_transport.hpp>
#include <nexigen/hw/can/detail/can_message_ring.hpp>

namespace nexigen::hw::can {

/// @brief Driver-to-iceoryx2 bridge for a set of channels.
///
/// The driver callback only copies into a bounded SPSC ring; one worker per
/// channel publishes from that ring.
class CanReceiver final : public ICanMessageSink {
public:
    /// @brief Bind a driver, configurations and App-owned producers.
    /// @param driver Driver that must outlive the receiver.
    /// @param configs Channel configurations.
    /// @param producers One message producer per configuration, in the same order.
    /// @param status_producers Optional status producers in the same order.
    CanReceiver(CanDriver& driver, std::span<const CanChannelConfig> configs,
                std::span<com::EventProducer<CanMessageBatch>*> producers,
                std::span<com::EventProducer<CanChannelStatus>*> status_producers = {}) noexcept;

    /// @brief Open every channel and start one publish worker per channel.
    /// @return OK, or the first open/thread failure.
    [[nodiscard]] core::Status start() noexcept;

    /// @brief Stop workers and close every opened channel.
    void stop() noexcept;

    /// @brief Driver callback; copies into the channel ring.
    /// @param message Driver message.
    void on_message(const CanMessage& message) noexcept override;

    /// @brief Render the receiver for cold-path diagnostics.
    /// @return Human-readable summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("CanReceiver(channels={}, published={})", channels_.size(),
                           published_.load(std::memory_order_relaxed));
    }

private:
    struct Channel {
        CanChannelConfig config{};
        com::EventProducer<CanMessageBatch>* producer{nullptr};
        com::EventProducer<CanChannelStatus>* status_producer{nullptr};
        detail::CanMessageRing<4096> ring{};
        CanChannelTracker tracker{};
        std::uint64_t last_status_ns{0};
        std::atomic<std::uint64_t> dropped{0};
        std::atomic<std::uint64_t> failed{0};
        std::uint64_t next_sequence{0};
        bool opened{false};
    };

    void publish_loop(Channel& channel, std::stop_token token) noexcept;
    void flush_batch(Channel& channel, CanMessageBatch& batch) noexcept;

    CanDriver* driver_{nullptr};
    std::vector<std::unique_ptr<Channel>> channels_{};
    std::vector<std::jthread> workers_{};
    std::atomic<std::uint64_t> published_{0};
};

/// @brief Free helper mirroring `CanReceiver::to_string`.
/// @param receiver Receiver to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const CanReceiver& receiver) {
    return receiver.to_string();
}

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanReceiver`.
template <>
struct std::formatter<nexigen::hw::can::CanReceiver> : std::formatter<std::string_view> {
    /// @brief Format the receiver through `to_string`.
    /// @tparam Ctx Format context.
    /// @param receiver Receiver to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx>
    auto format(const nexigen::hw::can::CanReceiver& receiver, Ctx& ctx) const {
        const std::string text = receiver.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
