#include "capture_thread.hpp"

#include <chrono>
#include <cstdio>
#include <format>

#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>

#include <nexigen/com/service_name.hpp>
#include <nexigen/com/transport_header.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/logging/log_service_options.hpp>

namespace nexigen::logview {

namespace {

bool affinity_is_allowed(const nexigen::core::CpuSet& requested) noexcept {
    if (requested.empty()) {
        return true;
    }
    cpu_set_t current{};
    if (::sched_getaffinity(0, sizeof(current), &current) != 0) {
        return false;
    }
    for (std::uint32_t cpu = 0; cpu <= nexigen::core::CpuSet::max_cpu_id; ++cpu) {
        if (!requested.contains(cpu)) {
            continue;
        }
        if (cpu >= static_cast<std::uint32_t>(CPU_SETSIZE) || !CPU_ISSET(cpu, &current)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string CaptureStats::to_string() const {
    return std::format("CaptureStats(received={}, rejected={}, overflow={}, publishers={}, "
                       "ingress={}, error={})",
                       received, rejected, overflow, publishers, ingress_size, last_error);
}

CaptureThread::CaptureThread(std::string service, nexigen::core::ThreadSpec spec)
    : service_(std::move(service)), spec_(std::move(spec)) {}

CaptureThread::~CaptureThread() {
    stop();
}

nexigen::core::Status CaptureThread::start() noexcept {
    if (worker_.joinable()) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    stopping_.store(false, std::memory_order_release);
    if (!affinity_is_allowed(spec_.cpus())) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    const auto validated = spec_.validate();
    if (!validated) {
        return validated;
    }
    auto node = nexigen::com::Node::create();
    if (!node) {
        last_error_.store(1);
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    node_ = std::move(*node);
    const auto service = nexigen::com::ServiceName::create(service_.c_str());
    if (!service) {
        stop();
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    auto subscriber =
        nexigen::com::Subscriber<nexigen::logging::LogFrame, nexigen::com::TransportHeader>::create(
            *node_, *service, nexigen::logging::log_service_options());
    if (!subscriber) {
        last_error_.store(static_cast<int>(subscriber.error()));
        stop();
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    subscriber_ = std::move(*subscriber);

    const std::string event_service = service_ + "Event";
    const auto event_name = nexigen::com::ServiceName::create(event_service.c_str());
    if (!event_name) {
        stop();
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    auto listener = nexigen::com::EventListener::create(*node_, *event_name);
    if (!listener) {
        last_error_.store(static_cast<int>(listener.error()));
        stop();
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    listener_ = std::move(*listener);

    auto waitset = nexigen::com::WaitSet::create_without_signal_handling();
    if (!waitset) {
        last_error_.store(static_cast<int>(waitset.error()));
        stop();
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    waitset_ = std::move(*waitset);
    auto event_guard = waitset_->attach_notification(*listener_);
    if (!event_guard) {
        last_error_.store(static_cast<int>(event_guard.error()));
        stop();
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    event_guard_.emplace(std::move(*event_guard));
    auto interval_guard = waitset_->attach_interval(std::chrono::milliseconds(10));
    if (!interval_guard) {
        last_error_.store(static_cast<int>(interval_guard.error()));
        stop();
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    interval_guard_.emplace(std::move(*interval_guard));

    worker_ = std::jthread([this](std::stop_token token) { run(token); });
    return {};
}

void CaptureThread::stop() noexcept {
    stopping_.store(true, std::memory_order_release);
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    event_guard_.reset();
    interval_guard_.reset();
    waitset_.reset();
    listener_.reset();
    subscriber_.reset();
    node_.reset();
}

void CaptureThread::run(std::stop_token token) noexcept {
    nexigen::core::ThreadSpec effective = spec_;
    if (effective.name().empty()) {
        static_cast<void>(effective.set_name("lgview-rx"));
    }
    nexigen::core::ThreadApplyReport report{};
    const auto applied = nexigen::core::apply_thread_spec_self(effective, report);
    if (!applied) {
        last_error_.store(static_cast<int>(applied.error()));
    }
    static_cast<void>(::setpriority(PRIO_PROCESS, 0, 10));

    while (!token.stop_requested() && !stopping_.load(std::memory_order_acquire) &&
           waitset_.has_value()) {
        const auto status =
            waitset_->wait_and_process_once([this](nexigen::com::WaitSetAttachmentId) {
                if (listener_.has_value()) {
                    static_cast<void>(listener_->try_wait([](nexigen::com::EventId) noexcept {}));
                }
                drain();
                return nexigen::com::CallbackProgression::Continue;
            });
        if (!status) {
            last_error_.store(static_cast<int>(status.error()));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    drain();
}

void CaptureThread::drain() noexcept {
    if (!subscriber_.has_value()) {
        return;
    }
    if (fail_next_receive_for_tests_.exchange(false, std::memory_order_acq_rel)) {
        last_error_.store(1);
        return;
    }
    for (;;) {
        auto got = subscriber_->receive();
        if (!got) {
            last_error_.store(static_cast<int>(got.error()));
            break;
        }
        if (!got->has_value()) {
            break;
        }
        auto& sample = **got;
        const nexigen::logging::LogFrame& frame = sample.payload();
        observe_publisher(sample.user_header().publisher_id);
        if (frame.version != nexigen::logging::LogFrame::format_version) {
            rejected_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        IngressRecord record{};
        record.frame = frame;
        record.lost_before = sample.lost_before();
        record.kind = RecordKind::Frame;
        if (ingress_.try_push(record)) {
            received_.fetch_add(1, std::memory_order_relaxed);
        } else {
            overflow_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void CaptureThread::observe_publisher(std::uint64_t publisher_id) noexcept {
    const std::uint32_t count = publisher_count_.load(std::memory_order_acquire);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (publisher_ids_[i].load(std::memory_order_relaxed) == publisher_id) {
            return;
        }
    }
    if (count < publisher_ids_.size()) {
        publisher_ids_[count].store(publisher_id, std::memory_order_relaxed);
        publisher_count_.store(count + 1U, std::memory_order_release);
    }
}

CaptureStats CaptureThread::stats() const noexcept {
    CaptureStats out{};
    out.received = received_.load(std::memory_order_relaxed);
    out.rejected = rejected_.load(std::memory_order_relaxed);
    out.overflow = overflow_.load(std::memory_order_relaxed);
    out.publishers = publisher_count_.load(std::memory_order_acquire);
    out.ingress_size = ingress_.size();
    out.last_error = last_error_.load(std::memory_order_relaxed);
    return out;
}

} // namespace nexigen::logview
