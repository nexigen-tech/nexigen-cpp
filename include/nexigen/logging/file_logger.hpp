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
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/rotation_schedule.hpp>
#include <nexigen/logging/settings.hpp>

namespace nexigen::logging {

/// @brief Asynchronous wait-free JSON-lines file sink with per-thread SPSC shards.
///
/// The producer hot path is the shared `detail::Producer`: level filter, shard
/// slot, stamp, throttle window, cached thread id, argument pack, `try_push` on
/// the calling thread's OWN shard — bounded steps, no allocation, no blocking.
/// Everything OS-touching lives on one pinned background `std::jthread`
/// (`detail::AsyncWorker`): UTC calibration, deferred format rendering, JSON
/// escaping, directory/size rotation and `fsync`. Records are appended as one
/// JSON object per line under `"<base_dir>/<period-label>/<file_name>"`.
///
/// Drops are counted per cause and never returned: over-throttle is
/// `rate_limited`, a full ring is `buffer_full`, a missing shard slot is
/// `slot_exhausted`. On the next window transition with pending drops the
/// producer synthesizes one bounded warning record naming the exact counts.
///
/// All ring, path and scratch storage exists after `initialize()`; steady-state
/// operation allocates nothing. Non-copyable and non-movable because the shard
/// rings must keep their addresses.
///
/// @tparam ShardCapacity Records per shard ring; must be a power of two (the ring
///         buffer pins it with a `static_assert`), so aggregate capacity is
///         `capacity() * max_threads()`.
/// @tparam MessageCapacity Inline argument-blob size of each `LogRecord`.
/// @tparam MaxThreads Number of per-thread shards; producers beyond it take the
///         `slot-exhausted` drop path.
/// @note Spec `logging/file-logger`, requirement "Asynchronous wait-free
///       JSON-lines file output".
/// @note Spec `logging/multi-producer`, requirement "Pre-allocated per-thread
///       shard array".
template <std::size_t ShardCapacity = 128, std::size_t MessageCapacity = 128,
          std::size_t MaxThreads = 32>
class FileLogger {
    /// @brief Shard pool and hot-path producer used by this instantiation.
    using Producer = detail::Producer<ShardCapacity, MessageCapacity, MaxThreads>;

    /// @brief Per-thread shards plus the level/throttle control cells.
    Producer producer_{};
    /// @brief Background render/rotate/write loop; sole consumer of every shard.
    detail::AsyncWorker<Producer, detail::FileSink> worker_{};

public:
    /// @brief Construct the logger; `initialize()` must run before logging.
    FileLogger() noexcept = default;
    /// @brief Copy construction is deleted; shard rings are address-pinned.
    FileLogger(const FileLogger&) = delete;
    /// @brief Copy assignment is deleted; shard rings are address-pinned.
    FileLogger& operator=(const FileLogger&) = delete;
    /// @brief Move construction is deleted; shard rings are address-pinned.
    FileLogger(FileLogger&&) = delete;
    /// @brief Move assignment is deleted; shard rings are address-pinned.
    FileLogger& operator=(FileLogger&&) = delete;

    /// @brief Report the per-shard ring capacity fixed at compile time.
    ///
    /// @return `ShardCapacity`; multiply by `max_threads()` for the aggregate pool
    ///         capacity.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    /// @brief Report how many producer threads may register a shard.
    ///
    /// @return `MaxThreads`; further threads are dropped and counted as
    ///         `slot_exhausted`.
    /// @note Spec `logging/multi-producer`, requirement "Slot exhaustion drops
    ///       instead of blocking".
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept { return MaxThreads; }

    /// @brief Initialize from a complete file configuration.
    ///
    /// Initialization phase, called before any real-time work: validates the
    /// configuration, copies the string views into fixed sink storage, installs
    /// the throttle policy, calibrates the clocks and starts the pinned writer.
    /// An invalid configuration returns `InvalidArgument` WITHOUT starting the
    /// worker.
    ///
    /// @param cfg File configuration: application name, base directory, file
    ///        name, level, throttle, rotation schedule, size limit and worker
    ///        thread spec. Views are copied, so `cfg` may die after the call.
    /// @return OK once the writer is running; `ErrorCode::InvalidArgument` for an
    ///         out-of-bounds path/name, a zero throttle window with throttling
    ///         enabled or a custom rotation with a zero period;
    ///         `ErrorCode::SystemError` when clock calibration or thread start
    ///         fails (a `Required` thread attribute rejection also rolls the
    ///         worker back). Never inspect it via `.value()`.
    /// @note Spec `logging/file-logger`, requirement "Background writer CPU core
    ///       affinity", scenario "Required failure rolls back initialization".
    /// @note Spec `logging/file-logger`, requirement "File logger config exposes
    ///       the poll ceiling".
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

