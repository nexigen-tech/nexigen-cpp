// nexigen::synchro — deterministic fault hooks for the synchro test target
// (change add-synchro-event-sync).
//
// The production build compiles these helpers to no-ops and the fault struct
// does not exist. The `nexigen_synchro_tests` target defines
// `NEXIGEN_SYNCHRO_TESTING`, exactly like `NEXIGEN_EVENT_SYSTEM_TESTING` does
// for the event-system tests, so error-mapping branches stay reachable in tests
// without altering the hot path.
#pragma once

#include <cstdint>

#if defined(NEXIGEN_SYNCHRO_TESTING)
#include <atomic>
#include <chrono>
#include <thread>

#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace nexigen::synchro::detail {

#if defined(NEXIGEN_SYNCHRO_TESTING)

/// @brief Deterministic synchro faults, compiled only into the test target.
///
/// Every flag is atomic because tests arm faults from the main thread while the
/// generator worker reads them.
struct SyncFaults {
    static inline std::atomic<bool> fail_eventfd{
        false}; ///< Make eventfd creation fail with `EMFILE`.
    static inline std::atomic<bool> fail_clock{
        false}; ///< Make every raw clock read fail with `EIO`.
    static inline std::atomic<bool> fail_notify{
        false}; ///< Report `NotifyFailed` after a successful send.
    static inline std::atomic<bool> fail_send{
        false}; ///< Reject every publication as undeliverable.

    /// One-shot worker stall in nanoseconds, consumed by the next publication
    /// slot. Asserting it produces one deterministic overrun episode.
    static inline std::atomic<std::uint64_t> stall_once_ns{0U};
    /// Observed `EINTR` returns of the generator's `ppoll` wait.
    static inline std::atomic<std::uint64_t> eintr_hits{0U};
    /// Entered coarse `ppoll` sleeps.
    static inline std::atomic<std::uint64_t> coarse_waits{0U};
    /// Final-window spin iterations.
    static inline std::atomic<std::uint64_t> spin_iterations{0U};
    /// Thread id of the first publication.
    static inline std::atomic<long> publish_tid{0};
    /// Set when a publication is observed from a second thread.
    static inline std::atomic<bool> publish_tid_changed{false};

    /// @brief Clear every injected fault and observation counter.
    static void reset() noexcept {
        fail_eventfd.store(false, std::memory_order_relaxed);
        fail_clock.store(false, std::memory_order_relaxed);
        fail_notify.store(false, std::memory_order_relaxed);
        fail_send.store(false, std::memory_order_relaxed);
        stall_once_ns.store(0U, std::memory_order_relaxed);
        eintr_hits.store(0U, std::memory_order_relaxed);
        coarse_waits.store(0U, std::memory_order_relaxed);
        spin_iterations.store(0U, std::memory_order_relaxed);
        publish_tid.store(0, std::memory_order_relaxed);
        publish_tid_changed.store(false, std::memory_order_relaxed);
    }
};

/// @brief Whether an eventfd creation failure is armed.
/// @return Armed flag; always `false` outside the test target.
[[nodiscard]] inline bool eventfd_failure_armed() noexcept {
    return SyncFaults::fail_eventfd.load(std::memory_order_relaxed);
}

/// @brief Whether a clock-read failure is armed.
/// @return Armed flag; always `false` outside the test target.
[[nodiscard]] inline bool clock_failure_armed() noexcept {
    return SyncFaults::fail_clock.load(std::memory_order_relaxed);
}

/// @brief Whether a notification failure is armed.
/// @return Armed flag; always `false` outside the test target.
[[nodiscard]] inline bool notify_failure_armed() noexcept {
    return SyncFaults::fail_notify.load(std::memory_order_relaxed);
}

/// @brief Whether a publication send failure is armed.
/// @return Armed flag; always `false` outside the test target.
[[nodiscard]] inline bool send_failure_armed() noexcept {
    return SyncFaults::fail_send.load(std::memory_order_relaxed);
}

/// @brief Sleep for the one-shot worker stall, if one was requested.
///
/// Called by the generator worker after a deadline wait and before it samples
/// the publication time, so the stale-phase classification observes the delay.
inline void apply_worker_stall() noexcept {
    const std::uint64_t stall_ns =
        SyncFaults::stall_once_ns.exchange(0U, std::memory_order_relaxed);
    if (stall_ns != 0U) {
        std::this_thread::sleep_for(std::chrono::nanoseconds{stall_ns});
    }
}

