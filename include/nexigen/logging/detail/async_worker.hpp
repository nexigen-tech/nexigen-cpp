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
#include <nexigen/core/worker_thread.hpp>
#include <nexigen/core/worker_tuning.hpp>
#include <nexigen/logging/detail/affinity.hpp>

namespace nexigen::logging::detail {

/// @brief Idle-behaviour knobs live in core (shared with the tracing consumer).
///
/// Alias exported so logger internals and tests name the same policy type the
/// worker applies: spin budget, geometric sleep ceiling, and timer slack.
using core::WorkerTuning;

/// @brief Consumer thread + drain loop + flush handshake over a Sink interface.
///
/// One instance per async logger, owning one `core::WorkerThread`. Producers
/// never wait on it: the worker drains every registered shard round-robin in
/// bounded batches, renders deferred formats into sink-owned scratch on this
/// thread, and answers flush barriers between sweeps.
///
/// Sink contract (duck-typed): core::Status start() noexcept;
/// void handle(const Producer::record_type&) noexcept;
/// core::Status synchronize() noexcept; void stop() noexcept.
///
/// @tparam Producer Sharded producer type; must expose `record_type`, `pop()`,
///         and `synthesize_flush_summary()`.
/// @tparam Sink Consumer-side sink constructed in place by `start()`; must
///         satisfy the four-call contract above.
/// @note Spec `logging/multi-producer`, requirement "Consumer drains every
///       active shard"; idle policy is `logging/file-logger`, requirement
///       "Background writer uses the shared idle-backoff policy".
template <class Producer, class Sink> class AsyncWorker {
    /// @brief Record type carried by the producer's shards.
    using record_type = typename Producer::record_type;

    /// @brief Upper bound on flush sleep iterations before giving up.
    ///
    /// Each iteration sleeps 200 us, so the escape hatch fires after roughly
    /// 10 s of a worker that is gone or stuck.
    static constexpr std::uint64_t flush_sleep_budget = 50'000; ///< * 200 us ~ 10 s escape hatch

    /// @brief Flush sequencing plus the last synchronize error, on one line.
    ///
    /// A single padded cache line keeps the two sequence counters and the error
    /// byte off the drain loop's data lines. The ordering rules are: request
    /// with `fetch_add(release)`, publish with `store(release)` after
    /// writes+fsync, and read with `acquire`.
    struct alignas(64) FlushCell {
        std::atomic<std::uint64_t> requested{}; ///< flushing threads fetch_add (release)
        std::atomic<std::uint64_t> drained{};   ///< worker store after sync (release)
        std::atomic<std::uint8_t> sync_error{}; ///< ErrorCode+1, 0 == clean
        /// Padding so the cell owns exactly one cache line.
        std::uint8_t pad_[47]{};
    } flush_{};

    Producer* producer_{};       ///< owned by the logger; stable address
    WorkerTuning tuning_{};      ///< set_tuning() before start(); read by the worker only
    std::optional<Sink> sink_{}; ///< constructed by start()

    /// @brief Worker-side hooks installed into `core::WorkerThread`.
    ///
    /// The thread wrapper applies the `ThreadSpec`, then calls `on_start()` and
    /// `run()` on the consumer thread. `self` points back to the owning
    /// `AsyncWorker`, whose lifetime encloses the thread's.
    struct Body {
        /// Owning worker; outlives the thread.
        AsyncWorker* self{nullptr};

        /// @brief Start hook: construct-free sink startup on the worker thread.
        ///
        /// @return The sink's `start()` status, or
        ///         `ErrorCode::NotInitialized` when no sink was emplaced.
        [[nodiscard]] core::Status on_start() noexcept {
            return self->sink_.has_value() ? self->sink_->start()
                                           : std::unexpected(core::ErrorCode::NotInitialized);
        }

        /// @brief Thread entry point: run the drain loop until stop is requested.
        ///
        /// @param stop Stop token supplied by `core::WorkerThread`.
        void run(std::stop_token stop) noexcept { self->run_loop(stop); }
    };

    /// @brief Worker thread; declared LAST so it joins before `sink_`/`producer_` die.
    core::WorkerThread<Body> worker_{}; ///< LAST member: joins before sink_/producer_ die

    /// @brief Consumer body: batch drain, flush answers, idle backoff, shutdown.
    ///
    /// Each sweep pops up to 64 records across all shards (the pool's own
    /// round-robin gives at most 16 per shard), resetting the idle counter on
    /// any progress. When the ring is momentarily empty it answers a pending
    /// flush request — synthesizing at most one drop summary on this thread —
    /// and only then starts sleeping. Sleeps start at the `WorkerTuning` floor
    /// (100 us) and double up to the clamped poll ceiling; the timer slack is
    /// widened once at loop entry so co-located idle workers coalesce
    /// wake-ups. On stop it drains everything still queued, synchronizes once,
    /// publishes the final sequence, and stops the sink.
    ///
    /// @param st Stop token polled once per sweep.
    /// @note Spec `logging/multi-producer`, requirement "Consumer drains every
    ///       active shard" and "Drop summaries are synthesized by the worker";
    ///       idle policy is `logging/console-logger`, requirement "Background
    ///       writer uses the shared idle-backoff policy".
    void run_loop(std::stop_token st) noexcept {
        if (!sink_.has_value()) {
            return;
        }
        Sink& sink = *sink_;
        const std::uint64_t poll_max_us = WorkerTuning::clamp_poll_us(tuning_.poll_interval_us);
        const std::uint64_t spin_budget = tuning_.spin_budget;
        WorkerTuning::apply_timer_slack(poll_max_us);

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

    /// @brief Synchronize the sink and publish the drained sequence.
    ///
    /// Runs on the worker thread only. Calls `sink.synchronize()`, stores the
    /// encoded result (`ErrorCode + 1`, or 0 for OK) with relaxed order, then
    /// publishes `seq` with release so a waiter that acquire-loads it also sees
    /// the error byte and every write the sync covered.
    ///
    /// @param seq Request sequence being satisfied; stored into `flush_.drained`.
    /// @note Spec `logging/file-logger`, requirement "Flush synchronizes the
    ///       file".
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
    /// @brief Default-construct an idle worker with no producer or sink.
    AsyncWorker() noexcept = default;
    /// @brief Copying is deleted: the worker owns a thread and flush cell.
    AsyncWorker(const AsyncWorker&) = delete;
    /// @brief Copy assignment is deleted: the worker owns a thread and flush cell.
    AsyncWorker& operator=(const AsyncWorker&) = delete;
    /// @brief Moving is deleted: `Body` holds a pointer to this object.
    AsyncWorker(AsyncWorker&&) = delete;
    /// @brief Move assignment is deleted: `Body` holds a pointer to this object.
    AsyncWorker& operator=(AsyncWorker&&) = delete;

    /// @brief Install idle-behaviour knobs for the next start.
    ///
    /// Must be called BEFORE `start()`: the worker reads them once, on its own
    /// thread, after thread creation.
    ///
    /// @param tuning Poll ceiling, spin budget and related idle policy.
    /// @note Spec `logging/logger-options`, requirement "LoggerOptions defines
    ///       the engine configuration" (`worker_poll_interval_us` ceiling).
    void set_tuning(const WorkerTuning& tuning) noexcept { tuning_ = tuning; }

    /// @brief Start the background consumer and construct the sink in place.
    ///
    /// Validates the thread spec, stores the producer pointer, emplaces the sink
    /// from `sink_args`, then starts the worker. The producer must outlive the
    /// worker (it is a member of the same logger object). The thread spec is
    /// applied by the worker itself; an empty spec means inherited attributes.
    ///
    /// @tparam Args Sink constructor argument types.
    /// @param producer Producer whose shards this worker drains; stored by
    ///        pointer and not owned.
    /// @param thread_spec Affinity/name/scheduling spec for the worker.
    /// @param sink_args Forwarded to the in-place `Sink` construction.
    /// @return OK once the thread exists; otherwise the thread-spec validation
    ///         error or the worker start error. Pin and sink start results are
    ///         published separately in the snapshot cell
    ///         (`writer_started()`/`writer_start_code()`/`writer_mask()`).
    /// @note Spec `logging/file-logger`, requirement "Background writer CPU core
    ///       affinity"; a failed worker start resets the sink so no half-built
    ///       object survives.
    template <class... Args>
        requires std::constructible_from<Sink, Args...>
    [[nodiscard]] core::Status start(Producer& producer, const core::ThreadSpec& thread_spec,
                                     Args&&... sink_args) noexcept {
        const core::Status valid = thread_spec.validate();
        if (!valid) {
            return valid;
        }
        producer_ = &producer;
        sink_.emplace(std::forward<Args>(sink_args)...);
        const core::Status started = worker_.start(thread_spec, Body{this});
        if (!started) {
            sink_.reset(); // the worker never ran the start hook
            return started;
        }
        return {};
    }

    /// @brief Control-plane drain barrier; waits for sink-synchronized records.
    ///
    /// Increments the request sequence with release (publishing everything this
    /// thread enqueued before asking), then sleeps until the worker's release
    /// store of a `drained` value at least that large is observed with acquire.
    /// Waits at most `flush_sleep_budget` iterations of 200 us. The flush-time
    /// drop summary is synthesized by the worker when it answers this barrier;
    /// this thread never touches a foreign shard.
    ///
    /// @return OK when every record accepted before the call is durable; the
    ///         last synchronize error for a file-layer failure;
    ///         `ErrorCode::NotInitialized` when the worker is not running;
    ///         `ErrorCode::SystemError` when the escape hatch fires.
    /// @warning This is a blocking control-plane call, explicitly outside the
    ///          never-block guarantee that binds producer logging.
    /// @note Spec `logging/multi-producer`, requirement "Drop summaries are
    ///       synthesized by the worker"; failure surfacing is
    ///       `logging/file-logger`, requirement "Flush synchronizes the file".
    [[nodiscard]] core::Status flush() noexcept {
        if (!worker_.joinable()) {
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

    /// @brief Pollable start snapshot: has the worker published readiness?
    ///
    /// @return `true` once the thread exists and the start hook has finished.
    /// @note Spec `logging/file-logger`, requirement "Background writer CPU core
    ///       affinity" (start outcome is observable without touching the thread).
    [[nodiscard]] bool writer_started() const noexcept { return worker_.ready(); }
    /// @brief Start-code snapshot for initialize-time validation and tests.
    ///
    /// @return The worker's published start code (`ErrorCode`), or the default
    ///         code while not started.
    [[nodiscard]] core::ErrorCode writer_start_code() const noexcept {
        return worker_.start_code();
    }
    /// @brief Immutable thread-attribute application report.
    ///
    /// @return Report describing which affinity/scheduler attributes were
    ///         applied; valid after `writer_started()`.
    /// @note Spec `logging/file-logger`, requirement "Background writer CPU core
    ///       affinity" (best-effort failures stay visible).
    [[nodiscard]] const core::ThreadApplyReport& writer_thread_report() const noexcept {
        return worker_.report();
    }
    /// @brief Affinity mask as observed by the worker after startup.
    ///
    /// Verification surface for design D15: tests compare this against the
    /// configured mask instead of reaching into `std::jthread` handles.
    ///
    /// @return Effective CPU mask observed on the worker thread.
    /// @note Spec `logging/file-logger`, requirement "Background writer CPU core
    ///       affinity".
    [[nodiscard]] std::uint64_t writer_mask() const noexcept { return worker_.mask(); }
};

} // namespace nexigen::logging::detail
