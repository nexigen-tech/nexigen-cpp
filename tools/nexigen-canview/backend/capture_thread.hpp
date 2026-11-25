// nexigen-canview — live CAN capture thread.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nexigen/com/event_consumer.hpp>
#include <nexigen/com/event_consumer_set.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/com/loss_info.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_message.hpp>

#include "anchor_estimator.hpp"
#include "can_ingress_record.hpp"
#include "capture_stats.hpp"
#include "can_queues.hpp"
#include "model/sequence_tracker.hpp"
#include "model/view_channel.hpp"

namespace nexigen::canview {

/// @brief Capture endpoint for up to eight live CAN channels.
///
/// One `canview-rx` thread owns one `com::Node`, one `EventConsumerSet<16>`
/// and eight `rx`/`status` consumer pairs. Callbacks only check the channel
/// binding, update sequence/anchor state and push into fixed SPSC queues; they
/// never allocate, format, validate externally or block.
class CaptureThread {
public:
    /// @brief Maximum live channels; more is a configuration error.
    static constexpr std::size_t max_live_channels{8U};

    /// @brief Construct an idle endpoint.
    /// @param channels Enabled channel profiles.
    /// @param spec Optional thread placement request (`canview-rx`, nice 10).
    CaptureThread(std::vector<CanViewChannel> channels, nexigen::core::ThreadSpec spec = {});
    /// @brief Stop and join the receiver.
    ~CaptureThread();

    CaptureThread(const CaptureThread&) = delete;
    CaptureThread& operator=(const CaptureThread&) = delete;

    /// @brief Validate profiles, open services and start the receiver.
    /// @return `Status{}` when at least one channel started; otherwise the
    ///         configuration or transport failure.
    [[nodiscard]] nexigen::core::Status start() noexcept;
    /// @brief Request stop, join the receiver, and release every endpoint.
    void stop() noexcept;

    /// @brief Whether the receiver thread is running.
    /// @return `true` between `start()` and `stop()`.
    [[nodiscard]] bool running() const noexcept { return worker_.joinable(); }

    /// @brief Hand-off queue for frames and loss markers.
    /// @return Mutable reference.
    [[nodiscard]] IngressQueue& ingress() noexcept { return *ingress_; }
    /// @brief Channel-status queue.
    /// @return Mutable reference.
    [[nodiscard]] StatusQueue& statuses() noexcept { return *statuses_; }
    /// @brief UTC anchor queue.
    /// @return Mutable reference.
    [[nodiscard]] AnchorQueue& anchors() noexcept { return *anchors_; }

    /// @brief Capture counters.
    /// @return Snapshot.
    [[nodiscard]] CaptureStats stats() const noexcept;

    /// @brief Consume the accumulated ingress-overflow count.
    /// @return Number of rejected pushes since the previous call.
    [[nodiscard]] std::uint64_t take_pending_overflow() noexcept {
        return pending_overflow_.exchange(0, std::memory_order_acq_rel);
    }

    /// @brief Last validation/start diagnostic.
    /// @return Text set by a failed `start()`.
    [[nodiscard]] const std::string& last_failure() const noexcept { return last_failure_; }

    /// @brief Inject a start failure for one channel name (tests only).
    /// @param name Channel profile that fails `start()`.
    void fail_channel_for_tests(std::string name) noexcept {
        fail_channel_for_tests_ = std::move(name);
    }
    /// @brief Clear the injected channel failure (tests only).
    void clear_channel_failure_for_tests() noexcept { fail_channel_for_tests_.clear(); }

    /// @brief Invoke the frame callback directly (tests only).
    /// @param header Transport header.
    /// @param message Frame to ingest.
    /// @param index Channel slot.
    void ingest_for_tests(const nexigen::com::EventHeader& header,
                          const nexigen::hw::can::CanMessage& message, std::size_t index = 0U) {
        on_message(index, header, message);
    }

    /// @brief Invoke the loss callback directly (tests only).
    /// @param info Transport loss.
    /// @param index Channel slot.
    void loss_for_tests(const nexigen::com::LossInfo& info, std::size_t index = 0U) {
        on_loss(index, info);
    }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the current counters.
    [[nodiscard]] std::string to_string() const { return stats().to_string(); }

private:
    using MessageConsumer = nexigen::com::EventConsumer<
        nexigen::hw::can::CanMessage,
        std::function<void(const nexigen::com::EventHeader&, const nexigen::hw::can::CanMessage&)>,
        std::function<void(const nexigen::com::LossInfo&)>>;
    using StatusConsumer =
        nexigen::com::EventConsumer<nexigen::hw::can::CanChannelStatus,
                                    std::function<void(const nexigen::com::EventHeader&,
                                                       const nexigen::hw::can::CanChannelStatus&)>,
                                    std::function<void(const nexigen::com::LossInfo&)>>;

