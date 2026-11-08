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
#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/settings.hpp>

namespace nexigen::logging {

/// Asynchronous wait-free console logger supporting multiple producer threads
/// via per-thread SPSC shards. Template parameters fix the per-shard capacity,
/// inline message size, and producer pool at compile time (SPSC power-of-two
/// contract enforced by the queue itself).
template <std::size_t ShardCapacity = 128, std::size_t MessageCapacity = 128,
          std::size_t MaxThreads = 32>
class ConsoleLogger {
    using Producer = detail::Producer<ShardCapacity, MessageCapacity, MaxThreads>;

    Producer producer_{};
    detail::AsyncWorker<Producer, detail::ConsoleSink> worker_{};

public:
    using Config = ConsoleLoggerConfig;

    ConsoleLogger() noexcept = default;
    ConsoleLogger(const ConsoleLogger&) = delete;
    ConsoleLogger& operator=(const ConsoleLogger&) = delete;
    ConsoleLogger(ConsoleLogger&&) = delete;
    ConsoleLogger& operator=(ConsoleLogger&&) = delete;

    /// Per-shard capacity; aggregate pool capacity is capacity()*max_threads().
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept { return MaxThreads; }

    /// Initialization phase (pre real-time): calibrate stamps + start the
    /// background writer. No allocations beyond jthread creation internals.
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

    /// Initialize from merged global/local settings (spec logging/logging-settings).
    /// The console sink is deliberately unpinned, so only the initial level,
    /// the worker poll interval and the explicit capacity check apply.
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

    void set_level(LogLevel new_level) noexcept { producer_.set_level(new_level); }
    [[nodiscard]] LogLevel level() const noexcept { return producer_.level(); }

    /// Variadic surface (spec logging/logger-concept): pack + shard enqueue,
    /// never blocks, never formats, returns void.
    template <class... Args>
    void log(LogLevel level, detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(level, fmt, std::forward<Args>(args)...);
    }

    template <class... Args>
    void verbose(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(LogLevel::Verbose, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void debug(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void info(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void warning(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void error(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void fatal(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(LogLevel::Fatal, fmt, std::forward<Args>(args)...);
    }

    /// Control-plane barrier: returns once every record accepted before the
    /// call has been written to its stream across ALL shards (console writes
    /// are unbuffered syscalls, so synchronize() is trivially complete).
    [[nodiscard]] core::Status flush() noexcept { return worker_.flush(); }

    /// Third-thread diagnostics (snapshots, not hot-path synchronization).
    [[nodiscard]] DroppedStats dropped() const noexcept { return producer_.dropped(); }
    [[nodiscard]] std::size_t pending() const noexcept { return producer_.ring_size(); }
    [[nodiscard]] bool writer_started() const noexcept { return worker_.writer_started(); }
    [[nodiscard]] core::ErrorCode writer_start_code() const noexcept {
        return worker_.writer_start_code();
    }
    [[nodiscard]] const core::ThreadApplyReport& writer_thread_report() const noexcept {
        return worker_.writer_thread_report();
    }

    /// Cold-path diagnostic (mandate 9): threshold + shard-pool snapshot
    /// (capacity, registered producers, pending, per-cause drops).
    [[nodiscard]] std::string to_string() const {
        return "ConsoleLogger(level=" + std::string(logging::to_string(producer_.level())) +
               ", shard_capacity=" + std::to_string(ShardCapacity) +
               ", shards=" + std::to_string(MaxThreads) +
               ", message_capacity=" + std::to_string(MessageCapacity) +
               ", pending=" + std::to_string(producer_.ring_size()) +
               ", dropped=" + producer_.dropped().to_string() + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
template <std::size_t ShardCapacity, std::size_t MessageCapacity, std::size_t MaxThreads>
[[nodiscard]] std::string
to_string(const ConsoleLogger<ShardCapacity, MessageCapacity, MaxThreads>& logger) {
    return logger.to_string();
}

static_assert(concepts::Logger<ConsoleLogger<>>,
              "ConsoleLogger must satisfy the Logger contract (pinned at header level)");

} // namespace nexigen::logging
