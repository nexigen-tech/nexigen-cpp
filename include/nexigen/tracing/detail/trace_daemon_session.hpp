// nexigen::tracing — testable TraceDaemon core (change add-trace-daemon; spec
// tracing/trace-daemon; design D1–D9).
//
// One thread blocks in a WaitSet (interval drain + recovery-notification
// wake-up), moves SpanFrame records into the bounded ring, and — when the PHM
// recovery channel reports an incident — freezes the ring into a versioned
// `.nxtr` snapshot. Steady state performs no disk I/O.
//
// State is single-threaded (drain and dump run on the same thread); no atomics
// or memory orders are introduced. The tool's main() is a thin CLI around this
// session so the end-to-end test drives the real loop in-process.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <cerrno>
#include <sys/mman.h>
#include <unistd.h>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/chrono/utc_offset.hpp>
#include <nexigen/com/callback_progression.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/waitset.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/exec/app_state.hpp>
#include <nexigen/exec/application_state.hpp>
#include <nexigen/sm/recovery_notification.hpp>
#include <nexigen/tracing/detail/span_ring.hpp>
#include <nexigen/tracing/detail/trace_dump.hpp>
#include <nexigen/tracing/span_frame.hpp>
#include <nexigen/tracing/trace_daemon_config.hpp>

namespace nexigen::tracing::detail {

/// @brief Testable core of the trace daemon: ports, ring, and the
/// drain/trigger/dump cycle.
///
/// Opens the span subscriber, the recovery subscriber, and a recovery event
/// listener; blocks in a `WaitSet` (interval drain plus recovery-notification
/// wake-up); moves `SpanFrame` records into the bounded ring; and — when the
/// PHM recovery channel reports an incident — freezes the ring into a versioned
/// `.nxtr` snapshot. Steady state performs no disk I/O.
///
/// Drain and dump run on one thread, so all state is single-threaded: no
/// atomics or memory orders are introduced. The tool's `main()` is a thin CLI
/// around this session, which lets the end-to-end test drive the real loop
/// in-process. Copy and move are deleted; `start()` is called once.
///
/// @note Spec `tracing/trace-daemon`, requirements "Daemon drains spans
///       through a blocking wait set", "A recovery notification triggers one
///       snapshot", and "Lifecycle and steady-state I/O"; design D1–D9.
class TraceDaemonSession {
public:
    /// @brief Store the configuration; no port, thread, or directory is
    /// touched until `start()`.
    ///
    /// @param config Validated-at-startup daemon configuration, copied into
    ///        the session.
    explicit TraceDaemonSession(const TraceDaemonConfig& config) noexcept : config_{config} {}

    /// @brief Stop the session if it is still running.
    ///
    /// Idempotent: `stop()` performs no snapshot write and tolerates an
    /// already-stopped session.
    ~TraceDaemonSession() { stop(); }

    /// @brief Copy construction is deleted: the session owns live ports.
    TraceDaemonSession(const TraceDaemonSession&) = delete;
    /// @brief Copy assignment is deleted: the session owns live ports.
    TraceDaemonSession& operator=(const TraceDaemonSession&) = delete;
    /// @brief Move construction is deleted: guards and ports are not movable
    /// here.
    TraceDaemonSession(TraceDaemonSession&&) = delete;
    /// @brief Move assignment is deleted: guards and ports are not movable
    /// here.
    TraceDaemonSession& operator=(TraceDaemonSession&&) = delete;

