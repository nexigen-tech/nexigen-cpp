// nexigen::logging — asynchronous wait-free JSON-lines file logger (design.md
// D8-D15, spec logging/file-logger).
//
// Shape: the producer hot path is the shared detail::Producer (level filter →
// shard slot → stamp → throttle window → cached tid → pack → try_push on the
// thread's OWN shard — never blocking, never allocating, bounded steps).
// Everything expensive and OS-touching — UTC calibration, DEFERRED FORMAT
// RENDERING, JSON escaping, directory rotation, fsync, and CPU affinity —
// lives on one background std::jthread (detail::AsyncWorker) that pins
// itself first thing (worker_thread) and then round-robin drains ALL
// producer shards in batches into "<base_dir>/<period-label>/<file_name>".
//
// Drop story (spec): over-throttle -> rate-limited counter; ring-full ->
// buffer-full counter; no shard slot -> slot-exhausted counter (all counted,
// none returned). On the next window transition with pending drops the
// producer synthesizes a Warning record reporting the exact per-cause counts
// (design D9) — it costs one bounded push per window at most, and never
// re-tries in a loop.
//
// CONTRACT (spec logging/multi-producer): up to MaxThreads distinct producer
// threads per instance (per-thread SPSC shards; the internal worker is the
// sole consumer of all of them). level()/dropped()/pending()/writer_*() are
// third-thread safe — relaxed SNAPSHOTS, diagnostics only. flush() is the
// control-plane barrier (drain ALL shards + offset re-sample + fsync),
// outside the never-block guarantee that binds the record methods.
//
// Result usage rule: branch on has_value(), never call .value().
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/detail/async_worker.hpp>
#include <nexigen/logging/detail/file_sink.hpp>
#include <nexigen/logging/detail/format_param.hpp>
#include <nexigen/logging/detail/producer.hpp>
#include <nexigen/logging/dropped_stats.hpp>
#include <nexigen/logging/file_logger_config.hpp>
#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/rotation_schedule.hpp>
#include <nexigen/logging/settings.hpp>

namespace nexigen::logging {

/// Asynchronous wait-free JSON-lines file logger supporting multiple
/// producer threads via per-thread SPSC shards (spec logging/multi-producer).
template <std::size_t ShardCapacity = 128, std::size_t MessageCapacity = 128,
          std::size_t MaxThreads = 32>
class FileLogger {
    using Producer = detail::Producer<ShardCapacity, MessageCapacity, MaxThreads>;

    Producer producer_{};
    detail::AsyncWorker<Producer, detail::FileSink> worker_{};

public:
    FileLogger() noexcept = default;
    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;
    FileLogger(FileLogger&&) = delete;
    FileLogger& operator=(FileLogger&&) = delete;

    /// Per-shard capacity; aggregate pool capacity is capacity()*max_threads().
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept { return MaxThreads; }

