// nexigen::logging — asynchronous console sink (design D13; multi-producer +
// deferred-format rework: change add-serialization-and-wait-free-logging,
// design D6/D7; spec logging/console-logger).
//
// Pipeline: detail::Producer (per-thread wait-free shard writes) +
// detail::AsyncWorker (std::jthread drain loop). Records carry a DEFERRED
// format: the consumer renders each pack into the sink's pre-allocated line
// scratch (std::vformat_to via the record's type-erased callback), formats
// the plain line, and writes it with raw ::write() to fd 1 / fd 2 —
// deliberately NOT std::cout/printf, whose lazy initialization, stream
// buffering, and locale state allocate and can block.
//
// Routing (spec "Stream routing by severity"): Verbose/Debug/Info -> stdout;
// Warning/Error/Fatal -> stderr.
//
// MULTI-PRODUCER CONTRACT (spec logging/multi-producer): up to MaxThreads
// (32 default) distinct producer threads may log to one instance; each thread
// owns one SPSC shard and the internal worker is the sole consumer of all of
// them. set_level()/level()/dropped()/pending()/ring_size() are safe from any
// thread (relaxed snapshots — diagnostics only). flush() is a control-plane
// barrier (drain all shards + publish) and sits outside the never-block
// guarantee that binds the record methods.
//
// DROP SEMANTICS (record methods return void; causes counted per shard and
// aggregated by dropped()): buffer-full when the calling thread's shard is
// full, rate-limited never (console configures no throttle), slot-exhausted
// beyond MaxThreads producers. The console sink renders nothing until
// drained, so drops never surface late in the output stream.
//
// Usage rule (see core/result.hpp): never call .value() on a returned Status.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/console_logger_config.hpp>
#include <nexigen/logging/detail/async_worker.hpp>
#include <nexigen/logging/detail/console_sink.hpp>
#include <nexigen/logging/detail/format_param.hpp>
#include <nexigen/logging/detail/producer.hpp>
#include <nexigen/logging/dropped_stats.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/settings.hpp>

namespace nexigen::logging {

/// @brief Asynchronous wait-free console sink with per-thread SPSC shards.
///
/// Producers only filter, pack and enqueue into their own shard; a single
/// background `std::jthread` (`detail::AsyncWorker`) drains every shard, renders
/// the deferred formats into pre-allocated line scratch and writes raw bytes to
/// fd 1 / fd 2 — deliberately not `std::cout`, whose lazy initialization,
/// buffering and locale state can allocate and block. `Verbose`/`Debug`/`Info`
/// go to stdout, `Warning`/`Error`/`Fatal` to stderr.
///
/// All shard storage and scratch buffers are created during `initialize()`;
/// capacities are fixed for the logger's lifetime and steady-state operation
/// allocates nothing. Non-copyable and non-movable because the shard rings must
/// keep their addresses.
///
/// @tparam ShardCapacity Records per shard ring; must be a power of two (the ring
///         buffer pins it with a `static_assert`), so aggregate capacity is
///         `capacity() * max_threads()`.
/// @tparam MessageCapacity Inline argument-blob size of each `LogRecord`.
/// @tparam MaxThreads Number of per-thread shards; producers beyond it take the
///         `slot-exhausted` drop path.
/// @note Spec `logging/console-logger`, requirement "Asynchronous wait-free
///       console output".
/// @note Spec `logging/multi-producer`, requirement "Pre-allocated per-thread
///       shard array" and requirement "Slot exhaustion drops instead of
///       blocking".
template <std::size_t ShardCapacity = 128, std::size_t MessageCapacity = 128,
          std::size_t MaxThreads = 32>
class ConsoleLogger {
    /// @brief Shard pool and hot-path producer used by this instantiation.
    using Producer = detail::Producer<ShardCapacity, MessageCapacity, MaxThreads>;

