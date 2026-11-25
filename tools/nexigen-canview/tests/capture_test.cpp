// Anchor estimation and live capture tests.
#include <chrono>
#include <cstdint>
#include <malloc.h>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nexigen/com/channel_config.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/com/event_producer.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_event_code.hpp>
#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/hw/can/can_service_names.hpp>
#include <nexigen/hw/can/com/can_transport.hpp>

#include "anchor_estimator.hpp"
#include "capture_thread.hpp"
#include "suite.hpp"
#include "support/check.hpp"

namespace {

namespace canview = nexigen::canview;
namespace com = nexigen::com;
namespace hwcan = nexigen::hw::can;

struct FakeClock {
    std::int64_t base_ns{0};
    long double drift{0.0L};
    bool available{true};
};

std::optional<std::int64_t> fake_lookup(std::int64_t canonical_ns, void* context) noexcept {
    auto* clock = static_cast<FakeClock*>(context);
    if (clock == nullptr || !clock->available) {
        return std::nullopt;
    }
    const auto drift_ns =
        static_cast<std::int64_t>(clock->drift * static_cast<long double>(canonical_ns));
    return clock->base_ns + canonical_ns + drift_ns;
}

hwcan::CanMessage make_frame(std::uint16_t channel_id, std::uint64_t timestamp,
                             std::uint64_t sequence, std::uint32_t can_id = 0x100U) {
    hwcan::CanMessage message{};
    message.channel_id = channel_id;
    message.can_id = can_id;
    message.dlc = 8U;
    message.hardware_timestamp_ns = timestamp;
    message.hardware_sequence = sequence;
    message.frame_flags.bits =
        hwcan::CanFrameFlags::has_hardware_timestamp | hwcan::CanFrameFlags::has_hardware_sequence;
    message.data[0] = static_cast<std::uint8_t>(sequence & 0xFFU);
    return message;
}

canview::CanViewChannel make_channel(std::string name, std::uint16_t id) {
    canview::CanViewChannel channel{};
    channel.name = std::move(name);
    channel.channel_id = id;
    channel.nominal_bitrate = 500'000U;
    return channel;
}

void test_anchor_estimator() {
    FakeClock clock{1'000'000'000, 0.0L, true};
    canview::AnchorEstimator estimator{1U, &fake_lookup, &clock};
    com::EventHeader header{};
    hwcan::CanMessage message = make_frame(1U, 5'000U, 1U);

    header.ingest_timestamp_ns = 6'000U;
    auto anchor = estimator.observe(header, message);
    NX_CHECK(anchor.has_value());
    if (anchor.has_value()) {
        NX_CHECK(anchor->channel_id == 1U);
        NX_CHECK(anchor->epoch == 0U);
        NX_CHECK(anchor->utc_offset_ns == 1'000'001'000);
        NX_CHECK(anchor->valid_from_hw_ns == 5'000U);
    }
    NX_CHECK(estimator.has_anchor());

    header.ingest_timestamp_ns = 7'000U;
    message.hardware_timestamp_ns = 6'000U;
    NX_CHECK(!estimator.observe(header, message).has_value());

    header.ingest_timestamp_ns = 1'000'006'000U;
    message.hardware_timestamp_ns = 1'000'005'000U;
    anchor = estimator.observe(header, message);
    NX_CHECK(anchor.has_value());
    if (anchor.has_value()) {
        NX_CHECK(anchor->epoch == 0U);
        NX_CHECK(anchor->drift_ppb == 0);
    }

    clock.base_ns += 200'000'000;
    header.ingest_timestamp_ns = 1'100'001'000U;
    message.hardware_timestamp_ns = 1'100'000'000U;
    anchor = estimator.observe(header, message);
    NX_CHECK(anchor.has_value());
    if (anchor.has_value()) {
        NX_CHECK(anchor->epoch == 1U);
    }

    message.timestamp_source = hwcan::CanTimestampSource::DriverFallback;
    header.ingest_timestamp_ns = 1'100'002'000U;
    anchor = estimator.observe(header, message);
    NX_CHECK(anchor.has_value());
    if (anchor.has_value()) {
        NX_CHECK(anchor->epoch == 2U);
        NX_CHECK(anchor->source ==
                 static_cast<std::uint8_t>(hwcan::CanTimestampSource::DriverFallback));
    }

    clock.available = false;
    header.ingest_timestamp_ns = 1'100'003'000U;
    NX_CHECK(!estimator.observe(header, message).has_value());
}

void test_anchor_drift() {
    FakeClock clock{0, 1.0e-3L, true};
    canview::AnchorEstimator estimator{2U, &fake_lookup, &clock};
    com::EventHeader header{};
    header.ingest_timestamp_ns = 1'000'000'000U;
    hwcan::CanMessage message = make_frame(2U, 1'000'000'000U, 1U);
    NX_CHECK(estimator.observe(header, message).has_value());

    // Advance in 100 ms steps so the minimum-latency window rolls over and the
    // rising offset becomes visible as drift.
    std::optional<canview::AnchorEvent> last_anchor;
    for (std::uint64_t step = 2U; step <= 20U; ++step) {
        const std::uint64_t timestamp = 1'000'000'000U + step * 100'000'000U;
        header.ingest_timestamp_ns = timestamp;
        message.hardware_timestamp_ns = timestamp;
        if (auto anchor = estimator.observe(header, message); anchor.has_value()) {
            last_anchor = anchor;
        }
    }
    NX_CHECK(last_anchor.has_value());
    if (last_anchor.has_value()) {
        NX_CHECK(last_anchor->drift_ppb > 900'000);
        NX_CHECK(last_anchor->drift_ppb < 1'100'000);
    }
}

void test_profile_validation() {
    std::vector<canview::CanViewChannel> channels;
    for (std::uint16_t index = 0U; index < 9U; ++index) {
        channels.push_back(make_channel("ch" + std::to_string(index), index));
    }
    canview::CaptureThread too_many{channels};
    const auto rejected = too_many.start();
    NX_CHECK(!rejected);
    NX_CHECK(too_many.last_failure().find("live limit") != std::string::npos);
    NX_CHECK(!too_many.running());

    std::vector<canview::CanViewChannel> duplicated{make_channel("dup", 1U),
                                                    make_channel("dup", 2U)};
    canview::CaptureThread duplicate_name{duplicated};
    NX_CHECK(!duplicate_name.start());
    NX_CHECK(duplicate_name.last_failure().find("duplicate channel name") != std::string::npos);

    std::vector<canview::CanViewChannel> duplicated_id{make_channel("one", 1U),
                                                       make_channel("two", 1U)};
    canview::CaptureThread duplicate_id_thread{duplicated_id};
    NX_CHECK(!duplicate_id_thread.start());
    NX_CHECK(duplicate_id_thread.last_failure().find("duplicate channel id") != std::string::npos);

    std::vector<canview::CanViewChannel> invalid{make_channel("Not Valid", 1U)};
    canview::CaptureThread invalid_name{invalid};
    NX_CHECK(!invalid_name.start());
    NX_CHECK(invalid_name.last_failure().find("invalid channel name") != std::string::npos);
}

bool start_or_skip(canview::CaptureThread& capture) {
    const auto started = capture.start();
    if (!started) {
        if (capture.last_failure().find("transport node") != std::string::npos ||
            capture.last_failure().find("consumer set") != std::string::npos) {
            return false;
        }
    }
    NX_CHECK(started.has_value());
    return started.has_value();
}

void test_failure_isolation() {
    std::vector<canview::CanViewChannel> channels{make_channel("canview-iso-a", 11U),
                                                  make_channel("canview-iso-b", 12U)};
    canview::CaptureThread capture{channels};
    capture.fail_channel_for_tests("canview-iso-b");
    if (!start_or_skip(capture)) {
        return;
    }
    const auto stats = capture.stats();
    NX_CHECK(stats.active_channels == 1U);
    NX_CHECK(stats.failed_channels == 1U);
    NX_CHECK(capture.running());
    capture.stop();
    NX_CHECK(!capture.running());

    capture.clear_channel_failure_for_tests();
    if (!start_or_skip(capture)) {
        return;
    }
    NX_CHECK(capture.stats().active_channels == 2U);
    NX_CHECK(capture.stats().failed_channels == 0U);
    capture.stop();
}

void test_invalid_affinity() {
    std::vector<canview::CanViewChannel> channels{make_channel("canview-aff", 41U)};
    nexigen::core::CpuSet cpus{};
    NX_CHECK(cpus.add(1023).has_value());
    nexigen::core::ThreadSpec spec{};
    spec.set_cpus(cpus);
    canview::CaptureThread capture{channels, spec};
    const auto started = capture.start();
    NX_CHECK(!started);
    NX_CHECK(capture.last_failure().find("affinity") != std::string::npos);
    NX_CHECK(!capture.running());
}

void test_all_channels_fail() {
    std::vector<canview::CanViewChannel> channels{make_channel("canview-fail-a", 21U)};
    canview::CaptureThread capture{channels};
    capture.fail_channel_for_tests("canview-fail-a");
    const auto started = capture.start();
    if (!started && capture.last_failure().find("transport node") != std::string::npos) {
        return;
    }
    NX_CHECK(!started);
    NX_CHECK(!capture.running());
    NX_CHECK(capture.stats().active_channels == 0U);
    NX_CHECK(capture.stats().failed_channels == 1U);
}

template <class T>
std::optional<com::EventProducer<T>> make_producer(com::Node& node, const std::string& channel,
                                                   bool status) {
    auto name = status ? hwcan::status_service(channel) : hwcan::rx_service(channel);
    if (!name) {
        return std::nullopt;
    }
    auto service = com::ServiceName::create(name->c_str());
    if (!service) {
        return std::nullopt;
    }
    auto producer =
        com::EventProducer<T>::create(node, std::move(*service), com::ChannelConfig{}, "canviewt");
    if (!producer) {
        return std::nullopt;
    }
    return std::move(*producer);
}

struct BusCounters {
    std::uint64_t frames{0};
    std::uint64_t statuses{0};
    std::uint64_t driver_drops{0};
    std::uint64_t fallback_gaps{0};
};

void publish_bus_frames(com::EventProducer<hwcan::CanMessage>& frames_a,
                        com::EventProducer<hwcan::CanMessage>& frames_b) {
    for (std::uint64_t sequence = 1U; sequence <= 2U; ++sequence) {
        const auto frame_a = make_frame(31U, 1'000U * sequence, sequence);
        static_cast<void>(frames_a.publish(frame_a, hwcan::event_rx, hwcan::event_time(frame_a),
                                           hwcan::hardware_metadata(frame_a)));
        const auto frame_b = make_frame(32U, 2'000U * sequence, sequence, 0x200U);
        static_cast<void>(frames_b.publish(frame_b, hwcan::event_rx, hwcan::event_time(frame_b),
                                           hwcan::hardware_metadata(frame_b)));
    }
    const auto mismatched = make_frame(32U, 9'000U, 1U);
    static_cast<void>(frames_a.publish(mismatched, hwcan::event_rx, hwcan::event_time(mismatched),
                                       hwcan::hardware_metadata(mismatched)));
}

void publish_bus_statuses(com::EventProducer<hwcan::CanChannelStatus>& status_a,
                          com::EventProducer<hwcan::CanChannelStatus>& status_b,
                          com::EventProducer<hwcan::CanChannelStatus>& status_c) {
    hwcan::CanChannelStatus status{};
    status.state = hwcan::CanChannelState::Active;
    status.rx_count = 2U;
    static_cast<void>(status_a.publish_now(status, hwcan::event_status));
    static_cast<void>(status_b.publish_now(status, hwcan::event_status));
    static_cast<void>(status_c.publish_now(status, hwcan::event_status));

    hwcan::CanChannelStatus dropped = status;
    dropped.dropped_count = 5U;
    static_cast<void>(status_a.publish_now(dropped, hwcan::event_status));

    hwcan::CanChannelStatus with_gap = status;
    with_gap.sequence_gap_count = 7U;
    static_cast<void>(status_b.publish_now(with_gap, hwcan::event_status));
    with_gap.sequence_gap_count = 4U;
    static_cast<void>(status_c.publish_now(with_gap, hwcan::event_status));
}

void drain_bus(canview::CaptureThread& capture, BusCounters& counters) {
    canview::CanIngressRecord record{};
    while (capture.ingress().try_pop(record)) {
        if (record.kind == canview::CanRecordKind::Frame) {
            ++counters.frames;
        }
        if (record.kind == canview::CanRecordKind::DriverDrop) {
            ++counters.driver_drops;
            NX_CHECK(record.lost_before == 5U);
            NX_CHECK(record.gap_reason() == canview::GapReason::DriverDropped);
            NX_CHECK(record.gap_count_exact());
        }
        if (record.kind == canview::CanRecordKind::TransportGap &&
            record.gap_reason() == canview::GapReason::DriverReportedSequenceGap) {
            ++counters.fallback_gaps;
            NX_CHECK(record.lost_before == 4U);
            NX_CHECK(!record.gap_count_exact());
        }
    }
    canview::StatusEvent event{};
    while (capture.statuses().try_pop(event)) {
        ++counters.statuses;
    }
}

bool wait_for_bus(canview::CaptureThread& capture, BusCounters& counters) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline &&
           (counters.frames < 4U || counters.statuses < 6U || counters.driver_drops < 1U ||
            counters.fallback_gaps < 1U)) {
        drain_bus(capture, counters);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return counters.frames == 4U && counters.statuses == 6U && counters.driver_drops == 1U &&
           counters.fallback_gaps == 1U;
}

void test_virtual_bus_capture() {
    const std::string channel_a{"canview-bus-a"};
    const std::string channel_b{"canview-bus-b"};
    const std::string channel_c{"canview-bus-c"};
    std::vector<canview::CanViewChannel> channels{
        make_channel(channel_a, 31U), make_channel(channel_b, 32U), make_channel(channel_c, 33U)};
    canview::CaptureThread capture{channels};
    if (!start_or_skip(capture)) {
        return;
    }

    auto node = com::Node::create();
    if (!node) {
        capture.stop();
        return;
    }
    auto frames_a = make_producer<hwcan::CanMessage>(*node, channel_a, false);
    auto frames_b = make_producer<hwcan::CanMessage>(*node, channel_b, false);
    auto status_a = make_producer<hwcan::CanChannelStatus>(*node, channel_a, true);
    auto status_b = make_producer<hwcan::CanChannelStatus>(*node, channel_b, true);
    auto status_c = make_producer<hwcan::CanChannelStatus>(*node, channel_c, true);
    if (!frames_a || !frames_b || !status_a || !status_b || !status_c) {
        capture.stop();
        return;
    }

    publish_bus_frames(*frames_a, *frames_b);
    publish_bus_statuses(*status_a, *status_b, *status_c);

    BusCounters counters;
    NX_CHECK(wait_for_bus(capture, counters));
    const auto stats = capture.stats();
    NX_CHECK(counters.frames == 4U);
    NX_CHECK(counters.statuses == 6U);
    NX_CHECK(counters.driver_drops == 1U);
    NX_CHECK(counters.fallback_gaps == 1U);
    NX_CHECK(stats.received >= 4U);
    NX_CHECK(stats.mismatched == 1U);
    NX_CHECK(capture.ingress().is_empty());
    capture.stop();
    NX_CHECK(!capture.running());
}

void test_loss_mapping() {
    canview::CaptureThread capture{{make_channel("canview-loss", 0U)}};
    const struct {
        com::LossReason reason;
        canview::GapReason expected;
        bool exact;
    } cases[] = {
        {com::LossReason::TransportGap, canview::GapReason::TransportGap, true},
        {com::LossReason::ProducerRestart, canview::GapReason::TransportRestart, false},
        {com::LossReason::Duplicate, canview::GapReason::TransportDuplicate, false},
        {com::LossReason::UnexpectedSequenceReset, canview::GapReason::SequenceReset, false},
    };
    std::uint64_t lost = 1U;
    for (const auto& item : cases) {
        com::LossInfo info{};
        info.reason = item.reason;
        info.lost_count = lost;
        info.actual_timestamp_ns = 1'000U + lost;
        capture.loss_for_tests(info);
        canview::CanIngressRecord record{};
        NX_CHECK(capture.ingress().try_pop(record).has_value());
        NX_CHECK(record.kind == canview::CanRecordKind::TransportGap);
        NX_CHECK(record.gap_reason() == item.expected);
        NX_CHECK(record.gap_count_exact() == item.exact);
        NX_CHECK(record.lost_before == lost);
        NX_CHECK(record.msg.hardware_timestamp_ns == 1'000U + lost);
        NX_CHECK(record.msg.channel_id == 0U);
        ++lost;
    }
    NX_CHECK(capture.stats().synthetic == 4U);
}

void test_hot_path_allocation_guard() {
    canview::CaptureThread capture{{make_channel("canview-hot", 0U)}};
    com::EventHeader header{};
    header.ingest_timestamp_ns = 1U;
    hwcan::CanMessage message = make_frame(0U, 1'000U, 1U);
    capture.ingest_for_tests(header, message);

    const std::size_t before = mallinfo2().uordblks;
    for (std::uint64_t sequence = 2U; sequence <= 1'001U; ++sequence) {
        message.hardware_sequence = sequence;
        message.hardware_timestamp_ns = 1'000U + sequence;
        capture.ingest_for_tests(header, message);
    }
    const std::size_t after = mallinfo2().uordblks;
    NX_CHECK(after <= before);
    NX_CHECK(capture.stats().received == 1'001U);
}

} // namespace

namespace canview_test {

void run_capture_tests() {
    test_anchor_estimator();
    test_anchor_drift();
    test_profile_validation();
    test_failure_isolation();
    test_all_channels_fail();
    test_invalid_affinity();
    test_virtual_bus_capture();
    test_loss_mapping();
    test_hot_path_allocation_guard();
}

} // namespace canview_test