    /// @brief Validate configuration and bring the daemon up.
    ///
    /// Validates the configuration, creates the dump directory, allocates the
    /// ring, attempts `mlockall` best-effort, applies the configured thread
    /// spec to the calling thread, samples the UTC offset, opens the ports and
    /// waitset, then reports `Running`. On any port/waitset/reporter failure
    /// every already-opened resource is closed again through `stop()`.
    ///
    /// @return Success when the daemon is running; the configuration's
    ///         validation status for a bad config; otherwise
    ///         `core::ErrorCode::SystemError` for a directory, port, waitset,
    ///         or readiness-report failure.
    /// @warning Call once, from the thread that will run the loop, because the
    ///          thread spec is applied to the caller.
    /// @note Spec `tracing/trace-daemon`, requirements "Lifecycle and
    ///       steady-state I/O" and "Trace daemon configuration"; design D1–D9.
    [[nodiscard]] core::Status start() noexcept {
        const core::Status valid = config_.validate();
        if (!valid) {
            return valid;
        }
        const core::Status directory = create_directories(config_.dump_directory);
        if (!directory) {
            return directory;
        }
        ring_.emplace(static_cast<std::size_t>(config_.ring_capacity_records));

        if (::mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
            mlock_failed_ = true;
            std::fprintf(stderr, "nexigen-traced: mlockall failed: %s\n", std::strerror(errno));
        }
        apply_affinity();
        static_cast<void>(chrono::sample_utc_offset(utc_offset_ns_));

        const core::Status ports = open_ports();
        if (!ports) {
            return ports;
        }
        const core::Status waitset = open_waitset();
        if (!waitset) {
            return waitset;
        }
        const core::Status ready = report_ready();
        if (!ready) {
            return ready;
        }
        started_ = true;
        return {};
    }

