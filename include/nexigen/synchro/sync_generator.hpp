// nexigen::synchro — real-time phase generator (change add-synchro-event-sync;
// spec synchro/generator; design D2/D4/D5/D6/D10/D13).
//
// SyncGenerator publishes one event per phase on an absolute CLOCK_MONOTONIC_RAW
// grid. Every deadline is derived from the resolved start instant and the
// absolute phase index, so a missed wake-up never moves the grid. The worker
// sleeps in ppoll on a private eventfd until the final spin_threshold_ns, then
// spins on the raw clock; stop() writes the eventfd through a stop callback.
//
// LIFETIME: the type is non-copyable and non-movable (the producer, the worker,
// and the eventfd are address-bound). create() is the only construction path and
// returns a heap object. The caller-supplied Logger and metrics::Meter must
// outlive the generator; the generator keeps no ownership of either.
#pragma once

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <nexigen/com/event_channel.hpp>
#include <nexigen/com/event_producer.hpp>
#include <nexigen/com/event_time.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publish_outcome.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/timestamp_domain.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/worker_thread.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/null_logger.hpp>
#include <nexigen/metrics/concurrency_policy.hpp>
#include <nexigen/metrics/counter.hpp>
#include <nexigen/metrics/explicit_histogram.hpp>
#include <nexigen/metrics/gauge.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/synchro/detail/sync_clock.hpp>
#include <nexigen/synchro/detail/sync_faults.hpp>
#include <nexigen/synchro/sync_config.hpp>
#include <nexigen/synchro/sync_error.hpp>
#include <nexigen/synchro/sync_generator_stats.hpp>
#include <nexigen/synchro/sync_payload.hpp>
#include <nexigen/synchro/sync_schedule.hpp>
#include <nexigen/synchro/sync_state.hpp>