    /// @brief Per-thread shards plus the level/throttle control cells.
    Producer producer_{};
    /// @brief Background drain loop; the sole consumer of every shard.
    detail::AsyncWorker<Producer, detail::ConsoleSink> worker_{};

public:
    /// @brief Construction-time configuration accepted by `initialize()`.
    using Config = ConsoleLoggerConfig;

    /// @brief Construct the logger; `initialize()` must run before logging.
    ConsoleLogger() noexcept = default;
    /// @brief Copy construction is deleted; shard rings are address-pinned.
    ConsoleLogger(const ConsoleLogger&) = delete;
    /// @brief Copy assignment is deleted; shard rings are address-pinned.
    ConsoleLogger& operator=(const ConsoleLogger&) = delete;
    /// @brief Move construction is deleted; shard rings are address-pinned.
    ConsoleLogger(ConsoleLogger&&) = delete;
    /// @brief Move assignment is deleted; shard rings are address-pinned.
    ConsoleLogger& operator=(ConsoleLogger&&) = delete;

    /// @brief Report the per-shard ring capacity fixed at compile time.
    ///
    /// @return `ShardCapacity`; multiply by `max_threads()` for the aggregate pool
    ///         capacity.
    /// @note Spec `logging/multi-producer`, requirement "Pre-allocated per-thread
    ///       shard array".
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    /// @brief Report how many producer threads may register a shard.
    ///
    /// @return `MaxThreads`; further threads are dropped and counted as
    ///         `slot_exhausted`.
    /// @note Spec `logging/multi-producer`, requirement "Slot exhaustion drops
    ///       instead of blocking".
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept { return MaxThreads; }

    /// @brief Initialize the sink: install the level, calibrate stamps, start the writer.
    ///
    /// Initialization phase, called before any real-time work: applies
    /// `cfg.initial_level`, calibrates the per-slot stamp sources, validates the
    /// worker thread spec and starts the background writer. No allocations beyond
    /// the `std::jthread` creation internals.
    ///
    /// @param cfg Construction-time configuration; the default is `Info`, an
    ///        inherited worker spec and the default poll ceiling.
    /// @return OK once the writer is running; `ErrorCode::SystemError` if clock
    ///         calibration or thread start fails, `ErrorCode::InvalidArgument` if
    ///         the worker thread spec is invalid. The failure is propagated
    ///         unchanged from the first step that rejects; never inspect it via
    ///         `.value()`.
    /// @note Spec `logging/console-logger`, requirement "Asynchronous wait-free
    ///       console output" and requirement "Console writer accepts a thread
    ///       specification".
    [[nodiscard]] core::Status initialize(Config cfg = Config{}) noexcept {
        producer_.set_level(cfg.initial_level);
        const core::Status calibrated = producer_.initialize();
        if (!calibrated) {
            return calibrated;
        }
        const core::Status valid_thread = cfg.worker_thread.validate();
        if (!valid_thread) {
            return valid_thread;
        }
        worker_.set_tuning(detail::WorkerTuning{.poll_interval_us = cfg.worker_poll_interval_us});
        return worker_.start(producer_, cfg.worker_thread);
    }

    /// @brief Initialize from merged global/local settings.
    ///
    /// Runs the explicit capacity check first, then maps only the options the
    /// console sink supports: the console sink is deliberately unpinned, so the
    /// initial level, worker poll interval and shard/message capacities apply —
    /// legacy file-logger affinity settings are never inherited.
    ///
    /// @param settings Merged settings; read-only views are copied into the config
    ///        before `initialize()` consumes it.
    /// @return OK once the writer is running; `ErrorCode::InvalidArgument` when a
    ///         capacity violates this instantiation, otherwise whatever the
    ///         config overload reports.
    /// @note Spec `logging/console-logger`, requirement "Console writer accepts a
    ///       thread specification", scenario "Default console behavior is
    ///       unchanged".
    /// @note Spec `logging/logging-settings`, requirement "Merged settings apply
    ///       to logger configuration".
    [[nodiscard]] core::Status initialize(const Settings& settings) noexcept {
        const core::Status capacity =
            check_capacity<ShardCapacity, MaxThreads, MessageCapacity>(settings);
        if (!capacity) {
            return capacity;
        }
        Config cfg{};
        cfg.initial_level = settings.options.initial_log_level;
        if (settings.worker_thread_set) {
            cfg.worker_thread = settings.options.worker_thread;
            if (!settings.worker_cpus_set) {
                cfg.worker_thread.set_cpus(core::CpuSet{});
            }
        }
        cfg.worker_poll_interval_us = settings.options.worker_poll_interval_us;
        return initialize(cfg);
    }

