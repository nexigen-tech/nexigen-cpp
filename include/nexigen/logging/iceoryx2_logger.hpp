// nexigen::logging — asynchronous wait-free logger publishing LogFrames to
// iceoryx2 (change add-iceoryx2-logger; spec logging/iceoryx2-logger;
// design D3-D8).
//
// Shape: the producer hot path is the shared detail::Producer (level filter →
// shard slot → stamp → throttle → cached tid → pack → try_push — ~20 ns,
// never blocking, never allocating, never formatting). Everything transport-
// touching lives on one background std::jthread (detail::AsyncWorker) that
// drains the shards and publishes each rendered record as a fixed LogFrame
// over iceoryx2 pub-sub + event (worker-publish — the transport cost of
// ~278 ns lands on the pinned non-RT worker, not the producer).
//
// DROP STORY (spec): over-throttle -> rate-limited, ring-full -> buffer-full,
// no slot -> slot-exhausted (all counted). The worker carries the drop delta
// into the next published frame (REQ_LOGC_007).
//
// CONTRACT (spec logging/multi-producer): up to MaxThreads distinct producer
// threads per instance; the worker is the sole consumer. level()/dropped()/
// pending()/writer_*() are third-thread safe relaxed snapshots; flush() is the
// control-plane barrier, outside the never-block guarantee.
#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/com/node.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/detail/async_worker.hpp>
#include <nexigen/logging/detail/format_param.hpp>
#include <nexigen/logging/detail/iceoryx2_sink.hpp>
#include <nexigen/logging/detail/producer.hpp>
#include <nexigen/logging/dropped_stats.hpp>
#include <nexigen/logging/iceoryx2_logger_config.hpp>

namespace nexigen::logging {

/// @brief Asynchronous wait-free logger publishing fixed `LogFrame`s to iceoryx2.
///
/// Producers only filter, pack and enqueue into their own shard; the shared
/// `detail::Producer` plus `detail::AsyncWorker` and `detail::Iceoryx2Sink`
/// render each record and publish it over iceoryx2 pub-sub, followed by an event
/// notification, entirely on the pinned background worker. Nothing on the
/// producer path formats, allocates or touches the transport.
///
/// Drops are counted per cause (`rate_limited`, `buffer_full`,
/// `slot_exhausted`); the worker carries the local drop delta into the next
/// published frame so a downstream daemon observes losses without side-band
/// communication.
///
/// The logger creates its local node during `initialize()`; the node, rings and
/// scratch storage live for the logger's lifetime, which is why the type is
/// non-copyable and non-movable.
///
/// @tparam ShardCapacity Records per shard ring; must be a power of two (the ring
///         buffer pins it with a `static_assert`), so aggregate capacity is
///         `capacity() * max_threads()`.
/// @tparam MessageCapacity Inline argument-blob size of each `LogRecord`.
/// @tparam MaxThreads Number of per-thread shards; producers beyond it take the
///         `slot-exhausted` drop path.
/// @note Spec `logging/iceoryx2-logger`, requirement "Worker publishes frames off
///       the hot path".
/// @note Spec `logging/multi-producer`, requirement "Pre-allocated per-thread
///       shard array".
template <std::size_t ShardCapacity = 128, std::size_t MessageCapacity = 128,
          std::size_t MaxThreads = 32>
class Iceoryx2Logger {
    /// @brief Shard pool and hot-path producer used by this instantiation.
    using Producer = detail::Producer<ShardCapacity, MessageCapacity, MaxThreads>;
    /// @brief Transport sink that renders and publishes each drained record.
    using Sink = detail::Iceoryx2Sink<Producer>;

    /// @brief Per-thread shards plus the level/throttle control cells.
    Producer producer_{};
    /// @brief Local iceoryx2 node, created by `initialize()` and outliving the worker.
    std::optional<com::Node> node_{};
    /// @brief Background render/publish loop; sole consumer of every shard.
    detail::AsyncWorker<Producer, Sink> worker_{};

public:
    /// @brief Construct the logger; `initialize()` must run before logging.
    Iceoryx2Logger() noexcept = default;
    /// @brief Copy construction is deleted; shard rings are address-pinned.
    Iceoryx2Logger(const Iceoryx2Logger&) = delete;
    /// @brief Copy assignment is deleted; shard rings are address-pinned.
    Iceoryx2Logger& operator=(const Iceoryx2Logger&) = delete;
    /// @brief Move construction is deleted; shard rings are address-pinned.
    Iceoryx2Logger(Iceoryx2Logger&&) = delete;
    /// @brief Move assignment is deleted; shard rings are address-pinned.
    Iceoryx2Logger& operator=(Iceoryx2Logger&&) = delete;