namespace nexigen::synchro {

/// @brief Publishes phase-aligned synchronization events on one `com` channel.
///
/// The generator owns one `EventProducer<SyncPayload>` and one real-time worker.
/// Creation validates the configuration and registers every metric before any
/// transport port is opened. `start()` resolves the configured start policy,
/// `start_at()` applies a one-shot target, and `stop()` interrupts the wait and
/// joins the worker.
///
/// @tparam Logger Logger type satisfying `logging::concepts::Logger`; defaults
///         to `logging::NullLogger`.
/// @note Spec `synchro/generator`, requirement "Generator lifecycle and
///       ownership".
/// @warning The logger and meter must outlive the generator. The meter must be
///          dedicated to this channel: the metric names are fixed and duplicate
///          registration fails.
/// @warning `proc_stats()` renders the transport endpoint and is a cold-path
///          diagnostic; `stats()` is a lock-free atomic snapshot safe to read
///          while the worker runs.
template <logging::concepts::Logger Logger = logging::NullLogger> class SyncGenerator {
public:
    /// @brief Create a generator after validating configuration and metrics.
    ///
    /// Registration order is configuration -> metrics -> transport, so a metric
    /// failure leaves no transport endpoint open.
    ///
    /// @param node Node that must outlive the generator.
    /// @param config Channel, start policy, spin window, and thread attributes.
    /// @param logger Logger receiving lifecycle and anomaly records; must
    ///        outlive the generator.
    /// @param meter Meter receiving the five generator instruments; must
    ///        outlive the generator and be dedicated to this channel.
    /// @return Heap generator in `Stopped`, or a structured `SyncError`.
    /// @note Spec `synchro/generator`, requirements "Generator lifecycle and
    ///       ownership" and "Generator observability".
    /// @note Spec `synchro/protocol`, requirement "Synchronization error model".
    [[nodiscard]] static SyncResult<std::unique_ptr<SyncGenerator>>
    create(com::Node& node, const SyncConfig& config, Logger& logger,
           metrics::Meter& meter) noexcept {
        const core::Status valid = config.validate();
        if (!valid.has_value()) {
            return std::unexpected(make_config_error());
        }
        auto registered = register_metrics(meter);
        if (!registered.has_value()) {
            return std::unexpected(registered.error());
        }
        auto name = com::ServiceName::create(config.channel.service_name.c_str());
        if (!name.has_value()) {
            return std::unexpected(make_transport_error(name.error()));
        }
        auto channel =
            com::EventChannel<SyncPayload>::create(node, std::move(*name), config.channel.channel);
        if (!channel.has_value()) {
            return std::unexpected(make_transport_error(channel.error()));
        }
        auto producer = channel->producer();
        if (!producer.has_value()) {
            return std::unexpected(make_transport_error(producer.error()));
        }

        if (detail::eventfd_failure_armed()) {
            return std::unexpected(make_os_error(SyncErrorKind::EventFd, EMFILE));
        }
        const int stop_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (stop_fd < 0) {
            return std::unexpected(make_os_error(SyncErrorKind::EventFd, errno));
        }

        auto* raw = new (std::nothrow)
            SyncGenerator(config, logger, meter, std::move(*producer), *registered, stop_fd);
        if (raw == nullptr) {
            ::close(stop_fd);
            return std::unexpected(make_lifecycle_error(core::ErrorCode::ResourceExhausted));
        }
        return std::unique_ptr<SyncGenerator>{raw};
    }

    /// @brief Stop the worker, join it, and close the wake eventfd.
    ~SyncGenerator() {
        worker_.stop();
        if (stop_fd_ >= 0) {
            static_cast<void>(::close(stop_fd_));
            stop_fd_ = -1;
        }
    }

    /// @brief Copy construction is deleted: the generator owns address-bound state.
    SyncGenerator(const SyncGenerator&) = delete;
    /// @brief Copy assignment is deleted: the generator owns address-bound state.
    SyncGenerator& operator=(const SyncGenerator&) = delete;
    /// @brief Move construction is deleted: the worker, producer, and eventfd are address-bound.
    SyncGenerator(SyncGenerator&&) = delete;
    /// @brief Move assignment is deleted: the worker, producer, and eventfd are address-bound.
    SyncGenerator& operator=(SyncGenerator&&) = delete;

    /// @brief Start a lifecycle from the configured start policy.
    ///
    /// Rejected while `Running` or `WaitingForStart`; a `Stopped` or `Faulted`
    /// generator starts a new lifecycle whose producer epoch and sequence are
    /// reset by the worker before the first publication.
    ///
    /// @return Empty status, or a `Lifecycle`, `Clock`, `Thread`, or
    ///         `InvalidConfig` error.
    /// @note Spec `synchro/generator`, requirement "Start and stop transitions".
    [[nodiscard]] SyncStatus start() noexcept { return start_impl(0U, false); }

    /// @brief Start a lifecycle at a one-shot target instant.
    ///
    /// The stored `SyncConfig` is not modified: only this start uses the target,
    /// subject to the configured alignment and past-target policy.
    ///
    /// @param target_time_ns Raw-monotonic target instant.
    /// @return Empty status, or a structured `SyncError`.
    /// @note Spec `synchro/generator`, requirement "Start and stop transitions".
    [[nodiscard]] SyncStatus start_at(std::uint64_t target_time_ns) noexcept {
        return start_impl(target_time_ns, true);
    }

    /// @brief Interrupt the wait, join the worker, and enter `Stopped`.
    ///
    /// Safe when the generator is already stopped. Must not be called from the
    /// worker itself.
    ///
    /// @note Spec `synchro/generator`, requirement "Start and stop transitions".
    void stop() noexcept {
        worker_.stop();
        set_state(SyncState::Stopped);
    }

    /// @brief Current lifecycle state (acquire load).
    /// @return Published state.
    /// @note Spec `synchro/generator`, requirement "Generator observability".
    [[nodiscard]] SyncState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    /// @brief Worker-written statistics snapshot.
    /// @return Lock-free copy of the counters as last written by the worker.
    /// @note Each field has exactly one writer (the worker), so the snapshot
    ///       may straddle one publication step but never races with it.
    /// @note Spec `synchro/generator`, requirement "Generator observability".
    [[nodiscard]] SyncGeneratorStats stats() const noexcept { return stats_.snapshot(); }

    /// @brief Diagnostics of the owned producer (cold path).
    /// @return Producer diagnostic string.
    /// @note Spec `synchro/generator`, requirement "Generator observability";
    ///       the transport endpoint renders its own cold-path state.
    [[nodiscard]] std::string proc_stats() const { return producer_.to_string(); }

    /// @brief Render the cold-path diagnostic string.
    /// @return Deterministic generator summary.
    /// @note Spec `synchro/generator`, requirement "Generator observability".
    [[nodiscard]] std::string to_string() const {
        return std::format("SyncGenerator(service={}, state={}, stats={})",
                           config_.channel.service_name, nexigen::synchro::to_string(state()),
                           stats().to_string());
    }

private:
    /// @brief Raw instrument pointers owned by the dedicated meter.
    ///
    /// No ownership: the registry owns every instrument and the caller owns the
    /// meter/provider. All instruments are `MultiThreaded` so the provider
    /// harvester can read while the worker records.
    struct SyncMetrics {
        metrics::Counter<metrics::ConcurrencyPolicy::MultiThreaded>* cycles{
            nullptr}; ///< Completed cycles.
        metrics::Counter<metrics::ConcurrencyPolicy::MultiThreaded>* phases{
            nullptr}; ///< Published phases.
        metrics::Counter<metrics::ConcurrencyPolicy::MultiThreaded>* overruns{
            nullptr}; ///< Skipped slots.
        metrics::ExplicitBucketHistogram<metrics::ConcurrencyPolicy::MultiThreaded>* jitter{
            nullptr}; ///< Signed publication jitter.
        metrics::Gauge<std::int64_t, metrics::ConcurrencyPolicy::MultiThreaded>* state{
            nullptr}; ///< Lifecycle code.
    };

    /// @brief Worker body forwarding the stop token to the generator loop.
    struct Body {
        SyncGenerator* owner{nullptr}; ///< Immortal owner while the worker runs.

        /// @brief Run the generator publication loop.
        /// @param stop Stop token supplied by `WorkerThread`.
        void run(std::stop_token stop) noexcept { owner->run_worker(stop); }
    };

    /// @brief Private constructor; `create()` is the only entry point.
    SyncGenerator(const SyncConfig& config, Logger& logger, metrics::Meter& meter,
                  com::EventProducer<SyncPayload> producer, const SyncMetrics& metrics,
                  int stop_fd) noexcept
        : config_(config), logger_(&logger), meter_(&meter), metrics_(metrics),
          producer_(std::move(producer)), stop_fd_(stop_fd) {}

    /// @brief Register the five generator instruments on the dedicated meter.
    /// @param meter Meter that must outlive the generator.
    /// @return Instrument pointers, or the first registration error.
    /// @note Spec `synchro/generator`, requirement "Generator observability";
    ///       design D10.
    [[nodiscard]] static SyncResult<SyncMetrics> register_metrics(metrics::Meter& meter) noexcept {
        static constexpr std::array<double, 11> jitter_bounds{
            -10'000'000.0, -1'000'000.0, -100'000.0, -10'000.0,   -1'000.0,    0.0,
            1'000.0,       10'000.0,     100'000.0,  1'000'000.0, 10'000'000.0};

        SyncMetrics m{};
        auto cycles =
            meter.create_counter<metrics::ConcurrencyPolicy::MultiThreaded>("sync_cycles_total");
        if (!cycles.has_value()) {
            return std::unexpected(make_metrics_error(cycles.error()));
        }
        m.cycles = *cycles;

        auto phases =
            meter.create_counter<metrics::ConcurrencyPolicy::MultiThreaded>("sync_phases_total");
        if (!phases.has_value()) {
            return std::unexpected(make_metrics_error(phases.error()));
        }
        m.phases = *phases;

        auto overruns =
            meter.create_counter<metrics::ConcurrencyPolicy::MultiThreaded>("sync_overruns_total");
        if (!overruns.has_value()) {
            return std::unexpected(make_metrics_error(overruns.error()));
        }
        m.overruns = *overruns;

        auto jitter = meter.create_explicit_histogram<metrics::ConcurrencyPolicy::MultiThreaded>(
            "sync_jitter_ns", std::span<const double>{jitter_bounds}, "ns");
        if (!jitter.has_value()) {
            return std::unexpected(make_metrics_error(jitter.error()));
        }
        m.jitter = *jitter;

        auto state = meter.create_gauge<std::int64_t, metrics::ConcurrencyPolicy::MultiThreaded>(
            "sync_state");
        if (!state.has_value()) {
            return std::unexpected(make_metrics_error(state.error()));
        }
        m.state = *state;
        return m;
    }

    /// @brief Publish one lifecycle state and mirror it into the gauge.
    /// @param next New state.
    void set_state(SyncState next) noexcept {
        state_.store(next, std::memory_order_release);
        if (metrics_.state != nullptr) {
            metrics_.state->set(static_cast<std::int64_t>(next));
        }
    }

    /// @brief Shared `start()`/`start_at()` implementation.
    /// @param target One-shot target instant.
    /// @param has_target `true` when `start_at()` supplied the target.
    /// @return Empty status or a structured error.
    [[nodiscard]] SyncStatus start_impl(std::uint64_t target, bool has_target) noexcept {
        const SyncState current = state_.load(std::memory_order_acquire);
        if (current == SyncState::Running || current == SyncState::WaitingForStart) {
            return std::unexpected(make_lifecycle_error(core::ErrorCode::Conflict));
        }
        if (worker_.joinable()) {
            worker_.join();
        }

        auto now = detail::raw_now_ns();
        if (!now.has_value()) {
            return std::unexpected(now.error());
        }
        auto resolved =
            resolve_start(*now, config_.channel.cycle_period_ns, config_.start, has_target, target);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        if (resolved->realigned) {
            logger_->warning("sync generator target is not in the future; realigned to {} ns",
                             resolved->start_time_ns);
        }

        stats_.reset();
        stats_.start_time_ns.store(resolved->start_time_ns, std::memory_order_relaxed);
        schedule_.emplace(resolved->start_time_ns, config_.channel.cycle_period_ns,
                          config_.channel.phases_per_cycle);
        set_state(SyncState::WaitingForStart);
        logger_->info("sync generator starting: service={} start_ns={} cycle_period_ns={} "
                      "phases={}",
                      config_.channel.service_name, resolved->start_time_ns,
                      config_.channel.cycle_period_ns, config_.channel.phases_per_cycle);

        const core::Status started = worker_.start(config_.thread, Body{this});
        if (!started.has_value()) {
            set_state(SyncState::Stopped);
            return std::unexpected(
                SyncError{SyncErrorKind::Thread, com::Error::Ok, started.error(), 0});
        }
        return {};
    }

    /// @brief Result of one bounded wait for an absolute deadline.
    enum class WaitResult : std::uint8_t {
        Deadline, ///< The deadline was reached, so the slot is publishable.
        Stopped,  ///< Stop was requested while waiting.
        Faulted,  ///< A clock or eventfd fault was reported; the state is `Faulted`.
    };

    /// @brief Worker entry: arm the stop wake, reset the epoch, publish the grid.
    /// @param stop Stop token supplied by `WorkerThread`.
    void run_worker(std::stop_token stop) noexcept {
        if (!schedule_.has_value()) {
            finish_stopped();
            return;
        }
        std::stop_callback wake{stop, [this] { wake_eventfd(); }};
        // EventProducer::on_source_reset() must run on the publishing thread.
        producer_.on_source_reset();

        const WaitResult start_wait = wait_until(schedule_->start_time_ns(), stop);
        if (start_wait != WaitResult::Deadline) {
            finish_wait(start_wait);
            return;
        }
        set_state(SyncState::Running);
        logger_->info("sync generator running: service={}", config_.channel.service_name);

        std::uint64_t absolute_phase = 0U;
        while (!stop.stop_requested()) {
            const std::uint64_t deadline = schedule_->deadline(absolute_phase);
            const WaitResult phase_wait = wait_until(deadline, stop);
            if (phase_wait != WaitResult::Deadline) {
                finish_wait(phase_wait);
                return;
            }
            // One-shot test hook: a worker stall must be classified as an
            // overrun instead of shifting the grid. No-op in production.
            detail::apply_worker_stall();
            auto now = detail::raw_now_ns();
            if (!now.has_value()) {
                fault(now.error());
                return;
            }

            const OverrunSkip skip = schedule_->classify_overrun(absolute_phase, *now);
            if (skip.skipped != 0U) {
                absolute_phase = skip.next_phase;
                stats_.overrun_slots.fetch_add(skip.skipped, std::memory_order_relaxed);
                stats_.overrun_episodes.fetch_add(1U, std::memory_order_relaxed);
                if (metrics_.overruns != nullptr) {
                    metrics_.overruns->add(skip.skipped);
                }
                logger_->warning("sync generator overrun: skipped {} phase slots", skip.skipped);
                continue;
            }

            publish_phase(absolute_phase, deadline, *now);
            ++absolute_phase;
        }
        finish_stopped();
    }

    /// @brief Send one phase sample through the owned producer.
    ///
    /// The test-only fault hooks make both publication failure arms reachable:
    /// `fail_send` rejects the sample, while `fail_notify` reports a delivered
    /// send whose wake notification failed. Both hooks compile out of
    /// production.
    ///
    /// @param payload Sample to publish.
    /// @param publish_time Actual raw publication instant.
    /// @return Outcome, or the transport error that rejected the sample.
    [[nodiscard]] com::Result<com::PublishOutcome>
    publish_sample(const SyncPayload& payload, std::uint64_t publish_time) noexcept {
        if (detail::send_failure_armed()) {
            return std::unexpected(com::Error::UnableToDeliver);
        }
        auto outcome =
            producer_.publish(payload, config_.event_code,
                              com::EventTime{publish_time, com::TimestampDomain::MonotonicRaw});
        if (outcome.has_value() && detail::notify_failure_armed()) {
            *outcome = com::PublishOutcome::NotifyFailed;
        }
        return outcome;
    }

    /// @brief Publish one phase and update telemetry.
    /// @param absolute_phase Absolute slot index.
    /// @param deadline Scheduled raw instant of the slot.
    /// @param publish_time Raw instant sampled immediately before publication.
    void publish_phase(std::uint64_t absolute_phase, std::uint64_t deadline,
                       std::uint64_t publish_time) noexcept {
        detail::note_publish_thread();
        const std::uint64_t phases = schedule_->phases_per_cycle();
        if (phases == 0U) {
            return;
        }
        const std::uint64_t cycle = absolute_phase / phases;
        const auto phase = static_cast<std::uint32_t>(absolute_phase % phases);
        const SyncPayload payload{cycle, phase, static_cast<std::uint32_t>(phases), deadline,
                                  schedule_->cycle_period_ns()};

        auto outcome = publish_sample(payload, publish_time);
        if (!outcome.has_value()) {
            stats_.send_failures.fetch_add(1U, std::memory_order_relaxed);
            logger_->error("sync generator publish failed: {}", com::to_string(outcome.error()));
            return;
        }

        stats_.phases_published.fetch_add(1U, std::memory_order_relaxed);
        if (metrics_.phases != nullptr) {
            metrics_.phases->inc();
        }
        if (*outcome == com::PublishOutcome::NotifyFailed) {
            stats_.notify_failures.fetch_add(1U, std::memory_order_relaxed);
            logger_->warning("sync generator wake notification failed after publication");
        }

        const std::int64_t jitter = detail::signed_delta_ns(publish_time, deadline);
        stats_.last_jitter_ns.store(jitter, std::memory_order_relaxed);
        stats_.last_phase_time_ns.store(publish_time, std::memory_order_relaxed);
        const std::uint64_t magnitude =
            jitter < 0 ? static_cast<std::uint64_t>(-jitter) : static_cast<std::uint64_t>(jitter);
        // Single writer: a relaxed load/compare/store is enough to keep the
        // running maximum monotonic for readers.
        if (magnitude > stats_.max_jitter_ns.load(std::memory_order_relaxed)) {
            stats_.max_jitter_ns.store(magnitude, std::memory_order_relaxed);
        }
        if (metrics_.jitter != nullptr) {
            metrics_.jitter->record(static_cast<double>(jitter));
        }

        if (phase == static_cast<std::uint32_t>(phases - 1U)) {
            stats_.cycles_completed.fetch_add(1U, std::memory_order_relaxed);
            if (metrics_.cycles != nullptr) {
                metrics_.cycles->inc();
            }
        }
    }

    /// @brief Wait for an absolute raw deadline, or return early on stop/fault.
    ///
    /// Sleeps in `ppoll` on the wake eventfd while more than the spin threshold
    /// remains, then spins on the raw clock for the final window. `EINTR`
    /// recomputes the remaining time from the same absolute deadline.
    ///
    /// @param deadline Raw-monotonic deadline.
    /// @param stop Stop token checked before every blocking or spinning step.
    /// @return `Deadline` when the deadline was reached, `Stopped` when stop was
    ///         requested, or `Faulted` when a clock/eventfd fault was reported.
    /// @note Spec `synchro/generator`, requirement "Hybrid absolute wait".
    [[nodiscard]] WaitResult wait_until(std::uint64_t deadline, std::stop_token stop) noexcept {
        while (true) {
            if (stop.stop_requested()) {
                return WaitResult::Stopped;
            }
            auto now = detail::raw_now_ns();
            if (!now.has_value()) {
                fault(now.error());
                return WaitResult::Faulted;
            }
            if (*now >= deadline) {
                return WaitResult::Deadline;
            }
            const std::uint64_t remaining = deadline - *now;
            if (remaining <= config_.spin_threshold_ns) {
                while (true) {
                    if (stop.stop_requested()) {
                        return WaitResult::Stopped;
                    }
                    detail::note_spin();
                    auto spin_now = detail::raw_now_ns();
                    if (!spin_now.has_value()) {
                        fault(spin_now.error());
                        return WaitResult::Faulted;
                    }
                    if (*spin_now >= deadline) {
                        return WaitResult::Deadline;
                    }
                }
            }
            const std::uint64_t sleep_ns = remaining - config_.spin_threshold_ns;
            ::timespec timeout{};
            timeout.tv_sec = static_cast<time_t>(sleep_ns / 1'000'000'000ULL);
            timeout.tv_nsec = static_cast<long>(sleep_ns % 1'000'000'000ULL);
            ::pollfd descriptor{stop_fd_, POLLIN, 0};
            detail::note_coarse_wait();
            const int waited = ::ppoll(&descriptor, 1, &timeout, nullptr);
            if (waited < 0) {
                if (errno == EINTR) {
                    detail::note_eintr();
                    continue; // recompute from the same absolute deadline
                }
                fault(make_os_error(SyncErrorKind::EventFd, errno));
                return WaitResult::Faulted;
            }
            if (waited > 0 && (descriptor.revents & POLLIN) != 0) {
                drain_eventfd();
                if (stop.stop_requested()) {
                    return WaitResult::Stopped;
                }
            }
        }
    }

    /// @brief Write one unit to the wake eventfd.
    void wake_eventfd() noexcept {
        const std::uint64_t value = 1U;
        // A full eventfd write either succeeds or fails with EAGAIN when the
        // counter is already saturated; both cases leave the wake pending.
        const ssize_t written = ::write(stop_fd_, &value, sizeof(value));
        static_cast<void>(written);
    }

    /// @brief Read every pending wake unit without blocking.
    void drain_eventfd() noexcept {
        std::uint64_t value = 0U;
        while (::read(stop_fd_, &value, sizeof(value)) == static_cast<ssize_t>(sizeof(value))) {}
    }

    /// @brief Enter `Faulted` and log the cause.
    /// @param error Structured fault.
    void fault(const SyncError& error) noexcept {
        set_state(SyncState::Faulted);
        logger_->error("sync generator faulted: {}", error.to_string());
    }

    /// @brief Finish the worker without overwriting a reported fault.
    /// @param result Result of the failed wait.
    void finish_wait(WaitResult result) noexcept {
        if (result == WaitResult::Stopped) {
            finish_stopped();
        }
    }

    /// @brief Enter `Stopped` and log the lifecycle milestone.
    void finish_stopped() noexcept {
        set_state(SyncState::Stopped);
        const SyncGeneratorStats snapshot = stats_.snapshot();
        logger_->info("sync generator stopped: published={} overruns={}", snapshot.phases_published,
                      snapshot.overrun_slots);
    }

    /// @brief Worker-written counters published as relaxed atomics.
    ///
    /// Every field has exactly one writer (the generator worker), so relaxed
    /// operations preserve the value and never form a data race with a
    /// concurrent `stats()` reader. The copy is a telemetry snapshot, not a
    /// transactionally consistent view.
    struct alignas(64) StatsStorage {
        std::atomic<std::uint64_t> cycles_completed{0};   ///< Completed cycles.
        std::atomic<std::uint64_t> phases_published{0};   ///< Published phases.
        std::atomic<std::uint64_t> overrun_slots{0};      ///< Skipped phase slots.
        std::atomic<std::uint64_t> overrun_episodes{0};   ///< Wake-ups with a skip.
        std::atomic<std::uint64_t> send_failures{0};      ///< Rejected publications.
        std::atomic<std::uint64_t> notify_failures{0};    ///< Failed wake notifications.
        std::atomic<std::uint64_t> start_time_ns{0};      ///< Resolved start instant.
        std::atomic<std::uint64_t> last_phase_time_ns{0}; ///< Last publication instant.
        std::atomic<std::int64_t> last_jitter_ns{0};      ///< Last signed jitter.
        std::atomic<std::uint64_t> max_jitter_ns{0};      ///< Largest absolute jitter.

        /// @brief Zero every counter for a new lifecycle.
        void reset() noexcept {
            cycles_completed.store(0U, std::memory_order_relaxed);
            phases_published.store(0U, std::memory_order_relaxed);
            overrun_slots.store(0U, std::memory_order_relaxed);
            overrun_episodes.store(0U, std::memory_order_relaxed);
            send_failures.store(0U, std::memory_order_relaxed);
            notify_failures.store(0U, std::memory_order_relaxed);
            start_time_ns.store(0U, std::memory_order_relaxed);
            last_phase_time_ns.store(0U, std::memory_order_relaxed);
            last_jitter_ns.store(0, std::memory_order_relaxed);
            max_jitter_ns.store(0U, std::memory_order_relaxed);
        }

        /// @brief Copy the counters into a plain statistics value.
        /// @return Lock-free snapshot.
        [[nodiscard]] SyncGeneratorStats snapshot() const noexcept {
            SyncGeneratorStats out{};
            out.cycles_completed = cycles_completed.load(std::memory_order_relaxed);
            out.phases_published = phases_published.load(std::memory_order_relaxed);
            out.overrun_slots = overrun_slots.load(std::memory_order_relaxed);
            out.overrun_episodes = overrun_episodes.load(std::memory_order_relaxed);
            out.send_failures = send_failures.load(std::memory_order_relaxed);
            out.notify_failures = notify_failures.load(std::memory_order_relaxed);
            out.start_time_ns = start_time_ns.load(std::memory_order_relaxed);
            out.last_phase_time_ns = last_phase_time_ns.load(std::memory_order_relaxed);
            out.last_jitter_ns = last_jitter_ns.load(std::memory_order_relaxed);
            out.max_jitter_ns = max_jitter_ns.load(std::memory_order_relaxed);
            return out;
        }
    };

    SyncConfig config_{};                      ///< Immutable generator configuration.
    Logger* logger_{nullptr};                  ///< Borrowed logger; never owned.
    metrics::Meter* meter_{nullptr};           ///< Borrowed dedicated meter; never owned.
    SyncMetrics metrics_{};                    ///< Raw instrument pointers.
    com::EventProducer<SyncPayload> producer_; ///< Owned transport endpoint.
    std::optional<SyncSchedule> schedule_{};   ///< Resolved grid of the active lifecycle.
    StatsStorage stats_{};                     ///< Worker-written atomic counters.
    alignas(64) std::atomic<SyncState> state_{
        SyncState::Stopped};            ///< Cross-thread lifecycle state.
    int stop_fd_{-1};                   ///< Wake eventfd (`EFD_NONBLOCK | EFD_CLOEXEC`).
    core::WorkerThread<Body> worker_{}; ///< LAST member: joins before the fields above die.
};

} // namespace nexigen::synchro

/// @brief Formats a generator as its diagnostic string.
/// @tparam Logger Logger type of the formatted generator.
template <nexigen::logging::concepts::Logger Logger>
struct std::formatter<nexigen::synchro::SyncGenerator<Logger>> : std::formatter<std::string_view> {
    /// @brief Write the generator diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param generator Generator to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::synchro::SyncGenerator<Logger>& generator, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(generator.to_string(), ctx);
    }
};