    /// @brief Initialize from merged global/local settings.
    ///
    /// Runs the explicit capacity check, maps every supported option into a
    /// temporary `FileLoggerConfig` and delegates to the config overload. String
    /// views inside that temporary alias `settings` only for this call; the sink
    /// copies them into its own fixed buffers.
    ///
    /// @param settings Merged settings produced by the settings layer.
    /// @return OK once the writer is running; `ErrorCode::InvalidArgument` when a
    ///         capacity or mapped value violates the logger's limits, otherwise
    ///         whatever the config overload reports.
    /// @note Spec `logging/logging-settings`, requirement "Merged settings apply
    ///       to logger configuration".
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

    /// @brief Install the runtime threshold in the shared relaxed level cell.
    ///
    /// @param new_level Threshold applied to every producer thread; calls below
    ///        it are filtered silently and consume no shard slot.
    void set_level(core::LogLevel new_level) noexcept { producer_.set_level(new_level); }
    /// @brief Read the active threshold as a third-thread-safe snapshot.
    ///
    /// @return The level last installed through `set_level()`.
    [[nodiscard]] core::LogLevel level() const noexcept { return producer_.level(); }

    /// @brief Explicit-level entry: filter, throttle, pack and enqueue; never blocks.
    ///
    /// Accepted records land in the calling thread's own shard through a single
    /// `try_push` and are rendered as JSON later by the writer. Nothing is
    /// formatted on the producer.
    ///
    /// @tparam Args Argument types deduced from the trailing values; each must
    ///         match the literal per `std::format_string<Args...>`.
    /// @param level Severity to record; filtered against the active threshold.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order; the fold
    ///        expands left to right so the renderer reads them in format order.
    /// @note Spec `logging/file-logger`, requirement "Hot-path rate limiting":
    ///       the excess is dropped immediately and counted per cause.
    template <class... Args>
    void log(core::LogLevel level, detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(level, fmt, std::forward<Args>(args)...);
    }

