// nexigen::synchro — synchronization consumer (change add-synchro-event-sync;
// spec synchro/consumer; design D7/D8/D10/D13).
//
// SyncConsumer wraps one com::EventConsumer<SyncPayload> (EventConsumer is
// final, so this is a separate IEventConsumer) and adds three layers on top of
// the transport:
//   1. logical phase continuity (SyncTracker),
//   2. latency measurement and late-event policy,
//   3. phase-handler dispatch and a watchdog.
//
// It owns no thread. An EventConsumerSet or an application loop calls drain(),
// and every callback runs on that calling thread.
#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/com/error.hpp>
#include <nexigen/com/event_channel.hpp>
#include <nexigen/com/event_consumer.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/com/i_event_consumer.hpp>
#include <nexigen/com/loss_info.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/null_logger.hpp>
#include <nexigen/metrics/concurrency_policy.hpp>
#include <nexigen/metrics/counter.hpp>
#include <nexigen/metrics/explicit_histogram.hpp>
#include <nexigen/metrics/gauge.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/synchro/detail/sync_clock.hpp>
#include <nexigen/synchro/late_event_policy.hpp>
#include <nexigen/synchro/sync_consumer_config.hpp>
#include <nexigen/synchro/sync_consumer_state.hpp>
#include <nexigen/synchro/sync_consumer_stats.hpp>
#include <nexigen/synchro/sync_context.hpp>
#include <nexigen/synchro/sync_error.hpp>
#include <nexigen/synchro/sync_payload.hpp>
#include <nexigen/synchro/sync_tracker.hpp>

namespace nexigen::synchro {

/// @brief Consumer of one synchronization channel with phase handlers.
///
/// The consumer validates every delivered payload against its configured grid,
/// reconciles transport loss with generator skips, measures IPC and end-to-end
/// latency, applies the late-event policy, and dispatches registered handlers.
/// It also maintains the `Unsynced`/`Locked` watchdog state.
///
/// @tparam Logger Logger type satisfying `logging::concepts::Logger`; defaults
///         to `logging::NullLogger`.
/// @note Spec `synchro/consumer`, requirement "Consumer endpoint and set
///       attachment".
/// @warning The logger and meter must outlive the consumer. The meter must be
///          dedicated to this channel.
/// @warning Statistics and handler registration are single-threaded: use the
///          runner thread (or the `drain()` caller) only.
template <logging::concepts::Logger Logger = logging::NullLogger>
class SyncConsumer final : public com::IEventConsumer {
public:
    /// @brief Handler invoked for one accepted synchronization event.
    using Handler = std::function<void(const SyncContext&)>;

