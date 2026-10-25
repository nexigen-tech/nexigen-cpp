// nexigen::logging — background consumer machinery (design D8; multi-producer
// rework: change add-serialization-and-wait-free-logging design D7).
//
// One std::jthread per async logger. Responsibilities, all OFF the producers'
// hot paths: batch-drain ALL producer shards (detail::Producer::pop
// round-robins across registered shards, ≤ 16 records per shard per sweep,
// up to 64 records per batch), render each record's deferred format into the
// sink's pre-allocated scratch, hand the line to the Sink (console write /
// JSON file write), answer flush() requests, and drain + synchronize +
// publish on shutdown.
//
// MULTI-PRODUCER CONTRACT: producers never wait on this thread; the sweep
// fairness bound in pop() prevents one flooded shard from starving others.
// flush()'s re-sweep runs until EVERY registered shard is momentarily empty
// before publishing the drained sequence, so "all records accepted before
// the request are durable" holds across the whole pool.
//
// IDLE BACKOFF: a consumer that busy-polls empty rings re-reads producers'
// tail cache lines every iteration and slows them down through false sharing
// — the never-block guarantee is the producers', and burning a pinned core
// to poll is not free. The loop therefore does ≤ 64 failed sweeps before
// switching to 200 us sleeps (bounded control-plane latency on the first
// record after idle; the producers never wait).
//
// FLUSH HANDSHAKE (two atomics in one padded cell, relaxed/acquire/release
// as noted; the wait loop is control plane, so sleeping is allowed):
//   requested: fetch_add(release) by the flushing thread — the release edge
//              publishes everything it had enqueued before asking;
//   drained:   store(release) by the worker AFTER writes+fsync complete for
//              all requests up to that sequence; acquire-load by the waiter;
//   sync_error: last synchronize() result code (or 0), stored relaxed before
//              the drained publication it explains.
// flush() is therefore "all records accepted before this call are durable",
// and it surfaces file-layer failures as ErrorCode::SystemError (spec
// logging/file-logger "Flush surfaces write failure").
//
// OWNERSHIP: start() takes a reference to the logger's Producer (stable:
// loggers are non-movable by design) and constructs the Sink in place.
// Destruction order matters: the jthread member is declared last so it is
// destroyed first — std::jthread requests stop + joins, and the final drain
// in run() still sees the producer and sink alive.
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <thread>
#include <utility>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/detail/affinity.hpp>

namespace nexigen::logging::detail {

/// Consumer thread + drain loop + flush handshake over a Sink interface.
/// Sink contract (duck-typed): core::Status start() noexcept;
/// void handle(const Producer::record_type&) noexcept;
/// core::Status synchronize() noexcept; void stop() noexcept.
template <class Producer, class Sink>
class Async_worker {
    using record_type = typename Producer::record_type;

    static constexpr std::uint64_t idle_spin_budget = 64;    ///< failed pops before sleeping
    static constexpr std::uint64_t flush_sleep_budget = 50'000; ///< * 200 us ~ 10 s escape hatch

    /// Flush sequencing + last synchronize error: one private cache line.
    struct alignas(64) Flush_cell {
        std::atomic<std::uint64_t> requested{}; ///< flushing threads fetch_add (release)
        std::atomic<std::uint64_t> drained{};   ///< worker store after sync (release)
        std::atomic<std::uint8_t> sync_error{}; ///< ErrorCode+1, 0 == clean
        std::uint8_t pad_[47]{};
    } flush_{};

    /// Worker start state: one line, read by initialize-time pollers only.
    struct alignas(64) Start_cell {
        std::atomic<bool> launched{};            ///< set (release) once the jthread exists
        std::atomic<bool> started{};             ///< set (release) after the start sequence ran
        std::atomic<std::uint8_t> code{};        ///< first start-phase ErrorCode snapshot
        std::atomic<std::uint64_t> mask{};       ///< post-pin affinity as the worker sees it
        std::uint8_t pad_[48]{};
    } start_{};

    Producer* producer_{};             ///< owned by the logger; stable address
    std::uint64_t core_mask_ = 0;      ///< set by start() before the thread exists
    std::optional<Sink> sink_{};       ///< constructed by start()
    std::jthread thread_{};            ///< LAST member: joins before sink_/producer_ die

