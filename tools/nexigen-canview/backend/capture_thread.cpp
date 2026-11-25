#include "capture_thread.hpp"

#include <chrono>
#include <utility>

#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>

#include <nexigen/com/service_name.hpp>
#include <nexigen/hw/can/can_service_names.hpp>
#include <nexigen/hw/can/can_channel_name.hpp>

#include "model/record_meta.hpp"

namespace nexigen::canview {

namespace {

namespace hwcan = nexigen::hw::can;

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

GapReason map_loss_reason(nexigen::com::LossReason reason) noexcept {
    switch (reason) {
        case nexigen::com::LossReason::TransportGap:            return GapReason::TransportGap;
        case nexigen::com::LossReason::ProducerRestart:         return GapReason::TransportRestart;
        case nexigen::com::LossReason::Duplicate:               return GapReason::TransportDuplicate;
        case nexigen::com::LossReason::UnexpectedSequenceReset: return GapReason::SequenceReset;
    }
    return GapReason::TransportGap;
}

bool exact_loss_count(nexigen::com::LossReason reason) noexcept {
    return reason == nexigen::com::LossReason::TransportGap;
}

} // namespace

CaptureThread::CaptureThread(std::vector<CanViewChannel> channels, nexigen::core::ThreadSpec spec)
    : profiles_(std::move(channels)), spec_(std::move(spec)) {}

CaptureThread::~CaptureThread() {
    stop();
}

nexigen::core::Status CaptureThread::validate_profiles() {
    if (profiles_.empty()) {
        last_failure_ = "no channels configured";
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    if (profiles_.size() > max_live_channels) {
        last_failure_ = std::format("live limit is {} channels, {} were enabled", max_live_channels,
                                    profiles_.size());
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    for (std::size_t index = 0U; index < profiles_.size(); ++index) {
        const CanViewChannel& channel = profiles_[index];
        if (!hwcan::is_valid_channel_name(channel.name)) {
            last_failure_ = std::format("invalid channel name: '{}'", channel.name);
            return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
        }
        for (std::size_t other = 0U; other < index; ++other) {
            if (profiles_[other].name == channel.name) {
                last_failure_ = std::format("duplicate channel name: '{}'", channel.name);
                return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
            }
            if (profiles_[other].channel_id == channel.channel_id) {
                last_failure_ = std::format("duplicate channel id: {}", channel.channel_id);
                return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
            }
        }
    }
    return {};
}

bool CaptureThread::start_channel(std::size_t index) noexcept {
    if (!node_.has_value() || !set_.has_value()) {
        return false;
    }
    ChannelState& state = channels_[index];
    state = ChannelState{};
    state.profile = profiles_[index];
    state.anchor = AnchorEstimator(state.profile.channel_id);
    if (!fail_channel_for_tests_.empty() && state.profile.name == fail_channel_for_tests_) {
        last_failure_ = std::format("channel '{}': injected start failure", state.profile.name);
        return false;
    }

    auto rx_service = hwcan::rx_service(state.profile.name);
    if (!rx_service) {
        last_failure_ = std::format("channel '{}': rx service name is invalid", state.profile.name);
        return false;
    }
    auto rx_name = nexigen::com::ServiceName::create(rx_service->c_str());
    if (!rx_name) {
        last_failure_ =
            std::format("channel '{}': rx service name is rejected", state.profile.name);
        return false;
    }
    const nexigen::com::ChannelConfig config{};
    auto message_consumer = MessageConsumer::create(
        *node_, std::move(*rx_name), config,
        [this, index](const nexigen::com::EventHeader& header,
                      const nexigen::hw::can::CanMessage& message) {
            on_message(index, header, message);
        },
        [this, index](const nexigen::com::LossInfo& info) { on_loss(index, info); });
    if (!message_consumer) {
        last_failure_ =
            std::format("channel '{}': rx consumer creation failed", state.profile.name);
        return false;
    }
    message_consumers_[index].emplace(std::move(*message_consumer));
    if (auto attached = set_->attach(*message_consumers_[index]); !attached) {
        last_failure_ = std::format("channel '{}': rx consumer attach failed", state.profile.name);
        message_consumers_[index].reset();
        return false;
    }

    auto status_service = hwcan::status_service(state.profile.name);
    if (!status_service) {
        static_cast<void>(set_->detach(*message_consumers_[index]));
        message_consumers_[index].reset();
        last_failure_ =
            std::format("channel '{}': status service name is invalid", state.profile.name);
        return false;
    }
    auto status_name = nexigen::com::ServiceName::create(status_service->c_str());
    if (!status_name) {
        static_cast<void>(set_->detach(*message_consumers_[index]));
        message_consumers_[index].reset();
        last_failure_ =
            std::format("channel '{}': status service name is rejected", state.profile.name);
        return false;
    }
    auto status_consumer = StatusConsumer::create(
        *node_, std::move(*status_name), config,
        [this, index](const nexigen::com::EventHeader& header,
                      const nexigen::hw::can::CanChannelStatus& status) {
            on_status(index, header, status);
        },
        [this, index](const nexigen::com::LossInfo& info) { on_loss(index, info); });
    if (!status_consumer) {
        static_cast<void>(set_->detach(*message_consumers_[index]));
        message_consumers_[index].reset();
        last_failure_ =
            std::format("channel '{}': status consumer creation failed", state.profile.name);
        return false;
    }
    status_consumers_[index].emplace(std::move(*status_consumer));
    if (auto attached = set_->attach(*status_consumers_[index]); !attached) {
        static_cast<void>(set_->detach(*message_consumers_[index]));
        message_consumers_[index].reset();
        status_consumers_[index].reset();
        last_failure_ =
            std::format("channel '{}': status consumer attach failed", state.profile.name);
        return false;
    }
    return true;
}

nexigen::core::Status CaptureThread::start() noexcept {
    if (worker_.joinable()) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    stopping_.store(false, std::memory_order_release);
    if (!validate_profiles()) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    if (!affinity_is_allowed(spec_.cpus())) {
        last_failure_ = "requested CPU affinity is not available";
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    const auto validated = spec_.validate();
    if (!validated) {
        last_failure_ = "thread specification is invalid";
        return std::unexpected(validated.error());
    }
    auto node = nexigen::com::Node::create();
    if (!node) {
        last_error_.store(1);
        last_failure_ = "transport node could not be created";
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    node_ = std::move(*node);
    set_.emplace(*node_);
    if (!*set_) {
        last_error_.store(static_cast<int>(set_->status().error()));
        last_failure_ = "event consumer set could not be created";
        stop();
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }

    active_channels_ = 0U;
    failed_channels_ = 0U;
    for (std::size_t index = 0U; index < profiles_.size(); ++index) {
        if (start_channel(index)) {
            ++active_channels_;
        } else {
            ++failed_channels_;
        }
    }
    if (active_channels_ == 0U) {
        if (last_failure_.empty()) {
            last_failure_ = "no channel could start";
        }
        stop();
        return std::unexpected(nexigen::core::ErrorCode::NotInitialized);
    }
    worker_ = std::jthread([this](std::stop_token token) { run(token); });
    return {};
}

void CaptureThread::stop() noexcept {
    stopping_.store(true, std::memory_order_release);
    if (set_.has_value()) {
        set_->wake_up();
    }
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    set_.reset();
    for (auto& consumer : message_consumers_) {
        consumer.reset();
    }
    for (auto& consumer : status_consumers_) {
        consumer.reset();
    }
    node_.reset();
    active_channels_ = 0U;
}

void CaptureThread::run(std::stop_token token) noexcept {
    nexigen::core::ThreadSpec effective = spec_;
    if (effective.name().empty()) {
        static_cast<void>(effective.set_name("canview-rx"));
    }
    nexigen::core::ThreadApplyReport report{};
    const auto applied = nexigen::core::apply_thread_spec_self(effective, report);
    if (!applied) {
        last_error_.store(static_cast<int>(applied.error()));
    }
    static_cast<void>(::setpriority(PRIO_PROCESS, 0, 10));

    while (!token.stop_requested() && !stopping_.load(std::memory_order_acquire) &&
           set_.has_value()) {
        const auto drained = set_->drain_all();
        if (!drained) {
            last_error_.store(static_cast<int>(drained.error()));
        }
        const auto wait_started = std::chrono::steady_clock::now();
        const auto outcome = set_->wait_for(std::chrono::milliseconds(50));
        const auto waited = std::chrono::steady_clock::now() - wait_started;
        if (!outcome) {
            last_error_.store(static_cast<int>(outcome.error()));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else if (waited < std::chrono::milliseconds(5)) {
            // Some transports return from wait_for immediately when data is
            // available. Batch those wake-ups so the receiver does not spin a
            // full CPU core while the ingress queue easily absorbs the burst.
            std::this_thread::sleep_for(std::chrono::milliseconds(5) - waited);
        }
    }
    if (set_.has_value()) {
        static_cast<void>(set_->drain_all());
    }
}

void CaptureThread::on_message(std::size_t index, const nexigen::com::EventHeader& header,
                               const nexigen::hw::can::CanMessage& message) noexcept {
    ChannelState& state = channels_[index];
    if (message.channel_id != state.profile.channel_id) {
        mismatched_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const SequenceObservation observation = state.sequence.observe(message);
    if (message.frame_flags.has(hwcan::CanFrameFlags::has_hardware_sequence)) {
        state.frames_have_sequence = true;
    }

    // Structural validation is deliberately not on the hot path: the UI
    // applies `validate_for_view()` while writing history and marks
    // `InvalidStruct`/`TimestampInvalid` there.
    auto record_flags = RecordFlags::None;
    if (observation.outcome == SequenceOutcome::Gap) {
        hardware_gaps_.fetch_add(1, std::memory_order_relaxed);
    }
    if (observation.outcome == SequenceOutcome::Duplicate ||
        observation.outcome == SequenceOutcome::Reorder) {
        record_flags = record_flags | RecordFlags::DuplicateReorder;
        duplicates_.fetch_add(1, std::memory_order_relaxed);
    }
    if (observation.outcome == SequenceOutcome::Reset ||
        observation.outcome == SequenceOutcome::SequenceReset) {
        record_flags = record_flags | RecordFlags::SequenceReset;
        resets_.fetch_add(1, std::memory_order_relaxed);
    }

    if (const auto anchor = state.anchor.observe(header, message); anchor.has_value()) {
        if (!anchors_->try_push(*anchor)) {
            overflow_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    CanIngressRecord record{};
    record.msg = message;
    record.kind = message.kind == hwcan::CanMessageKind::BusIdle ? CanRecordKind::BusIdleHeartbeat
                                                                 : CanRecordKind::Frame;
    record.lost_before = observation.outcome == SequenceOutcome::Gap ? observation.lost_before : 0U;
    record.flags = static_cast<std::uint8_t>(record_flags);
    record.time_epoch = state.anchor.epoch();
    record.anchor_source = state.anchor.source();

    if (record.kind == CanRecordKind::BusIdleHeartbeat) {
        bus_idle_.fetch_add(1, std::memory_order_relaxed);
    } else {
        received_.fetch_add(1, std::memory_order_relaxed);
    }
    static_cast<void>(push_record(record));
}

void CaptureThread::on_status(std::size_t index, const nexigen::com::EventHeader& /*header*/,
                              const nexigen::hw::can::CanChannelStatus& status) noexcept {
    ChannelState& state = channels_[index];
    StatusEvent event{};
    event.channel_id = state.profile.channel_id;
    event.status = status;
    if (!statuses_->try_push(event)) {
        overflow_.fetch_add(1, std::memory_order_relaxed);
    }

    if (state.status_seen) {
        if (status.dropped_count > state.last_dropped_count) {
            push_marker(index, CanRecordKind::DriverDrop, GapReason::DriverDropped,
                        status.dropped_count - state.last_dropped_count, true, status.timestamp_ns);
        }
        if (!state.frames_have_sequence && status.sequence_gap_count > state.last_sequence_gap) {
            push_marker(index, CanRecordKind::TransportGap, GapReason::DriverReportedSequenceGap,
                        status.sequence_gap_count - state.last_sequence_gap, false,
                        status.timestamp_ns);
        }
    }
    state.status_seen = true;
    state.last_dropped_count = status.dropped_count;
    state.last_sequence_gap = status.sequence_gap_count;

    if (status.state == hwcan::CanChannelState::BusOff ||
        status.state == hwcan::CanChannelState::Stopped ||
        status.last_error_flags.has(hwcan::CanErrorFlags::controller_restart)) {
        state.sequence.note_controller_reset();
    }
}

void CaptureThread::on_loss(std::size_t index, const nexigen::com::LossInfo& info) noexcept {
    transport_losses_.fetch_add(1, std::memory_order_relaxed);
    push_marker(index, CanRecordKind::TransportGap, map_loss_reason(info.reason), info.lost_count,
                exact_loss_count(info.reason), info.actual_timestamp_ns);
}

void CaptureThread::push_marker(std::size_t index, CanRecordKind kind, GapReason reason,
                                std::uint64_t count, bool count_exact,
                                std::uint64_t timestamp_ns) noexcept {
    const ChannelState& state = channels_[index];
    CanIngressRecord record{};
    record.msg.channel_id = state.profile.channel_id;
    record.msg.can_id = 0U;
    record.msg.kind = hwcan::CanMessageKind::ClassicData;
    record.msg.dlc = 0U;
    record.msg.hardware_timestamp_ns = timestamp_ns;
    if (timestamp_ns != 0U) {
        record.msg.frame_flags.bits |= hwcan::CanFrameFlags::has_hardware_timestamp;
    }
    record.kind = kind;
    record.lost_before = count;
    record.flags = CanIngressRecord::pack_gap_flags(reason, count_exact);
    record.time_epoch = state.anchor.epoch();
    record.anchor_source = state.anchor.source();
    if (kind == CanRecordKind::DriverDrop) {
        driver_drops_.fetch_add(1, std::memory_order_relaxed);
    }
    if (push_record(record)) {
        synthetic_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool CaptureThread::push_record(const CanIngressRecord& record) noexcept {
    if (ingress_->try_push(record)) {
        return true;
    }
    overflow_.fetch_add(1, std::memory_order_relaxed);
    ingress_losses_.fetch_add(1, std::memory_order_relaxed);
    pending_overflow_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

CaptureStats CaptureThread::stats() const noexcept {
    CaptureStats out{};
    out.received = received_.load(std::memory_order_relaxed);
    out.bus_idle = bus_idle_.load(std::memory_order_relaxed);
    out.synthetic = synthetic_.load(std::memory_order_relaxed);
    out.overflow = overflow_.load(std::memory_order_relaxed);
    out.mismatched = mismatched_.load(std::memory_order_relaxed);
    out.duplicates = duplicates_.load(std::memory_order_relaxed);
    out.resets = resets_.load(std::memory_order_relaxed);
    out.hardware_gaps = hardware_gaps_.load(std::memory_order_relaxed);
    out.transport_losses = transport_losses_.load(std::memory_order_relaxed);
    out.ingress_losses = ingress_losses_.load(std::memory_order_relaxed);
    out.driver_drops = driver_drops_.load(std::memory_order_relaxed);
    out.active_channels = active_channels_;
    out.failed_channels = failed_channels_;
    out.ingress_size = ingress_->size();
    out.last_error = last_error_.load(std::memory_order_relaxed);
    return out;
}

} // namespace nexigen::canview