    /// @brief Install the runtime threshold in the shared relaxed level cell.
    ///
    /// @param new_level Threshold applied to every producer thread; calls below
    ///        it are filtered silently and never reach a shard.
    /// @note Safe from any thread: a relaxed store, an observation rather than
    ///       synchronization.
    void set_level(core::LogLevel new_level) noexcept { producer_.set_level(new_level); }
    /// @brief Read the active threshold as a third-thread-safe snapshot.
    ///
    /// @return The level last installed through `set_level()`.
    [[nodiscard]] core::LogLevel level() const noexcept { return producer_.level(); }

    /// @brief Explicit-level entry: filter, stamp, pack and enqueue; never blocks.
    ///
    /// The calling thread's own shard receives the record through a single
    /// `try_push`. Nothing is rendered on the producer: the record carries the
    /// format pointer, the caller's location and the packed argument bytes, and
    /// the writer renders later.
    ///
    /// @tparam Args Argument types deduced from the trailing values; each must
    ///         match the literal per `std::format_string<Args...>`.
    /// @param level Severity to record; filtered against the active threshold.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order; the
    ///        variadic fold expands them left to right, so the renderer sees them
    ///        in the same order as the format string.
    /// @note Spec `logging/logger-concept`, requirement "Drop reporting moves to
    ///       snapshots": a full shard drops and is counted, never returned.
    template <class... Args>
    void log(core::LogLevel level, detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(level, fmt, std::forward<Args>(args)...);
    }