    void run(std::stop_token st) noexcept {
        // Start sequence ON the worker thread (design D15): self-pin first so
        // no scheduling window can run the body off-core; snapshot the
        // resulting mask for third-thread verification; then the sink's cold
        // start (fd/rotation/open). First error wins the code snapshot; a
        // failure never prevents the logging loop from running.
        std::uint8_t first_error = 0U;
        if (core_mask_ != 0ULL) {
            const core::Status pinned = pin_self(core_mask_);
            if (!pinned) {
                first_error = static_cast<std::uint8_t>(pinned.error());
            }
        }
        std::uint64_t mask_now = 0ULL;
        static_cast<void>(self_affinity(mask_now));
        start_.mask.store(mask_now, std::memory_order_relaxed);

        const core::Status started = sink_->start();
        if (!started && first_error == 0U) {
            first_error = static_cast<std::uint8_t>(started.error());
        }
        start_.code.store(first_error, std::memory_order_relaxed);
        start_.started.store(true, std::memory_order_release);

        record_type rec{};
        std::uint64_t idle = 0;
        while (!st.stop_requested()) {
            std::size_t n = 0;
            while (n < 64 && producer_->pop(rec)) { // bounded batch
                sink_->handle(rec);
                ++n;
            }
            if (n != 0) {
                idle = 0;
                continue;
            }
            // Ring empty at this instant: answer any flush request before
            // going idle. (All records accepted before the request were in
            // the ring we just observed empty.)
            const std::uint64_t req = flush_.requested.load(std::memory_order_acquire);
            if (req > flush_.drained.load(std::memory_order_relaxed)) {
                publish_sync(req);
            }
            if (++idle < idle_spin_budget) {
                continue;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }

        // Shutdown: drain what is still queued, synchronize once, publish,
        // then let the sink close (spec: "Destruction does not lose accepted
        // records" — the join happens before any logger member dies).
        while (producer_->pop(rec)) {
            sink_->handle(rec);
        }
        publish_sync(flush_.requested.load(std::memory_order_relaxed));
        sink_->stop();
    }

    void publish_sync(std::uint64_t seq) noexcept {
        const core::Status sync = sink_->synchronize();
        flush_.sync_error.store(
            sync ? std::uint8_t{0}
                 : static_cast<std::uint8_t>(static_cast<std::uint8_t>(sync.error()) + 1U),
            std::memory_order_relaxed);
        flush_.drained.store(seq, std::memory_order_release);
    }

public:
    Async_worker() noexcept = default;
    Async_worker(const Async_worker&) = delete;
    Async_worker& operator=(const Async_worker&) = delete;
    Async_worker(Async_worker&&) = delete;
    Async_worker& operator=(Async_worker&&) = delete;

    /// Start the background consumer. The Producer must outlive the worker
    /// (it is a member of the same logger object). core_mask pins the worker
    /// (0 = leave OS placement). Sink is constructed in place from sink_args.
    /// Returns OK once the thread exists; the pin/sink start results land in
    /// the snapshot cell (writer_started()/writer_code()/writer_mask()).
    template <class... Args>
        requires std::constructible_from<Sink, Args...>
    [[nodiscard]] core::Status start(Producer& producer, std::uint64_t core_mask,
                                     Args&&... sink_args) noexcept {
        producer_ = &producer;
        core_mask_ = core_mask;
        sink_.emplace(std::forward<Args>(sink_args)...);
        thread_ = std::jthread([this](std::stop_token st) { run(st); });
        start_.launched.store(true, std::memory_order_release);
        return {};
    }

    /// Control-plane drain barrier (spec: outside the never-block guarantee).
    /// Waits until everything enqueued before this call is sink-synchronized.
    [[nodiscard]] core::Status flush() noexcept {
        if (!start_.launched.load(std::memory_order_acquire)) {
            return std::unexpected(core::ErrorCode::NotInitialized); // not started yet
        }
        const std::uint64_t seq =
            flush_.requested.fetch_add(1U, std::memory_order_release) + 1U;
        for (std::uint64_t spins = 0;
             flush_.drained.load(std::memory_order_acquire) < seq; ++spins) {
            if (spins >= flush_sleep_budget) { // worker gone/stuck: escape hatch
                return std::unexpected(core::ErrorCode::SystemError);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        const std::uint8_t err = flush_.sync_error.load(std::memory_order_acquire);
        if (err != 0U) {
            return std::unexpected(static_cast<core::ErrorCode>(err - 1U));
        }
        return {};
    }

    /// Pollable start snapshot for initialize-time validation/tests.
    [[nodiscard]] bool writer_started() const noexcept {
        return start_.started.load(std::memory_order_acquire);
    }
    [[nodiscard]] core::ErrorCode writer_start_code() const noexcept {
        return static_cast<core::ErrorCode>(start_.code.load(std::memory_order_relaxed));
    }
    /// Post-start affinity mask as the worker itself observed it (design D15
    /// verification surface: the test compares this against the configured
    /// mask instead of reaching into std::jthread handles).
    [[nodiscard]] std::uint64_t writer_mask() const noexcept {
        return start_.mask.load(std::memory_order_relaxed);
    }
};

} // namespace nexigen::logging::detail