    /// @brief Enqueue a `Verbose` record subject to level and throttle.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order.
    template <class... Args>
    void verbose(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Verbose, fmt, std::forward<Args>(args)...);
    }
    /// @brief Enqueue a `Debug` record subject to level and throttle.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order.
    template <class... Args>
    void debug(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    /// @brief Enqueue an `Info` record subject to level and throttle.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order.
    template <class... Args>
    void info(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    /// @brief Enqueue a `Warning` record subject to level and throttle.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order.
    template <class... Args>
    void warning(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    /// @brief Enqueue an `Error` record subject to level and throttle.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order.
    template <class... Args>
    void error(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    /// @brief Enqueue a `Fatal` record subject to level and throttle.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order.
    template <class... Args>
    void fatal(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Fatal, fmt, std::forward<Args>(args)...);
    }

    /// @brief Control-plane barrier: drain all shards, re-sample UTC, `fsync`.
    ///
    /// @return OK when every record accepted before the call is written and the
    ///         active descriptor is synced; `ErrorCode::SystemError` when a write
    ///         or `fsync` failed while draining, `ErrorCode::NotInitialized` if the
    ///         writer was never started. Never inspect it via `.value()`.
    /// @warning Not a hot-path call: it blocks outside the never-block guarantee
    ///          that binds the record methods.
    /// @note Spec `logging/file-logger`, requirement "Flush synchronizes the file".
    [[nodiscard]] core::Status flush() noexcept { return worker_.flush(); }

    /// @brief Snapshot the aggregated drop counters across every shard.
    ///
    /// @return Per-cause totals (`rate_limited`, `buffer_full`, `slot_exhausted`)
    ///         summed over all shards.
    /// @note Third-thread diagnostic: a relaxed snapshot, not synchronization.
    /// @note Spec `logging/multi-producer`, requirement "Sharded drop accounting
    ///       without contention".
    [[nodiscard]] DroppedStats dropped() const noexcept { return producer_.dropped(); }
    /// @brief Report how many accepted records are still awaiting the writer.
    ///
    /// @return Aggregated shard occupancy at the moment of the call (a snapshot).
    [[nodiscard]] std::size_t pending() const noexcept { return producer_.ring_size(); }

    /// @brief Test whether the background writer reached its drain loop.
    ///
    /// @return `true` once the worker thread is running.
    /// @note Spec `logging/file-logger`, requirement "Background writer CPU core
    ///       affinity": poll this, then inspect the start code and thread report.
    [[nodiscard]] bool writer_started() const noexcept { return worker_.writer_started(); }
    /// @brief Report the first error observed while starting the writer.
    ///
    /// @return `ErrorCode::Ok` when the start phase was clean, otherwise the first
    ///         failure (for example `SystemError` from a `Required` attribute).
    [[nodiscard]] core::ErrorCode writer_start_code() const noexcept {
        return worker_.writer_start_code();
    }
    /// @brief Report the detailed outcome of applying the worker thread spec.
    ///
    /// @return Reference to the per-attribute apply report owned by the worker;
    ///         valid for the logger's lifetime.
    /// @note Spec `logging/file-logger`, requirement "Background writer CPU core
    ///       affinity", scenario "Best-effort failure remains non-fatal and
    ///       visible".
    [[nodiscard]] const core::ThreadApplyReport& writer_thread_report() const noexcept {
        return worker_.writer_thread_report();
    }
    /// @brief Report the CPU mask the writer actually applied.
    ///
    /// @return Effective affinity mask (empty when affinity was inherited).
    /// @note Spec `logging/file-logger`, requirement "Background writer CPU core
    ///       affinity", scenario "Pinned writer reports the configured mask".
    [[nodiscard]] std::uint64_t writer_mask() const noexcept { return worker_.writer_mask(); }

    /// @brief Render the logger's state for cold-path diagnostics.
    ///
    /// @return Threshold plus the shard-pool and writer snapshot — shard and
    ///         message capacities, shard count, aggregated pending records,
    ///         per-cause drop totals and whether the writer started.
    /// @note Mandate 9 (`to_string`), and spec `logging/file-logger`, requirement
    ///       "File logger vocabulary diagnostic strings"; never call this on a
    ///       hot path.
    [[nodiscard]] std::string to_string() const {
        return "FileLogger(level=" + std::string(core::to_string(producer_.level())) +
               ", shard_capacity=" + std::to_string(ShardCapacity) +
               ", shards=" + std::to_string(MaxThreads) +
               ", message_capacity=" + std::to_string(MessageCapacity) +
               ", pending=" + std::to_string(producer_.ring_size()) +
               ", dropped=" + producer_.dropped().to_string() +
               ", writer_started=" + std::string(worker_.writer_started() ? "true" : "false") + ")";
    }

private:
    /// @brief Validate a required path/name component.
    ///
    /// @param value Candidate text (directory or file name).
    /// @param max Largest accepted length in bytes, excluding the terminator.
    /// @return `true` when `value` is non-empty, fits in `max` bytes and contains
    ///         no embedded NUL.
    /// @note Spec `logging/file-logger`, requirement "Asynchronous wait-free
    ///       JSON-lines file output": an invalid path rejects initialization.
    [[nodiscard]] static bool valid_path(std::string_view value, std::size_t max) noexcept {
        return !value.empty() && value.size() <= max && value.find('\0') == std::string_view::npos;
    }

    /// @brief Validate an optional text field (emptiness is allowed).
    ///
    /// @param value Candidate text, typically the application name.
    /// @param max Largest accepted length in bytes, excluding the terminator.
    /// @return `true` when `value` fits in `max` bytes and contains no embedded
    ///         NUL; an empty view is valid.
    [[nodiscard]] static bool valid_optional_text(std::string_view value,
                                                  std::size_t max) noexcept {
        return value.size() <= max && value.find('\0') == std::string_view::npos;
    }

    /// @brief The complete configuration gate applied before the worker starts.
    ///
    /// @param cfg Configuration to check.
    /// @return `true` when every bounded string fits, the throttle budget is
    ///         coherent and a custom rotation carries a non-zero period; every
    ///         rejection maps to `ErrorCode::InvalidArgument`.
    /// @note Spec `logging/file-logger`, requirement "Hitch-free scheduled
    ///       directory rotation" and requirement "Hot-path rate limiting".
    [[nodiscard]] static bool valid_config(const FileLoggerConfig& cfg) noexcept {
        const bool throttle_ok =
            !cfg.throttle.enabled ||
            (cfg.throttle.max_records_per_window != 0ULL && cfg.throttle.window_ns != 0ULL);
        const bool rotation_ok =
            cfg.rotation.kind != RotationSchedule::Custom || cfg.rotation.custom_ns != 0ULL;
        return valid_path(cfg.base_dir, 511U) && valid_path(cfg.file_name, 63U) &&
               valid_optional_text(cfg.app_name, 63U) && throttle_ok && rotation_ok;
    }

    /// @brief Copy a string view into a fixed sink buffer, NUL-terminating it.
    ///
    /// @tparam N Capacity of the destination buffer in bytes.
    /// @param src Source text; may be longer than the buffer, in which case the
    ///        copy is truncated to `N - 1` bytes.
    /// @param dst Destination buffer of exactly `N` bytes.
    /// @param len_out Receives the number of copied bytes (excluding the
    ///        terminator).
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

/// @brief Free helper mirroring `FileLogger::to_string`.
///
/// @tparam ShardCapacity Records per shard ring of the dumped logger.
/// @tparam MessageCapacity Inline argument-blob size of its records.
/// @tparam MaxThreads Number of per-thread shards it owns.
/// @param logger Instance to render; safe to call from a third thread.
/// @return The same text as `logger.to_string()`.
/// @note Mandate 9 (`to_string`).
template <std::size_t ShardCapacity, std::size_t MessageCapacity, std::size_t MaxThreads>
[[nodiscard]] std::string
to_string(const FileLogger<ShardCapacity, MessageCapacity, MaxThreads>& logger) {
    return logger.to_string();
}

static_assert(concepts::Logger<FileLogger<>>,
              "FileLogger must satisfy the Logger contract (pinned at header level)");

} // namespace nexigen::logging
