#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/com/event_producer.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_channel_tracker.hpp>
#include <nexigen/hw/can/can_channel_config.hpp>
#include <nexigen/hw/can/can_driver.hpp>
#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/hw/can/can_message_batch.hpp>
#include <nexigen/hw/can/detail/can_message_ring.hpp>

namespace nexigen::hw::can {

/// @brief iceoryx2-to-driver bridge for a set of channels.
class CanTransmitter {
public:
    /// @brief Bind a driver and channel configurations.
    /// @param driver Driver that must outlive the transmitter.
    /// @param configs Channel configurations.
    /// @param status_producers Optional status producers aligned with `configs`.
    CanTransmitter(CanDriver& driver, std::span<const CanChannelConfig> configs,
                   std::span<com::EventProducer<CanChannelStatus>*> status_producers = {}) noexcept;

    /// @brief Open every channel and start one submit worker per channel.
    /// @return OK, or the first open/thread failure.
    [[nodiscard]] core::Status start() noexcept;

    /// @brief Stop workers and close every opened channel.
    void stop() noexcept;

    /// @brief Enqueue one transmission request.
    /// @param channel Channel name.
    /// @param message Message to submit.
    /// @return OK, `NotFound`, `BufferFull`, `InvalidArgument` or `UnsupportedFormat`.
    [[nodiscard]] core::Status enqueue(std::string_view channel,
                                       const CanMessage& message) noexcept;

    /// @brief Render the transmitter for cold-path diagnostics.
    /// @return Human-readable summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("CanTransmitter(channels={}, submitted={})", channels_.size(),
                           submitted_.load(std::memory_order_relaxed));
    }

private:
    struct Channel {
        CanChannelConfig config{};
        detail::CanMessageRing<1024> ring{};
        com::EventProducer<CanChannelStatus>* status_producer{nullptr};
        CanChannelTracker tracker{};
        std::uint64_t last_status_ns{0};
        std::atomic<std::uint64_t> dropped{0};
        std::atomic<std::uint64_t> failed{0};
        std::uint64_t next_sequence{0};
        bool opened{false};
    };

    class TrackingSink final : public ICanMessageSink {
    public:
        explicit TrackingSink(CanTransmitter& owner) noexcept : owner_(&owner) {}
        void on_message(const CanMessage& message) noexcept override;

    private:
        CanTransmitter* owner_{nullptr};
    };

    void on_driver_message(const CanMessage& message) noexcept;
    void publish_status(Channel& channel) noexcept;
    [[nodiscard]] Channel* find(std::string_view channel) noexcept;

    void submit_loop(Channel& channel, std::stop_token token) noexcept;
    void flush_batch(Channel& channel, CanMessageBatch& batch) noexcept;

    CanDriver* driver_{nullptr};
    TrackingSink tracking_sink_{*this};
    std::vector<std::unique_ptr<Channel>> channels_{};
    std::vector<std::jthread> workers_{};
    std::atomic<std::uint64_t> submitted_{0};
};

/// @brief Free helper mirroring `CanTransmitter::to_string`.
/// @param transmitter Transmitter to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const CanTransmitter& transmitter) {
    return transmitter.to_string();
}

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanTransmitter`.
template <>
struct std::formatter<nexigen::hw::can::CanTransmitter> : std::formatter<std::string_view> {
    /// @brief Format the transmitter through `to_string`.
    /// @tparam Ctx Format context.
    /// @param transmitter Transmitter to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx>
    auto format(const nexigen::hw::can::CanTransmitter& transmitter, Ctx& ctx) const {
        const std::string text = transmitter.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
