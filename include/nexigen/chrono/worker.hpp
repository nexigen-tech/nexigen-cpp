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
#include <nexigen/core/worker_thread.hpp>

namespace nexigen::chrono {

/// @brief Periodic snapshot daemon over a bound `Provider`.
///
/// Owns the background thread and the daemon-only machinery (thread attributes,
/// optional logger binding, fixed-rate delivery loop, shutdown draining) so the
/// provider type itself stays threadless in both daemon and manual modes. A zero
/// `collect_interval` disables the thread and leaves explicit `snapshot_now()`
/// calls as the sole trigger.
///
/// Hot/cold: the worker loop is a cold-path consumer; its only hot interaction
/// is calling the provider's `snapshot_now()`, which samples the clocks and
/// delivers one complete snapshot. The loop never touches producer stampers.
/// Destruction is the only path that drains: it stops and joins the thread,
/// delivers one final snapshot, and flushes the sink.
///
/// @tparam P Provider type, constrained by `concepts::Provider`; it must
///         outlive the worker.
/// @note Spec `chrono/worker`, requirement "Periodic snapshot delivery and
///       collection period"; requirement "Worker shutdown".
template <concepts::Provider P> class ChronoWorker {
public:
    /// @brief Bind the provider and copy the driver configuration.
    ///
    /// No thread is started and no snapshot is delivered here.
    ///
    /// @param provider Provider receiving snapshot triggers; it must outlive the
    ///        worker.
    /// @param config Driver configuration copied into the instance.
    /// @note Spec `chrono/worker`, requirement "Deferred worker start".
    ChronoWorker(P& provider, const ProviderConfig& config) noexcept
        : provider_(provider), config_(config) {}

    /// @brief Copying is deleted: the worker owns a thread and its state.
    ChronoWorker(const ChronoWorker&) = delete;
    /// @brief Copy assignment is deleted: the worker owns a thread and its state.
    ChronoWorker& operator=(const ChronoWorker&) = delete;
    /// @brief Moving is deleted: the thread body captures this instance.
    ChronoWorker(ChronoWorker&&) = delete;
    /// @brief Move assignment is deleted: the thread body captures this instance.
    ChronoWorker& operator=(ChronoWorker&&) = delete;

    /// @brief Stop the worker, deliver one final snapshot, and flush the sink.
    ///
    /// Requests stop, wakes the wait, and joins before releasing storage; then
    /// delivers the final snapshot through the provider and flushes its sink.
    /// Safe for a worker that was never started, and never throws.
    ///
    /// @note Spec `chrono/worker`, requirement "Worker shutdown".
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

    /// @brief Start the background snapshot worker without a logger.
    ///
    /// Idempotent: an already-running worker returns success without starting a
    /// second thread. A zero (or negative) `collect_interval` starts nothing and
    /// returns success without applying thread attributes. Under a `Required`
    /// thread spec a failed attribute makes start fail, stops the loop before any
    /// snapshot, and joins the thread.
    ///
    /// @return Success when the worker is running or intentionally started
    ///         nothing, `core::ErrorCode::InvalidArgument` for an invalid thread
    ///         spec, or `core::ErrorCode::SystemError` for a `Required`
    ///         attribute failure.
    /// @note Spec `chrono/worker`, requirement "Deferred worker start";
    ///       requirement "Worker thread attributes".
    [[nodiscard]] core::Status start() noexcept {
        if (worker_.joinable()) {
            return {};
        }
        if (config_.collect_interval <= std::chrono::nanoseconds{0}) {
            return {};
        }
        const core::Status valid_thread = config_.worker_thread.validate();
        if (!valid_thread) {
            return valid_thread;
        }
        return worker_.start(config_.worker_thread, Body{this});
    }

    /// @brief Bind a logger and start the background worker.
    ///
    /// A `start(Logger&)` on a running worker returns `Conflict` without
    /// touching the existing binding; rebinding is possible only while the
    /// worker is not running.
    ///
    /// @tparam Logger Concrete logger type, constrained by `concepts::Logger`
    ///         (`void log(core::LogLevel, std::string_view) noexcept`).
    /// @param logger Logger receiving worker diagnostics; it must outlive the
    ///        worker.
    /// @return Success when started (or intentionally not started),
    ///         `core::ErrorCode::Conflict` when the worker already runs, or the
    ///         failure status of the underlying `start()`.
    /// @note Spec `chrono/worker`, requirement "Worker logger binding";
    ///       requirement "Worker logger rebinding contract".
    template <concepts::Logger Logger> [[nodiscard]] core::Status start(Logger& logger) noexcept {
        if (worker_.joinable()) {
            return std::unexpected(core::ErrorCode::Conflict);
        }
        log_binding_ =
            detail::LogBinding{static_cast<void*>(&logger), [](void* self, core::LogLevel level,
                                                               std::string_view message) noexcept {
                                   static_cast<Logger*>(self)->log(level, message);
                               }};
        return start();
    }

    /// @brief Read the worker's startup result.
    ///
    /// Exposes the startup error published by `core::WorkerThread`. Under
    /// best-effort application an attribute failure still leaves the startup
    /// code `Ok` and remains visible through `worker_thread_report()`; under
    /// `Required` a failure is reported here and stops delivery.
    ///
    /// @return Success when the startup code is `Ok`, otherwise the worker's
    ///         published startup error. Returns success before start.
    /// @note Spec `chrono/worker`, requirement "Worker state reporting".
    [[nodiscard]] core::Status startup_status() const noexcept {
        const core::ErrorCode error = worker_.start_code();
        if (error != core::ErrorCode::Ok) {
            return std::unexpected(error);
        }
        return {};
    }

    /// @brief The post-pin affinity mask as the worker observed it.
    ///
    /// Cores above bit 63 are not representable in this legacy `uint64_t` mask;
    /// the requested CPU set in the worker's `ThreadSpec` stays authoritative.
    ///
    /// @return The worker-observed affinity mask, or `0` before start.
    /// @note Spec `chrono/worker`, requirement "Worker state reporting".
    [[nodiscard]] std::uint64_t worker_mask() const noexcept { return worker_.mask(); }

    /// @brief The immutable thread-application report published at startup.
    ///
    /// Readable without locks by dead-path diagnostics callers; before start it
    /// holds a default, empty report.
    ///
    /// @return Reference to the worker's `core::ThreadApplyReport`; valid for the
    ///         worker's lifetime.
    /// @note Spec `chrono/worker`, requirement "Worker state reporting".
    [[nodiscard]] const core::ThreadApplyReport& worker_thread_report() const noexcept {
        return worker_.report();
    }

    /// @brief Render configuration and startup state as a diagnostic string.
    ///
    /// Reports interval, worker thread spec, observed mask, and startup error.
    /// Allocates through `std::format`; diagnostics only.
    ///
    /// @return The rendered text.
    /// @note Spec `chrono/worker`, requirement "Worker diagnostics"; project
    ///       mandate 9.
    [[nodiscard]] std::string to_string() const {
        return std::format("ChronoWorker(collect_interval_ns={}, worker_thread={}, "
                           "worker_mask=0x{:x}, startup_error={})",
                           config_.collect_interval.count(), config_.worker_thread.to_string(),
                           worker_mask(), core::to_string(worker_.start_code()));
    }

private:
    /// @brief The background snapshot delivery loop.
    ///
    /// Waits on a condition variable until the next deadline or a stop request,
    /// then triggers exactly one provider snapshot. Periods are fixed-rate: a
    /// missed deadline is skipped rather than chased, so a stalled sink cannot
    /// accumulate a backlog. Delivery failures are logged and do not stop the
    /// loop.
    ///
    /// @param stop Stop token; a request ends the loop after the current wait.
    void snapshot_loop(std::stop_token stop) noexcept {
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

    /// @brief Emit a worker diagnostic through the bound logger, if any.
    ///
    /// @param level Severity forwarded to the logger.
    /// @param message Borrowed view; validity is the caller's responsibility.
    void log(core::LogLevel level, std::string_view message) noexcept {
        if (log_binding_.log != nullptr) {
            log_binding_.log(log_binding_.self, level, message);
        }
    }

    /// @brief Adapter turning this worker into a `core::WorkerBody`.
    struct Body {
        ChronoWorker* self{nullptr}; ///< Worker whose loop is executed.
        /// @brief Run the worker's snapshot loop on the background thread.
        ///
        /// @param stop Stop token owned by the worker thread.
        void run(std::stop_token stop) noexcept { self->snapshot_loop(stop); }
    };

    P& provider_;                           ///< Borrowed provider; outlives the worker.
    ProviderConfig config_{};               ///< Copied driver configuration.
    detail::LogBinding log_binding_{};      ///< Optional type-erased logger.
    std::mutex wake_mutex_{};               ///< Serializes wake-up against the wait.
    std::condition_variable_any wake_cv_{}; ///< Wakes the loop for stop requests.

    core::WorkerThread<Body> worker_{}; ///< LAST member: joins before the provider.
};

} // namespace nexigen::chrono

/// @brief `std::formatter` specialization rendering the worker diagnostically.
///
/// @tparam P Provider type of the worker being formatted; constrained by
///         `nexigen::chrono::concepts::Provider`.
/// @note Spec `chrono/worker`, requirement "Worker diagnostics"; project
///       mandate 11.
template <nexigen::chrono::concepts::Provider P>
struct std::formatter<nexigen::chrono::ChronoWorker<P>> : std::formatter<std::string_view> {
    /// @brief Format a worker as its diagnostic string.
    ///
    /// @tparam Ctx Format context type; the inherited string specifiers apply.
    /// @param worker Worker to format.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    /// @note Project mandate 11 (`std::formatter`).
    template <class Ctx>
    auto format(const nexigen::chrono::ChronoWorker<P>& worker, Ctx& ctx) const {
        const std::string text = worker.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
