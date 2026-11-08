// nexigen::chrono — the clock service: provider (change
// add-chrono-clock-service; spec chrono/chrono-provider). Two-phase lifecycle
// added by change integrate-chrono-into-infrastructure: construction provides
// snapshot_now() without starting the worker; start() (with an optional
// logger) starts the background snapshot worker.
//
// ChronoProvider samples the base clock domains (monotonic_raw + realtime)
// and delivers a UTC-offset Snapshot to a bound sink. It runs one
// background std::jthread worker that pins itself to the configured core mask
// before its first snapshot (so sampling and sink I/O never preempt the
// real-time threads) and delivers one snapshot per collect_interval; a zero
// interval disables the worker and leaves snapshot_now() as the sole trigger.
//
// The provider binds a ChronoSink type-erased through plain function pointers
// (no virtual dispatch, no RTTI). It performs no heap allocation on the
// steady-state snapshot path: all working state (worker, atomics, log hook) is
// fixed members. The optional worker logger is a module-local hook so chrono
// does not depend on the logging module.
//
// The provider's own vocabulary types are split per the file-per-entity
// convention: ClockLogLevel (clock_log_level.hpp), ChronoProviderConfig
// (chrono_provider_config.hpp), concepts::ClockLogger
// (concepts/clock_logger_concept.hpp), and the type-erased bindings
// (detail/sink_binding.hpp, detail/log_binding.hpp).
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <nexigen/chrono/chrono_provider_config.hpp>
#include <nexigen/chrono/snapshot.hpp>
#include <nexigen/chrono/clock_log_level.hpp>
#include <nexigen/chrono/concepts/chrono_sink_concept.hpp>
#include <nexigen/chrono/concepts/clock_logger_concept.hpp>
#include <nexigen/chrono/detail/log_binding.hpp>
#include <nexigen/chrono/detail/sink_binding.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>

namespace nexigen::chrono {

/// The clock service: samples base clock domains and delivers UTC-offset
/// snapshots to a bound sink.
class ChronoProvider {
public:
    /// Bind a sink. Construction does NOT start the background worker; call
    /// start() to begin periodic snapshotting. The sink MUST outlive the
    /// provider.
    template <concepts::ChronoSink Sink>
    ChronoProvider(const ChronoProviderConfig& config, Sink& sink)
        : ChronoProvider(config,
                         detail::SinkBinding{
                             static_cast<void*>(&sink),
                             [](void* self, const Snapshot& snapshot) noexcept -> core::Status {
                                 return static_cast<Sink*>(self)->on_snapshot(snapshot);
                             },
                             [](void* self) noexcept -> core::Status {
                                 return static_cast<Sink*>(self)->flush();
                             }}) {}

    ChronoProvider(const ChronoProvider&) = delete;
    ChronoProvider& operator=(const ChronoProvider&) = delete;
    ChronoProvider(ChronoProvider&&) = delete;
    ChronoProvider& operator=(ChronoProvider&&) = delete;

    ~ChronoProvider();

    /// Start the background snapshot worker (no logger). Idempotent; a zero
    /// collect_interval starts nothing.
    [[nodiscard]] core::Status start() noexcept {
        if (worker_.joinable()) {
            return {}; // already started
        }
        if (config_.collect_interval > std::chrono::nanoseconds{0}) {
            const core::Status valid_thread = config_.worker_thread.validate();
            if (!valid_thread) {
                return valid_thread;
            }
            startup_published_.store(false, std::memory_order_relaxed);
            worker_ = std::jthread([this](std::stop_token stop) { snapshot_loop(stop); });
            if (config_.worker_thread.apply_mode() == core::ThreadApplyMode::Required) {
                while (!startup_published_.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                const core::ErrorCode attribute_error = worker_thread_report_.first_error();
                if (attribute_error != core::ErrorCode::Ok) {
                    worker_.request_stop();
                    {
                        std::lock_guard<std::mutex> lock(wake_mutex_);
                        wake_cv_.notify_all();
                    }
                    worker_.join();
                    return std::unexpected(attribute_error);
                }
            }
        }
        return {};
    }

    /// Bind a logger and start the background snapshot worker. The logger
    /// MUST outlive the provider (the worker logs through it until it stops).
    /// Double-start guard (analysis #12; change fix-p1-races-process-
    /// correctness): while the worker runs, `log_binding_` is written only
    /// here and read by the worker — a second logger binding would race the
    /// worker's loads, so a call on a running provider returns `Conflict`
    /// WITHOUT touching the binding. Rebinding is only possible on a
    /// provider whose worker is not running.
    template <concepts::ClockLogger Logger>
    [[nodiscard]] core::Status start(Logger& logger) noexcept {
        if (worker_.joinable()) {
            return std::unexpected(core::ErrorCode::Conflict);
        }
        log_binding_ =
            detail::LogBinding{static_cast<void*>(&logger), [](void* self, ClockLogLevel level,
                                                               std::string_view message) noexcept {
                                   static_cast<Logger*>(self)->log(level, message);
                               }};
        return start();
    }

    /// Sample the clocks and deliver one snapshot synchronously on the calling
    /// thread.
    [[nodiscard]] core::Status snapshot_now() noexcept;

    /// Flush the bound sink.
    [[nodiscard]] core::Status flush() noexcept;

    /// Result of the provider's startup steps (affinity pinning).
    [[nodiscard]] core::Status startup_status() const noexcept;

    /// The post-pin affinity mask as the worker observed it (0 before start).
    [[nodiscard]] std::uint64_t worker_mask() const noexcept;

    [[nodiscard]] const core::ThreadApplyReport& worker_thread_report() const noexcept {
        static_cast<void>(startup_published_.load(std::memory_order_acquire));
        return worker_thread_report_;
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;

private:
    ChronoProvider(const ChronoProviderConfig& config, detail::SinkBinding binding);

    /// Background snapshot worker body.
    void snapshot_loop(std::stop_token stop) noexcept;

    /// Emit a worker diagnostic through the bound logger, if any.
    void log(ClockLogLevel level, std::string_view message) noexcept {
        if (log_binding_.log != nullptr) {
            log_binding_.log(log_binding_.self, level, message);
        }
    }

    ChronoProviderConfig config_{};
    detail::SinkBinding binding_{};
    detail::LogBinding log_binding_{};

    std::jthread worker_{};
    std::mutex snapshot_mutex_{}; // serializes manual snapshot_now() with the worker
    std::mutex wake_mutex_{};
    std::condition_variable_any wake_cv_{};

    // Cache-line-separated so the control plane never false-shares with the
    // worker's hot cells (mandate 5).
    alignas(64) std::atomic<std::uint64_t> worker_mask_{0};
    alignas(64) std::atomic<core::ErrorCode> startup_error_{core::ErrorCode::Ok};
    core::ThreadApplyReport worker_thread_report_{};
    std::atomic<bool> startup_published_{false};
};

} // namespace nexigen::chrono

/// The provider formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::chrono::ChronoProvider> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::ChronoProvider& provider, Ctx& ctx) const {
        const std::string text = provider.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