    /// @brief Create the consumer, its handler table, metrics, and transport.
    ///
    /// @param node Node that must outlive the consumer.
    /// @param config Service, expected grid, latency budget, and watchdog windows.
    /// @param logger Logger receiving lifecycle and anomaly records; must
    ///        outlive the consumer.
    /// @param meter Meter receiving the seven consumer instruments; must
    ///        outlive the consumer and be dedicated to this channel.
    /// @return Heap consumer in `Unsynced`, or a structured `SyncError`.
    /// @note Spec `synchro/consumer`, requirements "Consumer endpoint and set
    ///       attachment" and "Consumer observability".
    [[nodiscard]] static SyncResult<std::unique_ptr<SyncConsumer>>
    create(com::Node& node, const SyncConsumerConfig& config, Logger& logger,
           metrics::Meter& meter) noexcept {
        const core::Status valid = config.validate();
        if (!valid.has_value()) {
            return std::unexpected(make_config_error());
        }
        auto now = detail::raw_now_ns();
        if (!now.has_value()) {
            return std::unexpected(now.error());
        }
        auto registered = register_metrics(meter);
        if (!registered.has_value()) {
            return std::unexpected(registered.error());
        }

        const std::uint32_t handler_count = config.channel.phases_per_cycle;
        auto handlers = std::unique_ptr<Handler[]>(
            new (std::nothrow) Handler[static_cast<std::size_t>(handler_count)]);
        if (handlers == nullptr) {
            return std::unexpected(make_lifecycle_error(core::ErrorCode::ResourceExhausted));
        }

        auto* raw = new (std::nothrow) SyncConsumer(config, logger, meter, std::move(handlers),
                                                    handler_count, *now, *registered);
        if (raw == nullptr) {
            return std::unexpected(make_lifecycle_error(core::ErrorCode::ResourceExhausted));
        }

        auto name = com::ServiceName::create(config.channel.service_name.c_str());
        if (!name.has_value()) {
            delete raw;
            return std::unexpected(make_transport_error(name.error()));
        }
        auto channel =
            com::EventChannel<SyncPayload>::create(node, std::move(*name), config.channel.channel);
        if (!channel.has_value()) {
            delete raw;
            return std::unexpected(make_transport_error(channel.error()));
        }
        auto endpoint = channel->consumer(EventCallback{raw}, LossCallback{raw});
        if (!endpoint.has_value()) {
            delete raw;
            return std::unexpected(make_transport_error(endpoint.error()));
        }
        raw->endpoint_.emplace(std::move(*endpoint));
        return std::unique_ptr<SyncConsumer>{raw};
    }

    /// @brief Destroy the transport endpoint and the handler table.
    ~SyncConsumer() override = default;

    /// @brief Copy construction is deleted: callbacks capture the object address.
    SyncConsumer(const SyncConsumer&) = delete;
    /// @brief Copy assignment is deleted: callbacks capture the object address.
    SyncConsumer& operator=(const SyncConsumer&) = delete;
    /// @brief Move construction is deleted: callbacks capture the object address.
    SyncConsumer(SyncConsumer&&) = delete;
    /// @brief Move assignment is deleted: callbacks capture the object address.
    SyncConsumer& operator=(SyncConsumer&&) = delete;

    /// @brief Drain every queued event through the handlers.
    /// @return Empty status, or the first transport receive failure.
    /// @note Spec `synchro/consumer`, requirement "Consumer endpoint and set
    ///       attachment".
    [[nodiscard]] com::Status drain() noexcept override {
        if (!endpoint_.has_value()) {
            return std::unexpected(com::Error::InvalidState);
        }
        return endpoint_->drain();
    }

#if defined(NEXIGEN_SYNCHRO_TESTING)
    /// @brief Feed one transport loss observation to the normal loss path.
    ///
    /// Compiled only into the test target so the duplicate, reset, restart, and
    /// gap counters can be exercised without synthesizing transport faults.
    ///
    /// @param info Loss record forwarded to `handle_loss`.
    /// @note Test-only surface; absent from production builds.
    void inject_loss(const com::LossInfo& info) noexcept { handle_loss(info); }
#endif

    /// @brief Listener used by an `EventConsumerSet`.
    /// @return Borrowed listener; it must outlive the attachment.
    /// @note Spec `synchro/consumer`, requirement "Consumer endpoint and set
    ///       attachment".
    [[nodiscard]] const com::EventListener& listener() const noexcept override {
        return endpoint_->listener();
    }

    /// @brief Whether this consumer is attached to a set.
    /// @return `true` while set-attached.
    /// @note Spec `synchro/consumer`, requirement "Consumer endpoint and set
    ///       attachment".
    [[nodiscard]] bool attached_to_set() const noexcept override {
        return endpoint_.has_value() && endpoint_->attached_to_set();
    }

    /// @brief Mark or clear the set-attached state.
    /// @param attached New attachment state.
    /// @note Spec `synchro/consumer`, requirement "Consumer endpoint and set
    ///       attachment".
    void set_attached_to_set(bool attached) noexcept override {
        if (endpoint_.has_value()) {
            endpoint_->set_attached_to_set(attached);
        }
    }

