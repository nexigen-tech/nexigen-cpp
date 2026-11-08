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
// to poll is not free. The loop therefore does only a few failed sweeps
// (WorkerTuning::spin_budget) before sleeping, and the sleep grows
// geometrically (100 us, 200 us, ...) up to WorkerTuning::poll_interval_us
// (LoggerOptions::worker_poll_interval_us, default 1 ms). Right after
// activity the worker therefore reacts within ~100 us (bursts still fit the
// shard rings); a long-idle worker wakes only once per poll interval, which
// is what keeps many pinned loggers from saturating a shared core with
// context switches.
//
// (Policy implemented by core::WorkerTuning, shared with the tracing consumer.)
//
// TIMER SLACK: the worker raises its own timer slack to half the poll
// interval (never below the kernel's 50 us default). Idle sleeps of many
// loggers pinned to one core then expire inside shared windows and are
// coalesced by the kernel into one wake-up burst instead of N separate ones.
// Slack only delays the worker's own wake-ups; producers never wait.
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
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/worker_tuning.hpp>
#include <nexigen/logging/detail/affinity.hpp>

namespace nexigen::logging::detail {

/// Idle-behaviour knobs live in core (shared with the tracing consumer).
using core::WorkerTuning;

/// Consumer thread + drain loop + flush handshake over a Sink interface.
/// Sink contract (duck-typed): core::Status start() noexcept;
/// void handle(const Producer::record_type&) noexcept;
/// core::Status synchronize() noexcept; void stop() noexcept.
template <class Producer, class Sink> class AsyncWorker {
    using record_type = typename Producer::record_type;

    static constexpr std::uint64_t flush_sleep_budget = 50'000; ///< * 200 us ~ 10 s escape hatch

    /// Flush sequencing + last synchronize error: one private cache line.
    struct alignas(64) FlushCell {
        std::atomic<std::uint64_t> requested{}; ///< flushing threads fetch_add (release)
        std::atomic<std::uint64_t> drained{};   ///< worker store after sync (release)
        std::atomic<std::uint8_t> sync_error{}; ///< ErrorCode+1, 0 == clean
        std::uint8_t pad_[47]{};
    } flush_{};

    /// Worker start state: one line, read by initialize-time pollers only.
    struct alignas(64) StartCell {
        std::atomic<bool> launched{};            ///< set (release) once the jthread exists
        std::atomic<bool> started{};             ///< set (release) after the start sequence ran
        std::atomic<std::uint8_t> code{};        ///< first start-phase ErrorCode snapshot
        std::atomic<std::uint64_t> mask{};       ///< post-pin affinity as the worker sees it
        core::ThreadApplyReport thread_report{}; ///< written before started (release)
    } start_{};

    Producer* producer_{};           ///< owned by the logger; stable address
    core::ThreadSpec thread_spec_{}; ///< copied by start() before the thread exists
    WorkerTuning tuning_{};          ///< set_tuning() before start(); read by the worker only
    std::optional<Sink> sink_{};     ///< constructed by start()
    std::jthread thread_{};          ///< LAST member: joins before sink_/producer_ die

    void run(std::stop_token st) noexcept {
        // Start sequence ON the worker thread (design D15): self-pin first so
        // no scheduling window can run the body off-core; snapshot the
        // resulting mask for third-thread verification; then the sink's cold
        // start (fd/rotation/open). First error wins the code snapshot; a
        // failure never prevents the logging loop from running.
        std::uint8_t first_error = 0U;
        const core::Status attributes =
            core::apply_thread_spec_self(thread_spec_, start_.thread_report);
        first_error = static_cast<std::uint8_t>(start_.thread_report.first_error());
        if (!attributes && thread_spec_.apply_mode() == core::ThreadApplyMode::Required) {
            start_.code.store(first_error, std::memory_order_relaxed);
            start_.started.store(true, std::memory_order_release);
            return; // required failure: no sink start and no main loop
        }
        if (!sink_.has_value()) {
            return;
        }
        Sink& sink = *sink_;
        const std::uint64_t poll_max_us = WorkerTuning::clamp_poll_us(tuning_.poll_interval_us);
        const std::uint64_t spin_budget = tuning_.spin_budget;
        WorkerTuning::apply_timer_slack(poll_max_us);

        std::uint64_t mask_now = 0ULL;
        static_cast<void>(self_affinity(mask_now));
        start_.mask.store(mask_now, std::memory_order_relaxed);

        const core::Status started = sink.start();
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
                sink.handle(rec);
                ++n;
            }
            if (n != 0) {
                idle = 0;
                continue;
            }
            // Ring empty at this instant: answer any flush request before
            // going idle. (All records accepted before the request were in
            // the ring we just observed empty.) The flush-time drop summary
            // (spec logging/multi-producer "Drop summaries are synthesized
            // by the worker") is built HERE, on the consumer thread, and
            // handed straight to the sink: no ring push, no foreign-thread
            // producer work — a shard's ring is pushed only by its owner.
            const std::uint64_t req = flush_.requested.load(std::memory_order_acquire);
            if (req > flush_.drained.load(std::memory_order_relaxed)) {
                record_type summary{};
                if (producer_->synthesize_flush_summary(summary)) {
                    sink.handle(summary);
                }
                publish_sync(req);
            }
            if (++idle <= spin_budget) {
                continue;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(
                WorkerTuning::backoff_us(idle - spin_budget, poll_max_us)));
        }