    /// @brief Perform one `WaitSet` cycle.
    ///
    /// Blocks until the drain interval expires or the recovery event fires,
    /// then drains all available spans, collects recovery activations, and
    /// writes at most one snapshot for this wake-up when a trigger is pending.
    ///
    /// @return Success after a completed cycle;
    ///         `core::ErrorCode::NotInitialized` when `start()` has not
    ///         succeeded; `core::ErrorCode::SystemError` when the waitset wait
    ///         itself fails.
    /// @note Spec `tracing/trace-daemon`, requirement "Daemon drains spans
    ///       through a blocking wait set"; design D1–D9.
    [[nodiscard]] core::Status wait_and_process() noexcept {
        if (!waitset_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        const auto waited = waitset_->wait_and_process_once([this](com::WaitSetAttachmentId) {
            process();
            return com::CallbackProgression::Continue;
        });
        if (!waited) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// @brief Run the drain/trigger/dump loop until asked to stop.
    ///
    /// Checks `stop` after every interval or recovery event and exits early if
    /// a cycle reports an error.
    ///
    /// @param stop Stop flag owned by the caller's lifecycle code; read with
    ///        relaxed ordering, never written here.
    /// @note Spec `tracing/trace-daemon`, requirement "Lifecycle and
    ///       steady-state I/O". The loop blocks in the kernel while idle
    ///       instead of polling.
    void run(const std::atomic<bool>& stop) noexcept {
        while (!stop.load(std::memory_order_relaxed)) {
            if (!wait_and_process()) {
                break;
            }
        }
    }

    /// @brief Close every port in reverse dependency order.
    ///
    /// Retained counters and the ring stay readable afterwards; no snapshot is
    /// written here, so a graceful stop never produces a dump.
    ///
    /// @note Spec `tracing/trace-daemon`, requirement "Lifecycle and
    ///       steady-state I/O". Idempotent and allocation-free.
    void stop() noexcept {
        started_ = false;
        reporter_.reset();
        recovery_guard_.reset();
        interval_guard_.reset();
        waitset_.reset();
        recovery_listener_.reset();
        recovery_subscriber_.reset();
        span_subscriber_.reset();
        node_.reset();
    }

    // --- diagnostics -------------------------------------------------------

    /// @brief Whether `start()` completed successfully.
    ///
    /// @return `true` after a successful `start()` until `stop()` runs.
    [[nodiscard]] bool started() const noexcept { return started_; }
    /// @brief Access the ring, if one was allocated.
    ///
    /// @return Pointer to the retained-frame ring, or `nullptr` before
    ///         `start()`.
    [[nodiscard]] const SpanRing* ring() const noexcept { return ring_ ? &*ring_ : nullptr; }
    /// @brief Total span frames accepted into the ring.
    ///
    /// @return Cumulative count of frames pushed since construction.
    [[nodiscard]] std::uint64_t spans_received() const noexcept { return spans_received_; }
    /// @brief Total recovery triggers observed.
    ///
    /// @return Cumulative count of wake-ups that produced a trigger, whether
    ///         or not the snapshot write succeeded.
    [[nodiscard]] std::uint64_t triggers() const noexcept { return triggers_; }
    /// @brief Total snapshots written successfully.
    ///
    /// @return Cumulative count of dumps that completed and cleared the ring.
    [[nodiscard]] std::uint64_t dumps_written() const noexcept { return dumps_written_; }
    /// @brief Total snapshot write failures.
    ///
    /// @return Cumulative count of triggers whose dump failed; the ring is
    ///         then retained so the frames survive for the next attempt.
    [[nodiscard]] std::uint64_t dump_failures() const noexcept { return dump_failures_; }
    /// @brief Total receive errors from either subscriber.
    ///
    /// @return Cumulative count of failed `receive()` calls.
    [[nodiscard]] std::uint64_t receive_errors() const noexcept { return receive_errors_; }
    /// @brief Total recovery-listener errors.
    ///
    /// @return Cumulative count of failed event `try_wait()` calls.
    [[nodiscard]] std::uint64_t trigger_errors() const noexcept { return trigger_errors_; }
    /// @brief Whether `mlockall` was denied at startup.
    ///
    /// @return `true` when memory locking failed; startup continues regardless.
    [[nodiscard]] bool mlock_failed() const noexcept { return mlock_failed_; }
    /// @brief Outcome of applying the configured background thread spec.
    ///
    /// @return The thread-apply report captured by the most recent `start()`;
    ///         empty when affinity was never attempted.
    [[nodiscard]] const core::ThreadApplyReport& thread_report() const noexcept {
        return thread_report_;
    }
    /// @brief Path of the most recent successful snapshot.
    ///
    /// @return Dump path, or an empty string when no dump has been written.
    [[nodiscard]] const std::string& last_dump_path() const noexcept { return last_dump_path_; }

    /// @brief Render the session state for diagnostics.
    ///
    /// Cold path only; allocates a `std::string`.
    ///
    /// @return Text naming the service, span/retained/overwrite counts, trigger
    ///         and dump counters, and the started flag.
    /// @note Mandate 9. Cold path only; never called from the drain loop.
    [[nodiscard]] std::string to_string() const {
        const std::size_t retained = ring_ ? ring_->count() : 0U;
        const std::size_t capacity = ring_ ? ring_->capacity() : 0U;
        const std::uint64_t overwritten = ring_ ? ring_->overwritten() : 0U;
        return std::format(
            "TraceDaemonSession(service={}, spans={}, retained={}/{}, overwritten={}, "
            "triggers={}, dumps={}, dump_failures={}, started={})",
            config_.service_name, spans_received_, retained, capacity, overwritten, triggers_,
            dumps_written_, dump_failures_, started_);
    }

private:
    /// @brief Tear the session down and return a failure status.
    ///
    /// @param code Error to surface to the caller.
    /// @return Always `std::unexpected(code)`.
    /// @note Internal helper used by the startup path to roll back resources.
    [[nodiscard]] core::Status fail(core::ErrorCode code) noexcept {
        stop();
        return std::unexpected(code);
    }

    /// @brief Apply the configured background thread spec to the calling
    /// thread.
    ///
    /// Best-effort: the report is retained in `thread_report_` and a partial
    /// application is printed to stderr, but startup proceeds so span delivery
    /// is never blocked by an affinity failure.
    ///
    /// @note Spec `tracing/trace-daemon`, requirement "Lifecycle and
    ///       steady-state I/O"; spec `core/affinity`, requirements "Pinning the
    ///       calling thread by core mask" and "Single implementation behind
    ///       module shims". Cold path, called once from `start()`.
    void apply_affinity() noexcept {
        core::ThreadApplyReport report{};
        const core::Status affinity = core::apply_thread_spec_self(config_.bg_cpu_affinity, report);
        thread_report_ = report;
        if (!affinity) {
            std::fprintf(stderr, "nexigen-traced: thread affinity not fully applied: %s\n",
                         report.to_string().c_str());
        }
    }

    /// @brief Create the node, span subscriber, and recovery subscriber.
    ///
    /// The span subscriber is sized with `subscriber_max_buffer_depth`; the
    /// recovery subscriber uses a small fixed depth (8) because notifications
    /// coalesce.
    ///
    /// @return Success, or `core::ErrorCode::SystemError` (after rolling the
    ///         session back through `fail()`) when any port cannot be created.
    /// @note Spec `tracing/trace-daemon`, requirement "Daemon drains spans
    ///       through a blocking wait set".
    [[nodiscard]] core::Status open_ports() noexcept {
        auto node = com::Node::create();
        if (!node) {
            return fail(core::ErrorCode::SystemError);
        }
        node_.emplace(std::move(*node));

        const auto span_service = com::ServiceName::create(config_.service_name.c_str());
        if (!span_service) {
            return fail(core::ErrorCode::SystemError);
        }
        auto subscriber = com::Subscriber<SpanFrame>::create(*node_, *span_service,
                                                             config_.subscriber_max_buffer_depth,
                                                             config_.subscriber_max_buffer_depth);
        if (!subscriber) {
            return fail(core::ErrorCode::SystemError);
        }
        span_subscriber_.emplace(std::move(*subscriber));

        const auto recovery_service =
            com::ServiceName::create(config_.recovery_service_name.c_str());
        if (!recovery_service) {
            return fail(core::ErrorCode::SystemError);
        }
        auto recovery =
            com::Subscriber<sm::RecoveryNotification>::create(*node_, *recovery_service, 8U, 8U);
        if (!recovery) {
            return fail(core::ErrorCode::SystemError);
        }
        recovery_subscriber_.emplace(std::move(*recovery));
        return {};
    }

    /// @brief Create the recovery event listener and the waitset with its two
    /// attachments.
    ///
    /// Attaches the configured drain interval and a notification attachment on
    /// the recovery event listener, which is what wakes the loop for an
    /// incident.
    ///
    /// @return Success, or `core::ErrorCode::SystemError` (after rolling the
    ///         session back through `fail()`) when the listener, waitset, or
    ///         either attachment cannot be created.
    /// @note Spec `tracing/trace-daemon`, requirement "Daemon drains spans
    ///       through a blocking wait set".
    [[nodiscard]] core::Status open_waitset() noexcept {
        const auto event_service = com::ServiceName::create(config_.recovery_event_name.c_str());
        if (!event_service) {
            return fail(core::ErrorCode::SystemError);
        }
        auto listener = com::EventListener::create(*node_, *event_service);
        if (!listener) {
            return fail(core::ErrorCode::SystemError);
        }
        recovery_listener_.emplace(std::move(*listener));

        auto waitset = com::WaitSet::create_without_signal_handling();
        if (!waitset) {
            return fail(core::ErrorCode::SystemError);
        }
        waitset_.emplace(std::move(*waitset));

        const auto interval = std::chrono::milliseconds(config_.drain_interval_ms);
        auto timer_guard = waitset_->attach_interval(interval);
        if (!timer_guard) {
            return fail(core::ErrorCode::SystemError);
        }
        interval_guard_.emplace(std::move(*timer_guard));
        auto recovery_guard = waitset_->attach_notification(*recovery_listener_);
        if (!recovery_guard) {
            return fail(core::ErrorCode::SystemError);
        }
        recovery_guard_.emplace(std::move(*recovery_guard));
        return {};
    }

    /// @brief Publish `Running` on the application state channel.
    ///
    /// @return Success, or `core::ErrorCode::SystemError` (after rolling the
    ///         session back through `fail()`) when the reporter cannot be
    ///         created or the state cannot be published.
    /// @note Spec `tracing/trace-daemon`, requirement "Lifecycle and
    ///       steady-state I/O": readiness is reported before the drain loop
    ///       starts.
    [[nodiscard]] core::Status report_ready() noexcept {
        auto reporter = exec::ApplicationStateReporter::create(*node_, config_.app_name);
        if (!reporter) {
            return fail(core::ErrorCode::SystemError);
        }
        reporter_.emplace(std::move(*reporter));
        if (!reporter_->report(exec::AppState::Running)) {
            return fail(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// @brief One wake-up's work: drain, collect a trigger, maybe dump.
    ///
    /// Drains pending spans first, then (when `trigger_on_recovery` is set)
    /// looks for a recovery activation. A trigger always counts, but exactly
    /// one snapshot is attempted per wake-up: on success the ring is cleared
    /// and the dump counter advances; on failure the frames are retained and
    /// the failure counter advances.
    ///
    /// @note Spec `tracing/trace-daemon`, requirement "A recovery notification
    ///       triggers one snapshot". Runs on the loop thread only.
    void process() noexcept {
        if (!ring_ || !span_subscriber_) {
            return;
        }
        drain_spans();
        if (!config_.trigger_on_recovery) {
            return;
        }
        std::string group;
        if (!collect_trigger(group)) {
            return;
        }
        ++triggers_;
        if (write_snapshot(group).has_value()) {
            ring_->clear();
            ++dumps_written_;
        } else {
            ++dump_failures_;
        }
    }

    /// @brief Move every currently available span frame into the ring.
    ///
    /// Loops until the subscriber reports an empty queue; each received payload
    /// is copied into the ring in arrival order and counted. A receive error
    /// ends the drain and increments the error counter instead of throwing.
    ///
    /// @note Spec `tracing/trace-daemon`, requirement "Daemon drains spans
    ///       through a blocking wait set": the retained order matches publish
    ///       order.
    void drain_spans() noexcept {
        for (;;) {
            auto received = span_subscriber_->receive();
            if (!received) {
                ++receive_errors_;
                break;
            }
            if (!received->has_value()) {
                break;
            }
            ring_->push((*received)->payload());
            ++spans_received_;
        }
    }

    /// @brief Drain the recovery queue and consume its event activations.
    ///
    /// A wake-up with at least one activation is a trigger even when the
    /// payload queue is empty; two notifications in one wake-up coalesce into
    /// one trigger whose group is the last notification's function group.
    ///
    /// @param group Receives the NUL-trimmed function group of the last
    ///        notification; the caller passes a fresh string, so it stays empty
    ///        when no payload was queued.
    /// @return `true` when at least one activation arrived (write a snapshot),
    ///         `false` when there was none or the listener failed (the failure
    ///         increments the trigger-error counter).
    /// @note Spec `tracing/trace-daemon`, requirement "A recovery notification
    ///       triggers one snapshot".
    [[nodiscard]] bool collect_trigger(std::string& group) noexcept {
        if (!recovery_listener_ || !recovery_subscriber_) {
            return false;
        }
        std::uint64_t activations = 0U;
        const auto waited =
            recovery_listener_->try_wait([&activations](com::EventId) { ++activations; });
        if (!waited) {
            ++trigger_errors_;
            return false;
        }
        if (*waited == 0U) {
            return false;
        }
        for (;;) {
            auto received = recovery_subscriber_->receive();
            if (!received) {
                ++receive_errors_;
                break;
            }
            if (!received->has_value()) {
                break;
            }
            const auto& payload = (*received)->payload();
            std::size_t length = 0U;
            while (length < sizeof(payload.function_group) &&
                   payload.function_group[length] != '\0') {
                ++length;
            }
            group.assign(payload.function_group, length);
        }
        return true;
    }

    /// @brief Freeze the ring into one snapshot file.
    ///
    /// Samples the monotonic trigger time, converts it to UTC with the offset
    /// captured at startup, then probes `incident_trace_*` names with collision
    /// suffixes `0..999` until an unused path is found. Writes the dump through
    /// `write_trace_dump` (temp + fsync + rename), records the final path, and
    /// prunes the directory to `max_dump_files` best-effort.
    ///
    /// @param group PHM function group recorded in the header; truncated to 31
    ///        characters plus NUL by the writer.
    /// @return Success, `core::ErrorCode::NotInitialized` when the ring does
    ///         not exist, or `core::ErrorCode::SystemError` when no free file
    ///         name exists or the write fails.
    /// @note Spec `tracing/trace-daemon`, requirements "A snapshot is a
    ///       versioned binary record" and "Snapshot retention is bounded";
    ///       design D4–D6. Cold path.
    [[nodiscard]] core::Status write_snapshot(std::string_view group) noexcept {
        if (!ring_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        const auto now = chrono::Timestamp::now();
        const std::uint64_t trigger_ts_ns = now.has_value() ? now->nanoseconds() : 0ULL;
        const std::int64_t utc_ns = chrono::utc_from_mono_ns(trigger_ts_ns, utc_offset_ns_);

        std::string path;
        for (std::uint32_t collision = 0U; collision < 1000U; ++collision) {
            std::string candidate = config_.dump_directory;
            candidate += '/';
            candidate += snapshot_file_name(utc_ns, collision);
            if (::access(candidate.c_str(), F_OK) != 0) {
                path = std::move(candidate);
                break;
            }
        }
        if (path.empty()) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        const core::Status status = write_trace_dump(path, *ring_, group, trigger_ts_ns, true);
        if (!status) {
            return status;
        }
        last_dump_path_ = path;
        static_cast<void>(prune_dumps(config_.dump_directory, config_.max_dump_files));
        return {};
    }

    /// Immutable-by-convention startup configuration.
    TraceDaemonConfig config_{};
    /// Bounded flight-recorder ring; engaged by `start()`.
    std::optional<SpanRing> ring_{};
    /// Iceoryx2 node owning every port below.
    std::optional<com::Node> node_{};
    /// Incoming `SpanFrame` traffic.
    std::optional<com::Subscriber<SpanFrame>> span_subscriber_{};
    /// Recovery notifications carrying the PHM function group.
    std::optional<com::Subscriber<sm::RecoveryNotification>> recovery_subscriber_{};
    /// Recovery event activations consumed by the trigger path.
    std::optional<com::EventListener> recovery_listener_{};
    /// Interval plus notification waitset.
    std::optional<com::WaitSet> waitset_{};
    /// Attachment guard for the drain-interval timer.
    std::optional<com::WaitSetGuard> interval_guard_{};
    /// Attachment guard for the recovery notification.
    std::optional<com::WaitSetGuard> recovery_guard_{};
    /// Application-state publisher used to report `Running`.
    std::optional<exec::ApplicationStateReporter> reporter_{};
    /// UTC-minus-monotonic offset sampled once at startup.
    std::int64_t utc_offset_ns_{0};
    /// Per-attribute affinity/scheduler outcome of `apply_affinity()`.
    core::ThreadApplyReport thread_report_{};
    /// Path of the most recent successful snapshot.
    std::string last_dump_path_{};
    /// Cumulative frames pushed into the ring.
    std::uint64_t spans_received_{0U};
    /// Cumulative recovery triggers observed.
    std::uint64_t triggers_{0U};
    /// Cumulative snapshots written successfully.
    std::uint64_t dumps_written_{0U};
    /// Cumulative failed snapshot attempts.
    std::uint64_t dump_failures_{0U};
    /// Cumulative subscriber receive errors.
    std::uint64_t receive_errors_{0U};
    /// Cumulative recovery-listener errors.
    std::uint64_t trigger_errors_{0U};
    /// `mlockall` was denied at startup (non-fatal).
    bool mlock_failed_{false};
    /// `start()` completed successfully.
    bool started_{false};
};

} // namespace nexigen::tracing::detail