    /// @brief Number of delivered events.
    /// @return Cumulative delivered count.
    /// @note Spec `synchro/consumer`, requirement "Consumer endpoint and set
    ///       attachment".
    [[nodiscard]] std::uint64_t received() const noexcept override {
        return endpoint_.has_value() ? endpoint_->received() : 0U;
    }

    /// @brief Register a handler invoked for every accepted phase.
    /// @param handler Callable taking a `const SyncContext&`; registration may
    ///        allocate, dispatch never does.
    /// @return Empty status, or `Lifecycle`/`InvalidConfig` when the table is
    ///         unavailable.
    /// @note Spec `synchro/consumer`, requirement "Phase handler dispatch".
    [[nodiscard]] SyncStatus on_any_phase(Handler handler) noexcept {
        if (!handlers_) {
            return std::unexpected(make_lifecycle_error(core::ErrorCode::Conflict));
        }
        any_handler_ = std::move(handler);
        return {};
    }

    /// @brief Register a handler for one phase index.
    /// @param phase_index Zero-based phase index inside the cycle.
    /// @param handler Callable taking a `const SyncContext&`.
    /// @return Empty status, or `InvalidConfig` for an out-of-range index.
    /// @note Spec `synchro/consumer`, requirement "Phase handler dispatch".
    [[nodiscard]] SyncStatus on_phase(std::uint32_t phase_index, Handler handler) noexcept {
        if (!handlers_ || phase_index >= handler_count_) {
            return std::unexpected(make_config_error());
        }
        handlers_[phase_index] = std::move(handler);
        return {};
    }

    /// @brief Register a handler for phase zero of every cycle.
    /// @param handler Callable taking a `const SyncContext&`.
    /// @return Empty status, or `InvalidConfig` when the table is unavailable.
    /// @note Spec `synchro/consumer`, requirement "Phase handler dispatch";
    ///       `on_cycle_start` is an alias for `on_phase(0, ...)`.
    [[nodiscard]] SyncStatus on_cycle_start(Handler handler) noexcept {
        return on_phase(0U, std::move(handler));
    }

    /// @brief Check the watchdog against an explicit raw instant.
    ///
    /// Transitions to `Unsynced`, increments the loss counter, and logs an
    /// error when the first lock or the last accepted event is overdue. Repeated
    /// checks after the transition report nothing until synchronization is
    /// reacquired.
    ///
    /// @param now_ns Current raw-monotonic instant.
    /// @return `true` when this call reported a new synchronization loss.
    /// @note Spec `synchro/consumer`, requirement "Synchronization watchdog".
    bool check_sync_loss(std::uint64_t now_ns) noexcept {
        if (now_ns < watchdog_deadline_ns()) {
            return false;
        }
        if (state_.load(std::memory_order_acquire) == SyncConsumerState::Locked) {
            set_state(SyncConsumerState::Unsynced);
            ++stats_.sync_losses;
            logger_->error("sync consumer lost synchronization: silence exceeded {} ns",
                           config_.resolve_loss_timeout(cycle_period_ns_));
            return true;
        }
        if (!has_first_ && !initial_loss_reported_) {
            initial_loss_reported_ = true;
            ++stats_.sync_losses;
            logger_->error("sync consumer initial synchronization timeout: {}",
                           config_.resolve_initial_timeout(config_.channel.cycle_period_ns));
            return true;
        }
        return false;
    }

    /// @brief Read the raw clock and check the watchdog.
    /// @return `true` when a new synchronization loss was reported; `false` on
    ///         a healthy lock, an already-reported loss, or a clock fault.
    /// @note Spec `synchro/consumer`, requirement "Synchronization watchdog".
    bool check_timeout() noexcept {
        auto now = detail::raw_now_ns();
        if (!now.has_value()) {
            logger_->error("sync consumer clock read failed: {}", now.error().to_string());
            return false;
        }
        return check_sync_loss(*now);
    }