        // Shutdown: drain what is still queued, synchronize once, publish,
        // then let the sink close (spec: "Destruction does not lose accepted
        // records" — the join happens before any logger member dies).
        while (producer_->pop(rec)) {
            sink.handle(rec);
        }
        publish_sync(flush_.requested.load(std::memory_order_relaxed));
        sink.stop();
    }

    void publish_sync(std::uint64_t seq) noexcept {
        if (!sink_.has_value()) {
            return;
        }
        Sink& sink = *sink_;
        const core::Status sync = sink.synchronize();
        flush_.sync_error.store(
            sync ? std::uint8_t{0}
                 : static_cast<std::uint8_t>(static_cast<std::uint8_t>(sync.error()) + 1U),
            std::memory_order_relaxed);
        flush_.drained.store(seq, std::memory_order_release);
    }

public:
    AsyncWorker() noexcept = default;
    AsyncWorker(const AsyncWorker&) = delete;
    AsyncWorker& operator=(const AsyncWorker&) = delete;
    AsyncWorker(AsyncWorker&&) = delete;
    AsyncWorker& operator=(AsyncWorker&&) = delete;

    /// Idle-behaviour knobs. Must be called BEFORE start() (the worker reads
    /// them once, on its own thread, after thread creation).
    void set_tuning(const WorkerTuning& tuning) noexcept { tuning_ = tuning; }

    /// Start the background consumer. The Producer must outlive the worker
    /// (it is a member of the same logger object). thread_spec is applied by
    /// the worker itself (empty = inherited attributes). Sink is constructed in place from
    /// sink_args. Returns OK once the thread exists; the pin/sink start results land in the
    /// snapshot cell (writer_started()/writer_code()/writer_mask()).
    template <class... Args>
        requires std::constructible_from<Sink, Args...>
    [[nodiscard]] core::Status start(Producer& producer, const core::ThreadSpec& thread_spec,
                                     Args&&... sink_args) noexcept {
        const core::Status valid = thread_spec.validate();
        if (!valid) {
            return valid;
        }
        producer_ = &producer;
        thread_spec_ = thread_spec;
        sink_.emplace(std::forward<Args>(sink_args)...);
        thread_ = std::jthread([this](std::stop_token st) { run(st); });
        start_.launched.store(true, std::memory_order_release);
        if (thread_spec_.apply_mode() == core::ThreadApplyMode::Required) {
            while (!start_.started.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            const core::ErrorCode attribute_error = start_.thread_report.first_error();
            if (attribute_error != core::ErrorCode::Ok) {
                thread_.request_stop();
                thread_.join();
                sink_.reset(); // sink.start() never ran on this path
                start_.launched.store(false, std::memory_order_release);
                start_.started.store(false, std::memory_order_release);
                return std::unexpected(attribute_error);
            }
        }
        return {};
    }

    /// Control-plane drain barrier (spec: outside the never-block guarantee).
    /// Waits until everything enqueued before this call is sink-synchronized.
    /// The flush-time drop summary is synthesized by the worker when it
    /// answers this barrier (this thread never touches foreign shards:
    /// spec logging/multi-producer "Drop summaries are synthesized by the
    /// worker").
    [[nodiscard]] core::Status flush() noexcept {
        if (!start_.launched.load(std::memory_order_acquire)) {
            return std::unexpected(core::ErrorCode::NotInitialized); // not started yet
        }
        const std::uint64_t seq = flush_.requested.fetch_add(1U, std::memory_order_release) + 1U;
        for (std::uint64_t spins = 0; flush_.drained.load(std::memory_order_acquire) < seq;
             ++spins) {
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
    /// Immutable thread-attribute application report, valid after writer_started().
    [[nodiscard]] const core::ThreadApplyReport& writer_thread_report() const noexcept {
        return start_.thread_report;
    }
    /// Post-start affinity mask as the worker itself observed it (design D15
    /// verification surface: the test compares this against the configured
    /// mask instead of reaching into std::jthread handles).
    [[nodiscard]] std::uint64_t writer_mask() const noexcept {
        return start_.mask.load(std::memory_order_relaxed);
    }
};

} // namespace nexigen::logging::detail
