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

/// Asynchronous wait-free logger publishing fixed LogFrames to an iceoryx2
/// service (worker-publish). Composed of the shared Producer + AsyncWorker,
/// parameterized by the Iceoryx2Sink.
template <std::size_t ShardCapacity = 128, std::size_t MessageCapacity = 128,
          std::size_t MaxThreads = 32>
class Iceoryx2Logger {
    using Producer = detail::Producer<ShardCapacity, MessageCapacity, MaxThreads>;
    using Sink = detail::Iceoryx2Sink<Producer>;

    Producer producer_{};
    std::optional<com::Node> node_{};
    detail::AsyncWorker<Producer, Sink> worker_{};

public:
    Iceoryx2Logger() noexcept = default;
    Iceoryx2Logger(const Iceoryx2Logger&) = delete;
    Iceoryx2Logger& operator=(const Iceoryx2Logger&) = delete;
    Iceoryx2Logger(Iceoryx2Logger&&) = delete;
    Iceoryx2Logger& operator=(Iceoryx2Logger&&) = delete;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept { return MaxThreads; }
    [[nodiscard]] static constexpr std::size_t message_capacity() noexcept {
        return MessageCapacity;
    }

    /// Initialization phase (pre real-time): validate config, create the local
    /// node, copy config into fixed storage, calibrate clocks, start the
    /// pinned worker. Invalid configuration returns InvalidArgument WITHOUT
    /// starting the worker.
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

    void set_level(core::LogLevel new_level) noexcept { producer_.set_level(new_level); }
    [[nodiscard]] core::LogLevel level() const noexcept { return producer_.level(); }

    /// Variadic surface (spec logging/logger-concept): pack + shard enqueue,
    /// never blocks, never formats, returns void; drops count per cause.
    template <class... Args>
    void log(core::LogLevel level, detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(level, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void verbose(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Verbose, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void debug(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void info(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void warning(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void error(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void fatal(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(core::LogLevel::Fatal, fmt, std::forward<Args>(args)...);
    }

    /// Control-plane drain barrier: every record accepted before this call is
    /// published (drain + synchronize), outside the never-block guarantee.
    [[nodiscard]] core::Status flush() noexcept { return worker_.flush(); }

    /// Drop accounting snapshot (third-thread safe).
    [[nodiscard]] DroppedStats dropped() const noexcept { return producer_.dropped(); }
    [[nodiscard]] std::size_t pending() const noexcept { return producer_.ring_size(); }

    /// Writer start-phase snapshots (design D15 verification surface).
    [[nodiscard]] bool writer_started() const noexcept { return worker_.writer_started(); }
    [[nodiscard]] core::ErrorCode writer_start_code() const noexcept {
        return worker_.writer_start_code();
    }
    [[nodiscard]] const core::ThreadApplyReport& writer_thread_report() const noexcept {
        return worker_.writer_thread_report();
    }
    [[nodiscard]] std::uint64_t writer_mask() const noexcept { return worker_.writer_mask(); }

    /// Cold-path diagnostic (mandate 9).
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
    [[nodiscard]] static bool valid_text(std::string_view value, std::size_t max) noexcept {
        return !value.empty() && value.size() <= max && value.find('\0') == std::string_view::npos;
    }

    [[nodiscard]] static bool valid_config(const Iceoryx2LoggerConfig& cfg) noexcept {
        const bool throttle_ok =
            !cfg.throttle.enabled ||
            (cfg.throttle.max_records_per_window != 0ULL && cfg.throttle.window_ns != 0ULL);
        return valid_text(cfg.service_name, 511U) && valid_text(cfg.app_id, 4U) &&
               cfg.max_subscribers != 0U && cfg.history_size != 0U && throttle_ok;
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
to_string(const Iceoryx2Logger<ShardCapacity, MessageCapacity, MaxThreads>& logger) {
    return logger.to_string();
}

static_assert(concepts::Logger<Iceoryx2Logger<>>,
              "Iceoryx2Logger must satisfy the Logger contract (pinned at header level)");

} // namespace nexigen::logging
