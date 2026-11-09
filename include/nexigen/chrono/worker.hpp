// nexigen::chrono — the periodic snapshot daemon wrapper (change
// add-clock-authority-core; spec chrono/worker).
//
// ChronoWorker owns the background thread and the daemon-only machinery
// (thread attributes, logger binding, fixed-rate loop, shutdown draining)
// around a bound Provider, so the provider type itself stays threadless in
// both daemon and manual modes. A zero collect_interval disables the thread
// and leaves explicit snapshot_now() calls the sole trigger.
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

#include <nexigen/chrono/concepts/logger_concept.hpp>
#include <nexigen/chrono/concepts/provider_concept.hpp>
#include <nexigen/chrono/detail/affinity.hpp>
#include <nexigen/chrono/detail/log_binding.hpp>
#include <nexigen/chrono/provider_config.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>

namespace nexigen::chrono {

/// Periodic snapshot daemon over a bound Provider.
template <concepts::Provider P>
class ChronoWorker {
public:
    /// Bind the provider and copy the driver configuration (interval + worker
    /// thread spec). The provider MUST outlive the worker.
    ChronoWorker(P& provider, const ProviderConfig& config) noexcept
        : provider_(provider), config_(config) {}

    ChronoWorker(const ChronoWorker&) = delete;
    ChronoWorker& operator=(const ChronoWorker&) = delete;
    ChronoWorker(ChronoWorker&&) = delete;
    ChronoWorker& operator=(ChronoWorker&&) = delete;

    /// Stop the worker, deliver one final snapshot through the provider, and
    /// flush the sink before releasing storage.
    ~ChronoWorker() {
        if (worker_.joinable()) {
            worker_.request_stop();
            {
                std::lock_guard<std::mutex> lock(wake_mutex_);
                wake_cv_.notify_all();
            }
            worker_.join();
        }
        static_cast<void>(provider_.snapshot_now());
        static_cast<void>(provider_.flush());
    }

    /// Start the background snapshot worker (no logger). Idempotent; a zero
    /// collect_interval starts nothing.
    [[nodiscard]] core::Status start() noexcept {
        if (worker_.joinable()) {
            return {};
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

    /// Bind a logger and start the background worker. The logger MUST outlive
    /// the worker. A start(Logger&) on a running worker returns Conflict
    /// WITHOUT touching the binding; rebinding is only possible while the
    /// worker is not running.
    template <concepts::Logger Logger>
    [[nodiscard]] core::Status start(Logger& logger) noexcept {
        if (worker_.joinable()) {
            return std::unexpected(core::ErrorCode::Conflict);
        }
        log_binding_ =
            detail::LogBinding{static_cast<void*>(&logger),
                               [](void* self, core::LogLevel level,
                                  std::string_view message) noexcept {
                                   static_cast<Logger*>(self)->log(level, message);
                               }};
        return start();
    }

    /// Result of the worker's startup steps (affinity pinning).
    [[nodiscard]] core::Status startup_status() const noexcept {
        const core::ErrorCode error = startup_error_.load(std::memory_order_acquire);
        if (error != core::ErrorCode::Ok) {
            return std::unexpected(error);
        }
        return {};
    }

    /// The post-pin affinity mask as the worker observed it (0 before start).
    [[nodiscard]] std::uint64_t worker_mask() const noexcept {
        return worker_mask_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] const core::ThreadApplyReport& worker_thread_report() const noexcept {
        static_cast<void>(startup_published_.load(std::memory_order_acquire));
        return worker_thread_report_;
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("ChronoWorker(collect_interval_ns={}, worker_thread={}, "
                           "worker_mask=0x{:x}, startup_error={})",
                           config_.collect_interval.count(), config_.worker_thread.to_string(),
                           worker_mask(),
                           core::to_string(startup_error_.load(std::memory_order_acquire)));
    }

private:
    /// Background snapshot worker body.
    void snapshot_loop(std::stop_token stop) noexcept {
        const core::Status attributes =
            core::apply_thread_spec_self(config_.worker_thread, worker_thread_report_);
        startup_error_.store(worker_thread_report_.first_error(), std::memory_order_relaxed);
        startup_published_.store(true, std::memory_order_release);
        if (!attributes && config_.worker_thread.apply_mode() == core::ThreadApplyMode::Required) {
            return;
        }
        if (worker_thread_report_.first_error() != core::ErrorCode::Ok) {
            log(core::LogLevel::Warning, "chrono worker: thread attribute application failed");
        }
        std::uint64_t mask = 0;
        if (detail::self_affinity(mask)) {
            worker_mask_.store(mask, std::memory_order_relaxed);
        }
        log(core::LogLevel::Info, "chrono worker started");

        auto next = std::chrono::steady_clock::now() + config_.collect_interval;
        while (!stop.stop_requested()) {
            {
                std::unique_lock<std::mutex> lock(wake_mutex_);
                wake_cv_.wait_until(lock, stop, next, [] { return false; });
            }
            if (stop.stop_requested()) {
                break;
            }
            const core::Status delivered = provider_.snapshot_now();
            if (!delivered) {
                log(core::LogLevel::Warning, "chrono worker: snapshot delivery failed");
            }
            next += config_.collect_interval;
            const auto now = std::chrono::steady_clock::now();
            if (next < now) {
                next = now + config_.collect_interval; // do not chase a backlog
            }
        }
        log(core::LogLevel::Info, "chrono worker stopped");
    }

    /// Emit a worker diagnostic through the bound logger, if any.
    void log(core::LogLevel level, std::string_view message) noexcept {
        if (log_binding_.log != nullptr) {
            log_binding_.log(log_binding_.self, level, message);
        }
    }

    P& provider_;
    ProviderConfig config_{};
    detail::LogBinding log_binding_{};

    std::jthread worker_{};
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

/// The worker formats as its diagnostic string (mandate 11).
template <nexigen::chrono::concepts::Provider P>
struct std::formatter<nexigen::chrono::ChronoWorker<P>> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::ChronoWorker<P>& worker, Ctx& ctx) const {
        const std::string text = worker.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