    /// @brief Absolute raw instant at which the watchdog expires.
    /// @return Deadline derived from the last accepted event or from creation.
    /// @note Spec `synchro/consumer`, requirement "Synchronization watchdog".
    [[nodiscard]] std::uint64_t watchdog_deadline_ns() const noexcept {
        const std::uint64_t period =
            cycle_period_ns_ == 0U ? config_.channel.cycle_period_ns : cycle_period_ns_;
        if (has_first_) {
            return last_event_time_ns_ + config_.resolve_loss_timeout(period);
        }
        return create_time_ns_ + config_.resolve_initial_timeout(config_.channel.cycle_period_ns);
    }

    /// @brief Milliseconds until the watchdog expires, clamped for `wait_for`.
    ///
    /// Rounds up so the wait never wakes early, and clamps to `[1, 1000]` ms to
    /// avoid a zero-timeout busy loop and an unbounded sleep.
    ///
    /// @return Wait budget for `EventConsumerSet::wait_for`.
    /// @note Spec `synchro/consumer`, requirement "SyncRunner loop"; task 6.3.
    [[nodiscard]] std::chrono::milliseconds next_wait_timeout() const noexcept {
        const auto now = chrono::Timestamp::now();
        if (!now.has_value()) {
            return std::chrono::milliseconds{1};
        }
        const std::uint64_t deadline = watchdog_deadline_ns();
        const std::uint64_t current = now->nanoseconds();
        const std::uint64_t remaining = deadline > current ? deadline - current : 0U;
        std::uint64_t millis = (remaining + 999'999U) / 1'000'000U;
        if (millis < 1U) {
            millis = 1U;
        }
        if (millis > 1'000U) {
            millis = 1'000U;
        }
        return std::chrono::milliseconds{static_cast<std::int64_t>(millis)};
    }

    /// @brief Current synchronization lock state (acquire load).
    /// @return `Locked` or `Unsynced`.
    /// @note Spec `synchro/consumer`, requirement "Synchronization watchdog".
    [[nodiscard]] SyncConsumerState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    /// @brief Dispatch-thread statistics snapshot.
    /// @return Copy of the counters as last written by the dispatch thread.
    /// @warning Reading while another thread drains is a data race by design.
    /// @note Spec `synchro/consumer`, requirement "Consumer observability".
    [[nodiscard]] SyncConsumerStats stats() const noexcept { return stats_; }

    /// @brief Render the cold-path diagnostic string.
    /// @return Deterministic consumer summary.
    /// @note Spec `synchro/consumer`, requirement "Consumer observability".
    [[nodiscard]] std::string to_string() const override {
        return std::format(
            "SyncConsumer(service={}, state={}, received={}, resyncs={}, sync_losses={})",
            config_.channel.service_name, nexigen::synchro::to_string(state()), stats_.received,
            stats_.resyncs, stats_.sync_losses);
    }

private:
    /// @brief Raw instrument pointers owned by the dedicated meter.
    struct SyncMetrics {
        metrics::Counter<metrics::ConcurrencyPolicy::MultiThreaded>* received{
            nullptr}; ///< Accepted events.
        metrics::Counter<metrics::ConcurrencyPolicy::MultiThreaded>* dropped_late{
            nullptr}; ///< Late drops.
        metrics::Counter<metrics::ConcurrencyPolicy::MultiThreaded>* transport_lost{
            nullptr}; ///< Lost samples.
        metrics::Counter<metrics::ConcurrencyPolicy::MultiThreaded>* generator_skips{
            nullptr}; ///< Skipped phases.
        metrics::ExplicitBucketHistogram<metrics::ConcurrencyPolicy::MultiThreaded>* ipc_latency{
            nullptr}; ///< Publication-to-receive latency.
        metrics::ExplicitBucketHistogram<metrics::ConcurrencyPolicy::MultiThreaded>* e2e_latency{
            nullptr}; ///< Scheduled-to-receive latency.
        metrics::Gauge<std::int64_t, metrics::ConcurrencyPolicy::MultiThreaded>* locked{
            nullptr}; ///< 1 when locked, 0 when unsynced.
    };

    /// @brief Trampoline from the transport event callback to the owner.
    struct EventCallback {
        SyncConsumer* owner{nullptr}; ///< Owner address; stable for the object lifetime.

        /// @brief Forward one delivered event.
        /// @param header Event header.
        /// @param payload Synchronization payload.
        void operator()(const com::EventHeader& header, const SyncPayload& payload) noexcept {
            owner->handle_event(header, payload);
        }
    };

    /// @brief Trampoline from the transport loss callback to the owner.
    struct LossCallback {
        SyncConsumer* owner{nullptr}; ///< Owner address; stable for the object lifetime.

        /// @brief Forward one loss observation.
        /// @param info Transport loss classification.
        void operator()(const com::LossInfo& info) noexcept { owner->handle_loss(info); }
    };

    /// @brief Concrete transport endpoint type.
    using Endpoint = com::EventConsumer<SyncPayload, EventCallback, LossCallback>;

    /// @brief Private constructor; `create()` is the only entry point.
    SyncConsumer(const SyncConsumerConfig& config, Logger& logger, metrics::Meter& meter,
                 std::unique_ptr<Handler[]> handlers, std::uint32_t handler_count,
                 std::uint64_t create_time_ns, const SyncMetrics& metrics) noexcept
        : config_(config), logger_(&logger), meter_(&meter), metrics_(metrics),
          handlers_(std::move(handlers)), handler_count_(handler_count),
          create_time_ns_(create_time_ns) {}

    /// @brief Register the seven consumer instruments on the dedicated meter.
    /// @param meter Meter that must outlive the consumer.
    /// @return Instrument pointers, or the first registration error.
    [[nodiscard]] static SyncResult<SyncMetrics> register_metrics(metrics::Meter& meter) noexcept {
        static constexpr std::array<double, 12> latency_bounds{
            -1'000'000.0, -100'000.0, -10'000.0,   -1'000.0,     0.0,           1'000.0,
            10'000.0,     100'000.0,  1'000'000.0, 10'000'000.0, 100'000'000.0, 1'000'000'000.0};

        SyncMetrics m{};
        auto received = meter.create_counter<metrics::ConcurrencyPolicy::MultiThreaded>(
            "sync_consumer_received_total");
        if (!received.has_value()) {
            return std::unexpected(make_metrics_error(received.error()));
        }
        m.received = *received;

        auto dropped = meter.create_counter<metrics::ConcurrencyPolicy::MultiThreaded>(
            "sync_consumer_dropped_late_total");
        if (!dropped.has_value()) {
            return std::unexpected(make_metrics_error(dropped.error()));
        }
        m.dropped_late = *dropped;

        auto lost = meter.create_counter<metrics::ConcurrencyPolicy::MultiThreaded>(
            "sync_consumer_transport_lost_total");
        if (!lost.has_value()) {
            return std::unexpected(make_metrics_error(lost.error()));
        }
        m.transport_lost = *lost;

        auto skips = meter.create_counter<metrics::ConcurrencyPolicy::MultiThreaded>(
            "sync_consumer_generator_skips_total");
        if (!skips.has_value()) {
            return std::unexpected(make_metrics_error(skips.error()));
        }
        m.generator_skips = *skips;

        auto ipc = meter.create_explicit_histogram<metrics::ConcurrencyPolicy::MultiThreaded>(
            "sync_consumer_ipc_latency_ns", std::span<const double>{latency_bounds}, "ns");
        if (!ipc.has_value()) {
            return std::unexpected(make_metrics_error(ipc.error()));
        }
        m.ipc_latency = *ipc;

        auto e2e = meter.create_explicit_histogram<metrics::ConcurrencyPolicy::MultiThreaded>(
            "sync_consumer_e2e_latency_ns", std::span<const double>{latency_bounds}, "ns");
        if (!e2e.has_value()) {
            return std::unexpected(make_metrics_error(e2e.error()));
        }
        m.e2e_latency = *e2e;

        auto locked = meter.create_gauge<std::int64_t, metrics::ConcurrencyPolicy::MultiThreaded>(
            "sync_consumer_locked");
        if (!locked.has_value()) {
            return std::unexpected(make_metrics_error(locked.error()));
        }
        m.locked = *locked;
        return m;
    }

    /// @brief Publish the lock state and mirror it into the gauge.
    /// @param next New state.
    void set_state(SyncConsumerState next) noexcept {
        state_.store(next, std::memory_order_release);
        if (metrics_.locked != nullptr) {
            metrics_.locked->set(next == SyncConsumerState::Locked ? 1 : 0);
        }
    }

    /// @brief Handle one transport loss observation.
    ///
    /// Runs before the event callback for the triggering sample, so the pending
    /// gap/restart/reset is visible to `SyncTracker::observe`.
    ///
    /// @param info Transport loss classification.
    void handle_loss(const com::LossInfo& info) noexcept {
        tracker_.note_loss(info);
        switch (info.reason) {
            case com::LossReason::TransportGap:
                stats_.transport_lost += info.lost_count;
                if (metrics_.transport_lost != nullptr) {
                    metrics_.transport_lost->add(info.lost_count);
                }
                logger_->warning("sync consumer transport gap: lost {} samples", info.lost_count);
                break;
            case com::LossReason::Duplicate:
                ++stats_.duplicates;
                logger_->debug("sync consumer dropped a duplicate transport sample");
                break;
            case com::LossReason::ProducerRestart:
                ++stats_.producer_restarts;
                logger_->warning("sync consumer observed a producer restart: epoch {} -> {}",
                                 info.old_epoch, info.new_epoch);
                break;
            case com::LossReason::UnexpectedSequenceReset:
                ++stats_.unexpected_resets;
                logger_->warning("sync consumer observed an unexpected sequence reset");
                break;
        }
    }

    /// @brief Restart continuity when the payload grid disagrees with the config.
    /// @param payload Delivered payload.
    void reset_on_grid_mismatch(const SyncPayload& payload) noexcept {
        if (payload.phases_total == config_.channel.phases_per_cycle || !tracker_.has_position()) {
            return;
        }
        tracker_.reset();
        ++stats_.resyncs;
        logger_->warning("sync consumer grid mismatch: payload phases={} configured={}",
                         payload.phases_total, config_.channel.phases_per_cycle);
    }

    /// @brief Count and log logical continuity anomalies.
    /// @param observation Tracker result for the delivered event.
    void account_observation(const SyncObservation& observation) noexcept {
        if (observation.generator_skips != 0U) {
            stats_.generator_skips += observation.generator_skips;
            if (metrics_.generator_skips != nullptr) {
                metrics_.generator_skips->add(observation.generator_skips);
            }
            logger_->warning("sync consumer observed {} skipped phase slots",
                             observation.generator_skips);
        }
        if (observation.resync) {
            ++stats_.resyncs;
            logger_->warning("sync consumer resynchronized logical continuity: absolute_phase={}",
                             observation.absolute_phase);
        }
    }

    /// @brief Latencies and late classification of one delivered event.
    struct Timing {
        std::int64_t ipc_latency_ns{0};  ///< Receive minus publication instant.
        std::int64_t e2e_latency_ns{0};  ///< Receive minus scheduled instant.
        std::uint64_t max_latency_ns{0}; ///< Effective lateness budget.
        bool late{false};                ///< End-to-end latency exceeded the budget.
    };

    /// @brief Measure both latencies and classify lateness.
    /// @param header Event header with the publication instant.
    /// @param payload Payload with the scheduled instant.
    /// @param receive_time Raw instant sampled by the consumer.
    /// @return Timing of the delivered event.
    [[nodiscard]] Timing measure(const com::EventHeader& header, const SyncPayload& payload,
                                 std::uint64_t receive_time) const noexcept {
        Timing timing{};
        timing.ipc_latency_ns = detail::signed_delta_ns(receive_time, header.timestamp_ns);
        timing.e2e_latency_ns = detail::signed_delta_ns(receive_time, payload.scheduled_time_ns);
        timing.max_latency_ns = config_.resolve_max_latency(cycle_period_ns_);
        timing.late = timing.e2e_latency_ns > 0 &&
                      static_cast<std::uint64_t>(timing.e2e_latency_ns) > timing.max_latency_ns;
        return timing;
    }

    /// @brief Build the dispatch context of one accepted event.
    /// @param header Event header.
    /// @param payload Delivered payload.
    /// @param receive_time Raw instant sampled by the consumer.
    /// @param timing Measured latencies and lateness.
    /// @return Context passed to the handlers.
    [[nodiscard]] static SyncContext make_context(const com::EventHeader& header,
                                                  const SyncPayload& payload,
                                                  std::uint64_t receive_time,
                                                  const Timing& timing) noexcept {
        SyncContext context{};
        context.cycle_number = payload.cycle_number;
        context.phase_index = payload.phase_index;
        context.phases_total = payload.phases_total;
        context.scheduled_time_ns = payload.scheduled_time_ns;
        context.cycle_period_ns = payload.cycle_period_ns;
        context.publish_time_ns = header.timestamp_ns;
        context.receive_time_ns = receive_time;
        context.sequence_id = header.sequence_id;
        context.producer_epoch = header.producer_epoch;
        context.ipc_latency_ns = timing.ipc_latency_ns;
        context.e2e_latency_ns = timing.e2e_latency_ns;
        context.is_late = timing.late;
        return context;
    }

    /// @brief Record latency and identity telemetry.
    /// @param header Event header.
    /// @param observation Tracker result for the delivered event.
    /// @param timing Measured latencies.
    void record_timing(const com::EventHeader& header, const SyncObservation& observation,
                       const Timing& timing) noexcept {
        stats_.last_ipc_latency_ns = timing.ipc_latency_ns;
        stats_.last_e2e_latency_ns = timing.e2e_latency_ns;
        if (timing.e2e_latency_ns > 0) {
            const auto magnitude = static_cast<std::uint64_t>(timing.e2e_latency_ns);
            if (magnitude > stats_.max_e2e_latency_ns) {
                stats_.max_e2e_latency_ns = magnitude;
            }
        }
        stats_.last_sequence = header.sequence_id;
        stats_.last_epoch = header.producer_epoch;
        stats_.absolute_phase = observation.absolute_phase;
        if (metrics_.ipc_latency != nullptr) {
            metrics_.ipc_latency->record(static_cast<double>(timing.ipc_latency_ns));
        }
        if (metrics_.e2e_latency != nullptr) {
            metrics_.e2e_latency->record(static_cast<double>(timing.e2e_latency_ns));
        }
    }

    /// @brief Enter `Locked` on the first event and log the transition.
    /// @param prior_locked State observed before this event was handled.
    /// @param prior_first Whether an event had been accepted before.
    /// @param context Context of the accepted phase.
    void note_lock(bool prior_locked, bool prior_first, const SyncContext& context) noexcept {
        if (prior_locked) {
            return;
        }
        set_state(SyncConsumerState::Locked);
        if (!prior_first) {
            logger_->info("sync consumer acquired synchronization: cycle={} phase={}",
                          context.cycle_number, context.phase_index);
        } else {
            logger_->info("sync consumer reacquired synchronization: cycle={} phase={}",
                          context.cycle_number, context.phase_index);
        }
    }

    /// @brief Handle one delivered event.
    /// @param header Event header with epoch and publication timestamp.
    /// @param payload Synchronization payload.
    void handle_event(const com::EventHeader& header, const SyncPayload& payload) noexcept {
        auto now = detail::raw_now_ns();
        if (!now.has_value()) {
            logger_->error("sync consumer clock read failed: {}", now.error().to_string());
            return;
        }
        const std::uint64_t receive_time = *now;
        const bool prior_locked =
            state_.load(std::memory_order_acquire) == SyncConsumerState::Locked;
        const bool prior_first = has_first_;

        reset_on_grid_mismatch(payload);
        const SyncObservation observation = tracker_.observe(header, payload);
        account_observation(observation);

        has_first_ = true;
        cycle_period_ns_ = payload.cycle_period_ns == 0U ? config_.channel.cycle_period_ns
                                                         : payload.cycle_period_ns;
        last_event_time_ns_ = receive_time;

        ++stats_.received;
        if (metrics_.received != nullptr) {
            metrics_.received->inc();
        }

        const Timing timing = measure(header, payload, receive_time);
        const SyncContext context = make_context(header, payload, receive_time, timing);
        record_timing(header, observation, timing);
        note_lock(prior_locked, prior_first, context);

        if (timing.late) {
            if (config_.late_policy == LateEventPolicy::DropAndWarn) {
                ++stats_.dropped_late;
                if (metrics_.dropped_late != nullptr) {
                    metrics_.dropped_late->inc();
                }
                logger_->warning("sync consumer dropped a late phase: e2e={} ns budget={} ns",
                                 timing.e2e_latency_ns, timing.max_latency_ns);
                return;
            }
            ++stats_.late_delivered;
        }

        dispatch(context);
    }

    /// @brief Run the registered handlers in deterministic order.
    /// @param context Context of the accepted phase.
    void dispatch(const SyncContext& context) noexcept {
        if (any_handler_) {
            any_handler_(context);
        }
        if (handlers_ && context.phase_index < handler_count_ && handlers_[context.phase_index]) {
            handlers_[context.phase_index](context);
        }
    }

    SyncConsumerConfig config_{};           ///< Immutable consumer configuration.
    Logger* logger_{nullptr};               ///< Borrowed logger; never owned.
    metrics::Meter* meter_{nullptr};        ///< Borrowed dedicated meter; never owned.
    SyncMetrics metrics_{};                 ///< Raw instrument pointers.
    std::unique_ptr<Handler[]> handlers_{}; ///< Phase-indexed handlers; sized at creation.
    std::uint32_t handler_count_{0};        ///< Number of phase slots.
    Handler any_handler_{};                 ///< Handler invoked before the phase handler.
    std::optional<Endpoint> endpoint_{};    ///< Owned transport endpoint.
    SyncTracker tracker_{};                 ///< Logical continuity tracker.
    SyncConsumerStats stats_{};             ///< Dispatch-thread counters.
    std::uint64_t create_time_ns_{0};       ///< Creation instant for the initial watchdog.
    std::uint64_t last_event_time_ns_{0};   ///< Receive instant of the last accepted event.
    std::uint64_t cycle_period_ns_{0};      ///< Payload period, or the configured default.
    bool has_first_{false};                 ///< At least one event has been accepted.
    bool initial_loss_reported_{false};     ///< The initial timeout was already reported.
    alignas(64) std::atomic<SyncConsumerState> state_{SyncConsumerState::Unsynced}; ///< Lock state.
};

} // namespace nexigen::synchro

/// @brief Formats a consumer as its diagnostic string.
/// @tparam Logger Logger type of the formatted consumer.
template <nexigen::logging::concepts::Logger Logger>
struct std::formatter<nexigen::synchro::SyncConsumer<Logger>> : std::formatter<std::string_view> {
    /// @brief Write the consumer diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param consumer Consumer to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::synchro::SyncConsumer<Logger>& consumer, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(consumer.to_string(), ctx);
    }
};
