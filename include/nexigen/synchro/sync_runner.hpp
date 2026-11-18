// nexigen::synchro — bounded consumer loop (change add-synchro-event-sync;
// spec synchro/consumer; design D9).
//
// SyncRunner owns exactly one SyncConsumer and one EventConsumerSet<16>, so one
// runner manages one synchronization channel. Multiple channels use multiple
// runners; the v1 set capacity is never exceeded.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <new>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/com/event_consumer_set.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/wait_outcome.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/null_logger.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/synchro/detail/sync_clock.hpp>
#include <nexigen/synchro/sync_consumer.hpp>
#include <nexigen/synchro/sync_consumer_config.hpp>
#include <nexigen/synchro/sync_consumer_stats.hpp>
#include <nexigen/synchro/sync_error.hpp>

namespace nexigen::synchro {

/// @brief Owns one `SyncConsumer` and its `EventConsumerSet<16>` wait loop.
///
/// `run_once()` waits for the consumer listener with the watchdog-derived
/// timeout, drains on `Events`, and checks the watchdog on `Timeout`.
/// `run(stop_token)` repeats that step until stop and installs a stop callback
/// that calls `wake_up()`, so shutdown never waits for the watchdog.
///
/// @tparam Logger Logger type satisfying `logging::concepts::Logger`; defaults
///         to `logging::NullLogger`.
/// @note Spec `synchro/consumer`, requirement "SyncRunner loop".
/// @warning The logger and meter must outlive the runner. The meter must be
///          dedicated to this channel.
template <logging::concepts::Logger Logger = logging::NullLogger> class SyncRunner {
public:
    /// @brief Consumer capacity of the owned wait set (v1 hard limit).
    static constexpr std::size_t max_consumers = 16U;

    /// @brief Create the consumer, the wait set, and attach the consumer.
    ///
    /// @param node Node that must outlive the runner.
    /// @param config Consumer configuration (service, grid, latency, watchdog).
    /// @param logger Logger receiving lifecycle and anomaly records; must
    ///        outlive the runner.
    /// @param meter Meter receiving the consumer instruments; must outlive the
    ///        runner and be dedicated to this channel.
    /// @return Heap runner with the consumer attached, or a structured
    ///         `SyncError`.
    /// @note Spec `synchro/consumer`, requirement "SyncRunner loop".
    [[nodiscard]] static SyncResult<std::unique_ptr<SyncRunner>>
    create(com::Node& node, const SyncConsumerConfig& config, Logger& logger,
           metrics::Meter& meter) noexcept {
        auto consumer = SyncConsumer<Logger>::create(node, config, logger, meter);
        if (!consumer.has_value()) {
            return std::unexpected(consumer.error());
        }

        auto wait_set = std::unique_ptr<com::EventConsumerSet<max_consumers>>(
            new (std::nothrow) com::EventConsumerSet<max_consumers>(node));
        if (wait_set == nullptr) {
            return std::unexpected(make_lifecycle_error(core::ErrorCode::ResourceExhausted));
        }
        if (!wait_set->status().has_value()) {
            return std::unexpected(make_transport_error(wait_set->status().error()));
        }
        const com::Status attached = wait_set->attach(**consumer);
        if (!attached.has_value()) {
            return std::unexpected(make_transport_error(attached.error()));
        }

        auto* raw =
            new (std::nothrow) SyncRunner(std::move(*consumer), std::move(wait_set), logger);
        if (raw == nullptr) {
            return std::unexpected(make_lifecycle_error(core::ErrorCode::ResourceExhausted));
        }
        return std::unique_ptr<SyncRunner>{raw};
    }

    /// @brief Detach the consumer and destroy both owned objects.
    ~SyncRunner() {
        if (wait_set_ && consumer_) {
            static_cast<void>(wait_set_->detach(*consumer_));
        }
    }

    /// @brief Copy construction is deleted: the wait set is address-bound.
    SyncRunner(const SyncRunner&) = delete;
    /// @brief Copy assignment is deleted: the wait set is address-bound.
    SyncRunner& operator=(const SyncRunner&) = delete;
    /// @brief Move construction is deleted: the wait set is address-bound.
    SyncRunner(SyncRunner&&) = delete;
    /// @brief Move assignment is deleted: the wait set is address-bound.
    SyncRunner& operator=(SyncRunner&&) = delete;

    /// @brief Wait once, drain on events, and check the watchdog on timeout.
    /// @return The wait outcome (`Events`, `Timeout`, or `WokenUp`), or a
    ///         transport/clock error.
    /// @note Spec `synchro/consumer`, requirement "SyncRunner loop".
    [[nodiscard]] SyncResult<com::WaitOutcome> run_once() noexcept {
        const std::chrono::milliseconds timeout = consumer_->next_wait_timeout();
        auto outcome = wait_set_->wait_for(timeout);
        if (!outcome.has_value()) {
            return std::unexpected(make_transport_error(outcome.error()));
        }
        if (*outcome == com::WaitOutcome::Timeout) {
            auto now = detail::raw_now_ns();
            if (!now.has_value()) {
                return std::unexpected(now.error());
            }
            static_cast<void>(consumer_->check_sync_loss(*now));
        }
        return *outcome;
    }

    /// @brief Run the bounded loop until stop is requested.
    ///
    /// Installs a stop callback that wakes the wait set, so `request_stop()`
    /// from another thread interrupts an in-flight `ppoll` immediately.
    ///
    /// @param stop Stop token from the application or a `WorkerThread` body.
    /// @note Spec `synchro/consumer`, requirement "SyncRunner loop".
    void run(std::stop_token stop) noexcept {
        std::stop_callback wake{stop, [this] { wake_up(); }};
        logger_->info("sync runner started: attached={}", wait_set_->size());
        while (!stop.stop_requested()) {
            auto outcome = run_once();
            if (!outcome.has_value()) {
                logger_->error("sync runner stopping after error: {}", outcome.error().to_string());
                break;
            }
        }
        logger_->info("sync runner stopped");
    }

    /// @brief Interrupt a pending wait from any thread.
    /// @note Spec `synchro/consumer`, requirement "SyncRunner loop".
    void wake_up() noexcept {
        if (wait_set_) {
            wait_set_->wake_up();
        }
    }

    /// @brief The owned consumer.
    /// @return Mutable consumer reference.
    /// @note Spec `synchro/consumer`, requirement "SyncRunner loop".
    [[nodiscard]] SyncConsumer<Logger>& consumer() noexcept { return *consumer_; }
    /// @brief The owned consumer.
    /// @return Const consumer reference.
    /// @note Spec `synchro/consumer`, requirement "SyncRunner loop".
    [[nodiscard]] const SyncConsumer<Logger>& consumer() const noexcept { return *consumer_; }

    /// @brief The owned wait set.
    /// @return Mutable set reference.
    /// @note Spec `synchro/consumer`, requirement "SyncRunner loop".
    [[nodiscard]] com::EventConsumerSet<max_consumers>& wait_set() noexcept { return *wait_set_; }
    /// @brief The owned wait set.
    /// @return Const set reference.
    /// @note Spec `synchro/consumer`, requirement "SyncRunner loop".
    [[nodiscard]] const com::EventConsumerSet<max_consumers>& wait_set() const noexcept {
        return *wait_set_;
    }

    /// @brief Consumer statistics snapshot.
    /// @return Copy of the consumer counters.
    /// @note Spec `synchro/consumer`, requirement "Consumer observability".
    [[nodiscard]] SyncConsumerStats stats() const noexcept { return consumer_->stats(); }

    /// @brief Render the cold-path diagnostic string.
    /// @return Deterministic runner summary.
    /// @note Spec `synchro/consumer`, requirement "Consumer observability".
    [[nodiscard]] std::string to_string() const {
        return std::format("SyncRunner(consumers={}, set={})", wait_set_->size(),
                           wait_set_->to_string());
    }

private:
    /// @brief Private constructor; `create()` is the only entry point.
    SyncRunner(std::unique_ptr<SyncConsumer<Logger>> consumer,
               std::unique_ptr<com::EventConsumerSet<max_consumers>> wait_set,
               Logger& logger) noexcept
        : consumer_(std::move(consumer)), wait_set_(std::move(wait_set)), logger_(&logger) {}

    std::unique_ptr<SyncConsumer<Logger>> consumer_{};                 ///< Owned consumer.
    std::unique_ptr<com::EventConsumerSet<max_consumers>> wait_set_{}; ///< Owned wait set.
    Logger* logger_{nullptr}; ///< Borrowed logger; never owned.
};

} // namespace nexigen::synchro

/// @brief Formats a runner as its diagnostic string.
/// @tparam Logger Logger type of the formatted runner.
template <nexigen::logging::concepts::Logger Logger>
struct std::formatter<nexigen::synchro::SyncRunner<Logger>> : std::formatter<std::string_view> {
    /// @brief Write the runner diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param runner Runner to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::synchro::SyncRunner<Logger>& runner, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(runner.to_string(), ctx);
    }
};