    /// @brief Enqueue a `Verbose` record; filtered by the same threshold.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order.
    template <class... Args>
    void verbose(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Verbose, fmt, std::forward<Args>(args)...);
    }
    /// @brief Enqueue a `Debug` record; filtered by the same threshold.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order.
    template <class... Args>
    void debug(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    /// @brief Enqueue an `Info` record; filtered by the same threshold.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order.
    template <class... Args>
    void info(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    /// @brief Enqueue a `Warning` record; filtered by the same threshold.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order.
    template <class... Args>
    void warning(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    /// @brief Enqueue an `Error` record; filtered by the same threshold.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order.
    template <class... Args>
    void error(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    /// @brief Enqueue a `Fatal` record; filtered by the same threshold.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order.
    template <class... Args>
    void fatal(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Fatal, fmt, std::forward<Args>(args)...);
    }

    /// @brief Control-plane barrier: drain every shard and synchronize the streams.
    ///
    /// Returns once every record accepted before the call has been written to its
    /// stream across all shards. Console writes are unbuffered syscalls, so the
    /// sink-level synchronize is trivially complete.
    ///
    /// @return OK when the barrier completed; `ErrorCode::NotInitialized` if the
    ///         writer was never started and `ErrorCode::SystemError` if it is gone
    ///         or stuck. Never inspect it via `.value()`.
    /// @warning Not a hot-path call: it blocks and sits outside the never-block
    ///          guarantee that binds the record methods.
    /// @note Spec `logging/console-logger`, requirement "Flush drains
    ///       asynchronously-accepted records".
    [[nodiscard]] core::Status flush() noexcept { return worker_.flush(); }

    /// @brief Snapshot the aggregated drop counters across every shard.
    ///
    /// @return Per-cause totals (`rate_limited`, `buffer_full`, `slot_exhausted`);
    ///         console configuration installs no throttle, so `rate_limited`
    ///         stays zero.
    /// @note Third-thread diagnostic: a relaxed snapshot, not synchronization.
    /// @note Spec `logging/multi-producer`, requirement "Sharded drop accounting
    ///       without contention", scenario "Snapshot aggregates all shards".
    [[nodiscard]] DroppedStats dropped() const noexcept { return producer_.dropped(); }
    /// @brief Report how many accepted records are still awaiting the writer.
    ///
    /// @return Aggregated shard occupancy at the moment of the call (a snapshot;
    ///         the writer may drain concurrently).
    [[nodiscard]] std::size_t pending() const noexcept { return producer_.ring_size(); }
    /// @brief Test whether the background writer reached its drain loop.
    ///
    /// @return `true` once the worker thread is running.
    /// @note Spec `logging/console-logger`, requirement "Console writer accepts a
    ///       thread specification", scenario "Required failure prevents console
    ///       output": poll this before deciding the writer entered its loop.
    [[nodiscard]] bool writer_started() const noexcept { return worker_.writer_started(); }
    /// @brief Report the first error observed while starting the writer.
    ///
    /// @return `ErrorCode::Ok` when the start phase was clean, otherwise the
    ///         first failure (for example `SystemError` from a required affinity
    ///         request). Meaningful after `writer_started()` reports the phase
    ///         finished.
    [[nodiscard]] core::ErrorCode writer_start_code() const noexcept {
        return worker_.writer_start_code();
    }
    /// @brief Report the detailed outcome of applying the worker thread spec.
    ///
    /// @return Reference to the per-attribute apply report owned by the worker;
    ///         valid for the logger's lifetime.
    /// @note Spec `logging/console-logger`, requirement "Console writer accepts a
    ///       thread specification", scenario "Console writer applies valid
    ///       attributes".
    [[nodiscard]] const core::ThreadApplyReport& writer_thread_report() const noexcept {
        return worker_.writer_thread_report();
    }

    /// @brief Render the logger's state for cold-path diagnostics.
    ///
    /// @return Threshold plus the shard-pool snapshot — shard and message
    ///         capacities, shard count, aggregated pending records and per-cause
    ///         drop totals.
    /// @note Mandate 9 (`to_string`), and spec `logging/console-logger`,
    ///       requirement "Console logger diagnostic strings"; never call this on a
    ///       hot path.
    [[nodiscard]] std::string to_string() const {
        return "ConsoleLogger(level=" + std::string(core::to_string(producer_.level())) +
               ", shard_capacity=" + std::to_string(ShardCapacity) +
               ", shards=" + std::to_string(MaxThreads) +
               ", message_capacity=" + std::to_string(MessageCapacity) +
               ", pending=" + std::to_string(producer_.ring_size()) +
               ", dropped=" + producer_.dropped().to_string() + ")";
    }
};

/// @brief Free helper mirroring `ConsoleLogger::to_string`.
///
/// @tparam ShardCapacity Records per shard ring of the dumped logger.
/// @tparam MessageCapacity Inline argument-blob size of its records.
/// @tparam MaxThreads Number of per-thread shards it owns.
/// @param logger Instance to render; safe to call from a third thread.
/// @return The same text as `logger.to_string()`.
/// @note Mandate 9 (`to_string`).
template <std::size_t ShardCapacity, std::size_t MessageCapacity, std::size_t MaxThreads>
[[nodiscard]] std::string
to_string(const ConsoleLogger<ShardCapacity, MessageCapacity, MaxThreads>& logger) {
    return logger.to_string();
}

static_assert(concepts::Logger<ConsoleLogger<>>,
              "ConsoleLogger must satisfy the Logger contract (pinned at header level)");

} // namespace nexigen::logging