    /// @brief Report the per-shard ring capacity fixed at compile time.
    ///
    /// @return `ShardCapacity`; multiply by `max_threads()` for the aggregate pool
    ///         capacity.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    /// @brief Report how many producer threads may register a shard.
    ///
    /// @return `MaxThreads`; further threads are dropped and counted as
    ///         `slot_exhausted`.
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept { return MaxThreads; }
    /// @brief Report the inline argument-blob size of each record.
    ///
    /// @return `MessageCapacity` in bytes; bounds the packed argument payload
    ///         carried by a record.
    [[nodiscard]] static constexpr std::size_t message_capacity() noexcept {
        return MessageCapacity;
    }

    /// @brief Initialize the transport: validate, create the node, start the writer.
    ///
    /// Initialization phase, called before any real-time work: validates the
    /// configuration, creates the local iceoryx2 node, copies the configuration
    /// into fixed sink storage, installs the throttle policy, calibrates the
    /// clocks and starts the pinned worker. An invalid configuration returns
    /// `InvalidArgument` WITHOUT starting the worker.
    ///
    /// @param cfg Transport and worker configuration: service name, application
    ///        id, subscriber/history limits, level, throttle and worker thread
    ///        spec. Views are copied into fixed storage.
    /// @return OK once the worker is running; `ErrorCode::InvalidArgument` for an
    ///         empty/oversized service name, a bad application id, zero
    ///         subscriber/history limits, an incoherent throttle policy or an
    ///         invalid worker spec; `ErrorCode::SystemError` when node creation,
    ///         clock calibration or thread start fails. Never inspect it via
    ///         `.value()`.
    /// @note Spec `logging/iceoryx2-logger`, requirement "Iceoryx2 logger
    ///       configuration", scenario "Invalid config is rejected".
    [[nodiscard]] core::Status initialize(const Iceoryx2LoggerConfig& cfg) noexcept {
        if (!valid_config(cfg)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        const core::Status valid_thread = cfg.worker_thread.validate();
        if (!valid_thread) {
            return valid_thread;
        }

        auto node = com::Node::create();
        if (!node) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        node_.emplace(std::move(*node));

        typename Sink::Settings settings{};
        copy_into(cfg.service_name, settings.service_name, settings.service_name_len);
        const std::size_t app_take = cfg.app_id.size() < sizeof(settings.app_id)
                                         ? cfg.app_id.size()
                                         : sizeof(settings.app_id);
        for (std::size_t i = 0; i < app_take; ++i) {
            settings.app_id[i] = cfg.app_id[i];
        }
        settings.max_subscribers = cfg.max_subscribers;
        settings.history_size = cfg.history_size;

        producer_.set_level(cfg.initial_level);
        producer_.configure_throttle(cfg.throttle);
        const core::Status calibrated = producer_.initialize(); // per-slot calibrate, cold
        if (!calibrated) {
            return calibrated;
        }
        worker_.set_tuning(detail::WorkerTuning{.poll_interval_us = cfg.worker_poll_interval_us});
        return worker_.start(producer_, cfg.worker_thread, settings, &producer_, &*node_);
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
    /// `try_push`; the worker renders and publishes them later.
    ///
    /// @tparam Args Argument types deduced from the trailing values; each must
    ///         match the literal per `std::format_string<Args...>`.
    /// @param level Severity to record; filtered against the active threshold.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values packed into the record in declaration order; the fold
    ///        expands left to right so the renderer reads them in format order.
    /// @note Spec `logging/iceoryx2-logger`, requirement "Worker publishes frames
    ///       off the hot path".
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

    /// @brief Control-plane barrier: drain every shard and publish the records.
    ///
    /// @return OK when every record accepted before the call has been published
    ///         and the transport synchronized; `ErrorCode::NotInitialized` if the
    ///         writer was never started, `ErrorCode::SystemError` on a publish or
    ///         synchronization failure. Never inspect it via `.value()`.
    /// @warning Not a hot-path call: it blocks outside the never-block guarantee
    ///          that binds the record methods.
    [[nodiscard]] core::Status flush() noexcept { return worker_.flush(); }

    /// @brief Snapshot the aggregated drop counters across every shard.
    ///
    /// @return Per-cause totals (`rate_limited`, `buffer_full`, `slot_exhausted`)
    ///         summed over all shards.
    /// @note Third-thread diagnostic: a relaxed snapshot, not synchronization.
    /// @note Spec `logging/multi-producer`, requirement "Sharded drop accounting
    ///       without contention".
    [[nodiscard]] DroppedStats dropped() const noexcept { return producer_.dropped(); }
    /// @brief Report how many accepted records are still awaiting the worker.
    ///
    /// @return Aggregated shard occupancy at the moment of the call (a snapshot).
    [[nodiscard]] std::size_t pending() const noexcept { return producer_.ring_size(); }

    /// @brief Test whether the background worker reached its publish loop.
    ///
    /// @return `true` once the worker thread is running.
    [[nodiscard]] bool writer_started() const noexcept { return worker_.writer_started(); }
    /// @brief Report the first error observed while starting the worker.
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
    [[nodiscard]] const core::ThreadApplyReport& writer_thread_report() const noexcept {
        return worker_.writer_thread_report();
    }
    /// @brief Report the CPU mask the worker actually applied.
    ///
    /// @return Effective affinity mask (empty when affinity was inherited).
    [[nodiscard]] std::uint64_t writer_mask() const noexcept { return worker_.writer_mask(); }

    /// @brief Render the logger's state for cold-path diagnostics.
    ///
    /// @return Threshold plus the shard-pool and writer snapshot — shard and
    ///         message capacities, shard count, aggregated pending records,
    ///         per-cause drop totals and whether the writer started.
    /// @note Mandate 9 (`to_string`), and spec `logging/iceoryx2-logger`,
    ///       requirement "Iceoryx2 logger diagnostics"; never call this on a hot
    ///       path.
    [[nodiscard]] std::string to_string() const {
        return "Iceoryx2Logger(level=" + std::string(core::to_string(producer_.level())) +
               ", shard_capacity=" + std::to_string(ShardCapacity) +
               ", shards=" + std::to_string(MaxThreads) +
               ", message_capacity=" + std::to_string(MessageCapacity) +
               ", pending=" + std::to_string(producer_.ring_size()) +
               ", dropped=" + producer_.dropped().to_string() +
               ", writer_started=" + std::string(worker_.writer_started() ? "true" : "false") + ")";
    }

private:
    /// @brief Validate a required, non-empty bounded text field.
    ///
    /// @param value Candidate text (service name or application id).
    /// @param max Largest accepted length in bytes, excluding the terminator.
    /// @return `true` when `value` is non-empty, fits in `max` bytes and contains
    ///         no embedded NUL.
    [[nodiscard]] static bool valid_text(std::string_view value, std::size_t max) noexcept {
        return !value.empty() && value.size() <= max && value.find('\0') == std::string_view::npos;
    }

    /// @brief The complete configuration gate applied before the worker starts.
    ///
    /// @param cfg Configuration to check.
    /// @return `true` when the service name fits 511 bytes, the application id
    ///         fits 4 bytes, both subscriber and history limits are non-zero and
    ///         the throttle budget is coherent; every rejection maps to
    ///         `ErrorCode::InvalidArgument`.
    /// @note Spec `logging/iceoryx2-logger`, requirement "Iceoryx2 logger
    ///       configuration", scenario "Invalid config is rejected".
    [[nodiscard]] static bool valid_config(const Iceoryx2LoggerConfig& cfg) noexcept {
        const bool throttle_ok =
            !cfg.throttle.enabled ||
            (cfg.throttle.max_records_per_window != 0ULL && cfg.throttle.window_ns != 0ULL);
        return valid_text(cfg.service_name, 511U) && valid_text(cfg.app_id, 4U) &&
               cfg.max_subscribers != 0U && cfg.history_size != 0U && throttle_ok;
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

/// @brief Free helper mirroring `Iceoryx2Logger::to_string`.
///
/// @tparam ShardCapacity Records per shard ring of the dumped logger.
/// @tparam MessageCapacity Inline argument-blob size of its records.
/// @tparam MaxThreads Number of per-thread shards it owns.
/// @param logger Instance to render; also names the worker state.
/// @return The same text as `logger.to_string()`.
/// @note Mandate 9 (`to_string`), and spec `logging/iceoryx2-logger`,
///       requirement "Iceoryx2 logger diagnostics".
template <std::size_t ShardCapacity, std::size_t MessageCapacity, std::size_t MaxThreads>
[[nodiscard]] std::string
to_string(const Iceoryx2Logger<ShardCapacity, MessageCapacity, MaxThreads>& logger) {
    return logger.to_string();
}

static_assert(concepts::Logger<Iceoryx2Logger<>>,
              "Iceoryx2Logger must satisfy the Logger contract (pinned at header level)");

} // namespace nexigen::logging