    /// Initialization phase (pre real-time): validate config, copy it into
    /// fixed storage, calibrate clocks, start the pinned writer. Invalid
    /// configuration returns InvalidArgument WITHOUT starting the worker.
    [[nodiscard]] core::Status initialize(const FileLoggerConfig& cfg) noexcept {
        if (!valid_config(cfg)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        const core::Status valid_thread = cfg.worker_thread.validate();
        if (!valid_thread) {
            return valid_thread;
        }

        detail::FileSink::Settings settings{};
        copy_into(cfg.app_name, settings.app, settings.app_len);
        copy_into(cfg.base_dir, settings.base_dir, settings.base_len);
        copy_into(cfg.file_name, settings.file_name, settings.file_name_len);
        settings.kind = cfg.rotation.kind;
        settings.custom_ns = cfg.rotation.custom_ns;
        settings.max_file_size_bytes = cfg.max_file_size_bytes;

        producer_.set_level(cfg.initial_level);
        producer_.configure_throttle(cfg.throttle);
        const core::Status calibrated = producer_.initialize(); // per-slot calibrate, cold
        if (!calibrated) {
            return calibrated;
        }
        worker_.set_tuning(detail::WorkerTuning{.poll_interval_us = cfg.worker_poll_interval_us});
        return worker_.start(producer_, cfg.worker_thread, settings);
    }

    /// Initialize from merged global/local settings (spec logging/logging-settings
    /// "Merged settings apply to logger configuration"). Runs the explicit
    /// capacity check and maps every supported option; string views inside the
    /// temporary config alias `settings` for the duration of this call only.
    [[nodiscard]] core::Status initialize(const Settings& settings) noexcept {
        const core::Status capacity =
            check_capacity<ShardCapacity, MaxThreads, MessageCapacity>(settings);
        if (!capacity) {
            return capacity;
        }
        FileLoggerConfig cfg{};
        const core::Status applied = apply_to(settings, cfg);
        if (!applied) {
            return applied;
        }
        return initialize(cfg);
    }

    void set_level(LogLevel new_level) noexcept { producer_.set_level(new_level); }
    [[nodiscard]] LogLevel level() const noexcept { return producer_.level(); }

    /// Variadic surface (spec logging/logger-concept): pack + shard enqueue,
    /// never blocks, never formats, returns void; drops count per cause.
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

    /// Control-plane: drain all shards, re-sample the UTC offset, fsync,
    /// surface accumulated write/fsync failures (spec: SystemError).
    [[nodiscard]] core::Status flush() noexcept { return worker_.flush(); }

    /// Drop accounting snapshot (third-thread safe).
    [[nodiscard]] DroppedStats dropped() const noexcept { return producer_.dropped(); }
    [[nodiscard]] std::size_t pending() const noexcept { return producer_.ring_size(); }

    /// Writer start-phase snapshots (design D15 verification surface):
    /// poll writer_started(), then inspect the thread report and start ErrorCode.
    [[nodiscard]] bool writer_started() const noexcept { return worker_.writer_started(); }
    [[nodiscard]] core::ErrorCode writer_start_code() const noexcept {
        return worker_.writer_start_code();
    }
    [[nodiscard]] const core::ThreadApplyReport& writer_thread_report() const noexcept {
        return worker_.writer_thread_report();
    }
    [[nodiscard]] std::uint64_t writer_mask() const noexcept { return worker_.writer_mask(); }

    /// Cold-path diagnostic (mandate 9): threshold + shard-pool snapshot
    /// (capacity, pool size, pending, per-cause drops, writer state).
    [[nodiscard]] std::string to_string() const {
        return "FileLogger(level=" + std::string(logging::to_string(producer_.level())) +
               ", shard_capacity=" + std::to_string(ShardCapacity) +
               ", shards=" + std::to_string(MaxThreads) +
               ", message_capacity=" + std::to_string(MessageCapacity) +
               ", pending=" + std::to_string(producer_.ring_size()) +
               ", dropped=" + producer_.dropped().to_string() +
               ", writer_started=" + std::string(worker_.writer_started() ? "true" : "false") + ")";
    }

private:
    /// Non-empty bounded text with no embedded NUL (directories/file names).
    [[nodiscard]] static bool valid_path(std::string_view value, std::size_t max) noexcept {
        return !value.empty() && value.size() <= max && value.find('\0') == std::string_view::npos;
    }

    /// Bounded text with no embedded NUL; emptiness is allowed.
    [[nodiscard]] static bool valid_optional_text(std::string_view value,
                                                  std::size_t max) noexcept {
        return value.size() <= max && value.find('\0') == std::string_view::npos;
    }

    /// The complete configuration gate; every rejection maps to InvalidArgument.
    [[nodiscard]] static bool valid_config(const FileLoggerConfig& cfg) noexcept {
        const bool throttle_ok =
            !cfg.throttle.enabled ||
            (cfg.throttle.max_records_per_window != 0ULL && cfg.throttle.window_ns != 0ULL);
        const bool rotation_ok =
            cfg.rotation.kind != RotationSchedule::Custom || cfg.rotation.custom_ns != 0ULL;
        return valid_path(cfg.base_dir, 511U) && valid_path(cfg.file_name, 63U) &&
               valid_optional_text(cfg.app_name, 63U) && throttle_ok && rotation_ok;
    }

    template <std::size_t N>
    static void copy_into(std::string_view src, char (&dst)[N], std::size_t& len_out) noexcept {
        const std::size_t take = src.size() < N ? src.size() : N - 1U;
        if (take > 0) {
            std::memcpy(dst, src.data(), take); // NOLINT(bugprone-suspicious-stringview-data-usage)
        }
        dst[take] = '\0';
        len_out = take;
    }
};

/// Free helper (mandate 9): mirrors the member.
template <std::size_t ShardCapacity, std::size_t MessageCapacity, std::size_t MaxThreads>
[[nodiscard]] std::string
to_string(const FileLogger<ShardCapacity, MessageCapacity, MaxThreads>& logger) {
    return logger.to_string();
}

static_assert(concepts::Logger<FileLogger<>>,
              "FileLogger must satisfy the Logger contract (pinned at header level)");

} // namespace nexigen::logging
