// nexigen-logview — live log-service capture thread.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/waitset.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/logging/log_frame.hpp>

#include "ingress_record.hpp"

namespace nexigen::logview {

/// @brief Snapshot of the capture thread's counters.
struct CaptureStats {
    std::uint64_t received{0};   ///< Frames accepted into the hand-off queue.
    std::uint64_t rejected{0};   ///< Frames with an unsupported wire version.
    std::uint64_t overflow{0};   ///< Frames the hand-off queue could not retain.
    std::uint32_t publishers{0}; ///< Distinct publisher ids observed.
    std::size_t ingress_size{0}; ///< Current hand-off queue occupancy.
    int last_error{0};           ///< Last transport error, or zero.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the counters.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic mirror of `CaptureStats::to_string`.
/// @param stats Stats to render.
/// @return The same text.
[[nodiscard]] inline std::string to_string(const CaptureStats& stats) {
    return stats.to_string();
}

/// @brief Owns the iceoryx2 node, subscriber, event listener, and WaitSet.
///
/// The constructor prepares nothing; `start()` opens every endpoint and
/// launches a low-priority receiver. The receiver pushes `IngressRecord`s into
/// a fixed 65 536-slot SPSC queue; the UI thread is the sole consumer. A
/// transport error is recorded and the thread keeps trying instead of tearing
/// down the view.
class CaptureThread {
public:
    /// @brief Hand-off queue type shared with the model.
    using IngressQueue = nexigen::core::SpscRingBuffer<IngressRecord, 65536>;

    /// @brief Construct an idle capture endpoint.
    /// @param service Log service name.
    /// @param spec Optional low-priority/affinity specification.
    CaptureThread(std::string service, nexigen::core::ThreadSpec spec);
    /// @brief Stop and join the receiver.
    ~CaptureThread();

    /// @brief Copying is disabled: the endpoint owns transport handles.
    CaptureThread(const CaptureThread&) = delete;
    /// @brief Copy assignment is disabled.
    CaptureThread& operator=(const CaptureThread&) = delete;

    /// @brief Open the service and start the receiver thread.
    /// @return `Status{}` on success.
    [[nodiscard]] nexigen::core::Status start() noexcept;
    /// @brief Request stop, join the receiver, and release every endpoint.
    void stop() noexcept;

    /// @brief Hand-off queue.
    /// @return Mutable reference.
    [[nodiscard]] IngressQueue& ingress() noexcept { return ingress_; }
    /// @brief Capture counters.
    /// @return Snapshot.
    [[nodiscard]] CaptureStats stats() const noexcept;
    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the current counters.
    [[nodiscard]] std::string to_string() const { return stats().to_string(); }

    /// @brief Inject one receive failure for the transport-error test.
    ///
    /// The next drain records a transport error and returns without consuming
    /// a sample; the following drain resumes normally.
    void inject_receive_error_for_tests() noexcept {
        fail_next_receive_for_tests_.store(true, std::memory_order_release);
    }

private:
    /// @brief Receiver thread body.
    void run(std::stop_token token) noexcept;
    /// @brief Drain every queued sample into the hand-off queue.
    void drain() noexcept;
    /// @brief Remember a publisher id.
    void observe_publisher(std::uint64_t publisher_id) noexcept;

    std::string service_;                      ///< Log service name.
    nexigen::core::ThreadSpec spec_{};         ///< Thread placement request.
    std::optional<nexigen::com::Node> node_{}; ///< Transport node.
    std::optional<
        nexigen::com::Subscriber<nexigen::logging::LogFrame, nexigen::com::TransportHeader>>
        subscriber_{};                                           ///< Log subscriber.
    std::optional<nexigen::com::EventListener> listener_{};      ///< Notification listener.
    std::optional<nexigen::com::WaitSet> waitset_{};             ///< Multiplexed wait.
    std::optional<nexigen::com::WaitSetGuard> event_guard_{};    ///< Listener attachment.
    std::optional<nexigen::com::WaitSetGuard> interval_guard_{}; ///< Fallback timer.
    std::jthread worker_{};                                      ///< Receiver thread.
    IngressQueue ingress_{};                                     ///< Hand-off queue.

    std::atomic<std::uint64_t> received_{0}; ///< Accepted frames.
    std::atomic<std::uint64_t> rejected_{0}; ///< Rejected wire versions.
    std::atomic<std::uint64_t> overflow_{0}; ///< Hand-off overflows.
    std::atomic<int> last_error_{0};         ///< Last transport error.
    std::atomic<bool> stopping_{false};      ///< Stop flag independent of the stop token.
    std::atomic<bool> fail_next_receive_for_tests_{false};       ///< Test-only receive failure.
    std::array<std::atomic<std::uint64_t>, 16> publisher_ids_{}; ///< Observed publishers.
    std::atomic<std::uint32_t> publisher_count_{0};              ///< Distinct publisher count.
};

/// @brief Free diagnostic mirror of `CaptureThread::stats`.
/// @param thread Capture endpoint.
/// @return Stats snapshot text.
[[nodiscard]] inline std::string to_string(const CaptureThread& thread) {
    return thread.stats().to_string();
}

} // namespace nexigen::logview