/// @brief Record one `EINTR` return of the coarse wait.
inline void note_eintr() noexcept {
    SyncFaults::eintr_hits.fetch_add(1U, std::memory_order_relaxed);
}

/// @brief Number of observed `EINTR` returns.
/// @return Counter value; always zero in production builds.
[[nodiscard]] inline std::uint64_t eintr_count() noexcept {
    return SyncFaults::eintr_hits.load(std::memory_order_relaxed);
}

/// @brief Record one coarse `ppoll` sleep.
inline void note_coarse_wait() noexcept {
    SyncFaults::coarse_waits.fetch_add(1U, std::memory_order_relaxed);
}

/// @brief Number of coarse `ppoll` sleeps seen by the generator.
/// @return Counter value.
[[nodiscard]] inline std::uint64_t coarse_wait_count() noexcept {
    return SyncFaults::coarse_waits.load(std::memory_order_relaxed);
}

/// @brief Record one final-window spin iteration.
inline void note_spin() noexcept {
    SyncFaults::spin_iterations.fetch_add(1U, std::memory_order_relaxed);
}

/// @brief Number of final-window spin iterations seen by the generator.
/// @return Counter value.
[[nodiscard]] inline std::uint64_t spin_count() noexcept {
    return SyncFaults::spin_iterations.load(std::memory_order_relaxed);
}

/// @brief Record the thread that performed one transport publication.
///
/// The first call stores the thread id; a later call from another thread raises
/// `publish_thread_changed()`.
inline void note_publish_thread() noexcept {
    const auto tid = ::syscall(SYS_gettid);
    long expected = 0;
    if (SyncFaults::publish_tid.compare_exchange_strong(expected, tid, std::memory_order_relaxed)) {
        return;
    }
    if (expected != tid) {
        SyncFaults::publish_tid_changed.store(true, std::memory_order_relaxed);
    }
}

/// @brief Thread id of the first observed publication.
/// @return Numeric thread id, or zero when nothing was published.
[[nodiscard]] inline long published_thread_id() noexcept {
    return SyncFaults::publish_tid.load(std::memory_order_relaxed);
}

/// @brief Whether publications came from more than one thread.
/// @return `true` when a second publishing thread was observed.
[[nodiscard]] inline bool publish_thread_changed() noexcept {
    return SyncFaults::publish_tid_changed.load(std::memory_order_relaxed);
}

#else

/// @brief Whether an eventfd creation failure is armed.
/// @return Always `false` in production builds.
[[nodiscard]] inline bool eventfd_failure_armed() noexcept {
    return false;
}

/// @brief Whether a clock-read failure is armed.
/// @return Always `false` in production builds.
[[nodiscard]] inline bool clock_failure_armed() noexcept {
    return false;
}

/// @brief Whether a notification failure is armed.
/// @return Always `false` in production builds.
[[nodiscard]] inline bool notify_failure_armed() noexcept {
    return false;
}

/// @brief Whether a publication send failure is armed.
/// @return Always `false` in production builds.
[[nodiscard]] inline bool send_failure_armed() noexcept {
    return false;
}

/// @brief One-shot worker stall hook; no work in production builds.
inline void apply_worker_stall() noexcept {}

/// @brief `EINTR` accounting hook; no work in production builds.
inline void note_eintr() noexcept {}

/// @brief Number of observed `EINTR` returns.
/// @return Always zero in production builds.
[[nodiscard]] inline std::uint64_t eintr_count() noexcept {
    return 0U;
}

/// @brief Coarse-wait accounting hook; no work in production builds.
inline void note_coarse_wait() noexcept {}

/// @brief Number of coarse `ppoll` sleeps.
/// @return Always zero in production builds.
[[nodiscard]] inline std::uint64_t coarse_wait_count() noexcept {
    return 0U;
}

/// @brief Spin accounting hook; no work in production builds.
inline void note_spin() noexcept {}

/// @brief Number of final-window spin iterations.
/// @return Always zero in production builds.
[[nodiscard]] inline std::uint64_t spin_count() noexcept {
    return 0U;
}

/// @brief Publication-thread accounting hook; no work in production builds.
inline void note_publish_thread() noexcept {}

/// @brief Thread id of the first observed publication.
/// @return Always zero in production builds.
[[nodiscard]] inline long published_thread_id() noexcept {
    return 0;
}

/// @brief Whether publications came from more than one thread.
/// @return Always `false` in production builds.
[[nodiscard]] inline bool publish_thread_changed() noexcept {
    return false;
}

#endif

} // namespace nexigen::synchro::detail