    /// @brief Per-channel runtime state.
    struct ChannelState {
        CanViewChannel profile{};            ///< Configured profile.
        SequenceTracker sequence{};          ///< Hardware-sequence bookkeeping.
        AnchorEstimator anchor{};            ///< UTC anchor estimator.
        std::uint64_t last_dropped_count{0}; ///< Previous status dropped counter.
        std::uint64_t last_sequence_gap{0};  ///< Previous status sequence-gap counter.
        bool status_seen{false};             ///< Whether a status sample arrived.
        bool frames_have_sequence{false};    ///< Any frame carried a hardware sequence.
    };

    /// @brief Receiver thread body.
    void run(std::stop_token token) noexcept;
    /// @brief Validate names, ids and the live limit before any service exists.
    /// @return `Status{}`, or `InvalidArgument` with `last_failure_` set.
    [[nodiscard]] nexigen::core::Status validate_profiles();
    /// @brief Create and attach both consumers for one channel.
    /// @return `true` when the channel started.
    [[nodiscard]] bool start_channel(std::size_t index) noexcept;
    /// @brief Frame callback.
    void on_message(std::size_t index, const nexigen::com::EventHeader& header,
                    const nexigen::hw::can::CanMessage& message) noexcept;
    /// @brief Channel-status callback.
    void on_status(std::size_t index, const nexigen::com::EventHeader& header,
                   const nexigen::hw::can::CanChannelStatus& status) noexcept;
    /// @brief Transport-loss callback shared by both streams.
    void on_loss(std::size_t index, const nexigen::com::LossInfo& info) noexcept;
    /// @brief Push a synthetic loss marker.
    void push_marker(std::size_t index, CanRecordKind kind, GapReason reason, std::uint64_t count,
                     bool count_exact, std::uint64_t timestamp_ns) noexcept;
    /// @brief Push one ingress record, counting overflow.
    /// @return `true` when the queue accepted the record.
    [[nodiscard]] bool push_record(const CanIngressRecord& record) noexcept;

    std::vector<CanViewChannel> profiles_;                    ///< Configured channels.
    nexigen::core::ThreadSpec spec_{};                        ///< Thread placement request.
    std::optional<nexigen::com::Node> node_{};                ///< Transport node.
    std::optional<nexigen::com::EventConsumerSet<16>> set_{}; ///< Multiplexer.
    std::array<std::optional<MessageConsumer>, max_live_channels> message_consumers_{};
    std::array<std::optional<StatusConsumer>, max_live_channels> status_consumers_{};
    std::array<ChannelState, max_live_channels> channels_{};
    std::jthread worker_{}; ///< Receiver thread.

    // Queues live on the heap: the ingress ring alone is 8 MiB, which must not
    // consume the caller's stack.
    std::unique_ptr<IngressQueue> ingress_{std::make_unique<IngressQueue>()}; ///< Frames.
    std::unique_ptr<StatusQueue> statuses_{std::make_unique<StatusQueue>()};  ///< Statuses.
    std::unique_ptr<AnchorQueue> anchors_{std::make_unique<AnchorQueue>()};   ///< Anchors.

    std::atomic<std::uint64_t> received_{0};
    std::atomic<std::uint64_t> bus_idle_{0};
    std::atomic<std::uint64_t> synthetic_{0};
    std::atomic<std::uint64_t> overflow_{0};
    std::atomic<std::uint64_t> pending_overflow_{0};
    std::atomic<std::uint64_t> mismatched_{0};
    std::atomic<std::uint64_t> duplicates_{0};
    std::atomic<std::uint64_t> resets_{0};
    std::atomic<std::uint64_t> hardware_gaps_{0};
    std::atomic<std::uint64_t> transport_losses_{0};
    std::atomic<std::uint64_t> ingress_losses_{0};
    std::atomic<std::uint64_t> driver_drops_{0};
    std::atomic<int> last_error_{0};
    std::atomic<bool> stopping_{false};
    std::uint32_t active_channels_{0};
    std::uint32_t failed_channels_{0};
    std::string last_failure_{};
    std::string fail_channel_for_tests_{}; ///< Test-only start failure.
};

/// @brief Free diagnostic mirror of `CaptureThread::stats`.
/// @param thread Capture endpoint.
/// @return The same text.
[[nodiscard]] inline std::string to_string(const CaptureThread& thread) {
    return thread.stats().to_string();
}

} // namespace nexigen::canview
