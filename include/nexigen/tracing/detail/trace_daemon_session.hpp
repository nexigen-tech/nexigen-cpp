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

/// The daemon session: opens the ports, owns the ring, and performs the
/// drain/trigger/dump cycle. Copy and move are deleted; `start()` is called once.
class TraceDaemonSession {
public:
    explicit TraceDaemonSession(const TraceDaemonConfig& config) noexcept : config_{config} {}

    ~TraceDaemonSession() { stop(); }

    TraceDaemonSession(const TraceDaemonSession&) = delete;
    TraceDaemonSession& operator=(const TraceDaemonSession&) = delete;
    TraceDaemonSession(TraceDaemonSession&&) = delete;
    TraceDaemonSession& operator=(TraceDaemonSession&&) = delete;

    /// Validate, create the dump directory and ring, pin the thread, open the
    /// ports/waitset, and report Running.
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

    /// One WaitSet cycle: drain spans, collect recovery activations, dump when
    /// a trigger is pending. Blocks until the interval or the recovery event.
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

    /// Run until `stop` becomes true (checked after every interval/event).
    void run(const std::atomic<bool>& stop) noexcept {
        while (!stop.load(std::memory_order_relaxed)) {
            if (!wait_and_process()) {
                break;
            }
        }
    }

    /// Close every port in reverse dependency order. Retained counters and the
    /// ring stay readable; no snapshot is written here.
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

    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] const SpanRing* ring() const noexcept { return ring_ ? &*ring_ : nullptr; }
    [[nodiscard]] std::uint64_t spans_received() const noexcept { return spans_received_; }
    [[nodiscard]] std::uint64_t triggers() const noexcept { return triggers_; }
    [[nodiscard]] std::uint64_t dumps_written() const noexcept { return dumps_written_; }
    [[nodiscard]] std::uint64_t dump_failures() const noexcept { return dump_failures_; }
    [[nodiscard]] std::uint64_t receive_errors() const noexcept { return receive_errors_; }
    [[nodiscard]] std::uint64_t trigger_errors() const noexcept { return trigger_errors_; }
    [[nodiscard]] bool mlock_failed() const noexcept { return mlock_failed_; }
    [[nodiscard]] const core::ThreadApplyReport& thread_report() const noexcept {
        return thread_report_;
    }
    [[nodiscard]] const std::string& last_dump_path() const noexcept { return last_dump_path_; }

    /// Cold-path diagnostic (mandate 9).
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
    [[nodiscard]] core::Status fail(core::ErrorCode code) noexcept {
        stop();
        return std::unexpected(code);
    }

    void apply_affinity() noexcept {
        core::ThreadApplyReport report{};
        const core::Status affinity = core::apply_thread_spec_self(config_.bg_cpu_affinity, report);
        thread_report_ = report;
        if (!affinity) {
            std::fprintf(stderr, "nexigen-traced: thread affinity not fully applied: %s\n",
                         report.to_string().c_str());
        }
    }

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

    /// Drain the recovery queue and consume its event activations. Returns
    /// true when at least one wake-up arrived (a notification without a payload
    /// still triggers a dump with an empty group).
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

    TraceDaemonConfig config_{};
    std::optional<SpanRing> ring_{};
    std::optional<com::Node> node_{};
    std::optional<com::Subscriber<SpanFrame>> span_subscriber_{};
    std::optional<com::Subscriber<sm::RecoveryNotification>> recovery_subscriber_{};
    std::optional<com::EventListener> recovery_listener_{};
    std::optional<com::WaitSet> waitset_{};
    std::optional<com::WaitSetGuard> interval_guard_{};
    std::optional<com::WaitSetGuard> recovery_guard_{};
    std::optional<exec::ApplicationStateReporter> reporter_{};
    std::int64_t utc_offset_ns_{0};
    core::ThreadApplyReport thread_report_{};
    std::string last_dump_path_{};
    std::uint64_t spans_received_{0U};
    std::uint64_t triggers_{0U};
    std::uint64_t dumps_written_{0U};
    std::uint64_t dump_failures_{0U};
    std::uint64_t receive_errors_{0U};
    std::uint64_t trigger_errors_{0U};
    bool mlock_failed_{false};
    bool started_{false};
};

} // namespace nexigen::tracing::detail
