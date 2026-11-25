// Heatmap and bit-activity tests.
#include <array>
#include <cstdint>
#include <string>

#include <nexigen/hw/can/can_message.hpp>

#include "bit_activity.hpp"
#include "bit_timing.hpp"
#include "can_record_ring.hpp"
#include "heatmap.hpp"
#include "record_meta.hpp"
#include "record_source.hpp"
#include "suite.hpp"
#include "support/check.hpp"
#include "time_presenter.hpp"

namespace {

namespace canview = nexigen::canview;
namespace hwcan = nexigen::hw::can;

std::uint64_t push_frame(canview::CanRecordRing& ring, std::uint16_t channel, std::uint64_t ts,
                         std::uint32_t can_id, std::uint8_t dlc = 8U,
                         hwcan::CanMessageKind kind = hwcan::CanMessageKind::ClassicData,
                         bool brs = false) {
    hwcan::CanMessage message{};
    message.channel_id = channel;
    message.can_id = can_id;
    message.hardware_timestamp_ns = ts;
    message.dlc = dlc;
    message.kind = kind;
    message.frame_flags.bits |=
        hwcan::CanFrameFlags::has_hardware_timestamp | hwcan::CanFrameFlags::has_hardware_sequence;
    if (brs) {
        message.frame_flags.bits |= hwcan::CanFrameFlags::bit_rate_switch;
    }
    const std::uint8_t meta = canview::pack_frame_meta(canview::CanRecordKind::Frame);
    return ring.push(message, 0, canview::CanRecordView::no_node, meta, 0U, 0U);
}

std::uint64_t push_bus_idle(canview::CanRecordRing& ring, std::uint16_t channel, std::uint64_t ts,
                            std::uint64_t idle_ns) {
    hwcan::CanMessage message{};
    message.channel_id = channel;
    message.kind = hwcan::CanMessageKind::BusIdle;
    message.hardware_timestamp_ns = ts;
    message.idle_duration_ns = idle_ns;
    const std::uint8_t meta = canview::pack_frame_meta(canview::CanRecordKind::BusIdleHeartbeat);
    return ring.push(message, 0, canview::CanRecordView::no_node, meta, 0U, 0U);
}

void test_bit_timing() {
    canview::ChannelTiming classic{500'000U, 0U, false};
    auto message = hwcan::CanMessage{};
    message.dlc = 8U;
    auto timing = canview::estimate_frame_timing(message, classic);
    NX_CHECK(timing.available);
    NX_CHECK(timing.bits > 100U);
    NX_CHECK(timing.time_ns > 0U);

    canview::ChannelTiming unknown{0U, 0U, false};
    NX_CHECK(!canview::estimate_frame_timing(message, unknown).available);

    canview::ChannelTiming fd{500'000U, 2'000'000U, true};
    auto fd_message = message;
    fd_message.kind = hwcan::CanMessageKind::FdData;
    fd_message.dlc = 64U;
    fd_message.frame_flags.bits |= hwcan::CanFrameFlags::bit_rate_switch;
    const auto fd_timing = canview::estimate_frame_timing(fd_message, fd);
    auto classic_64 = fd_message;
    classic_64.kind = hwcan::CanMessageKind::ClassicData;
    classic_64.dlc = 64U;
    const auto slow = canview::estimate_frame_timing(classic_64, classic);
    NX_CHECK(fd_timing.available);
    NX_CHECK(fd_timing.time_ns > 0U);
    NX_CHECK(slow.time_ns > fd_timing.time_ns);
}

void test_heatmap() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    for (std::uint64_t index = 0U; index < 10U; ++index) {
        push_frame(*ring, 1U, index * 10'000'000ULL, 0x100U + static_cast<std::uint32_t>(index));
    }
    push_bus_idle(*ring, 1U, 50'000'000ULL, 5'000'000ULL);
    push_frame(*ring, 1U, 55'000'000ULL, 0x500U, 0U, hwcan::CanMessageKind::Error);
    push_frame(*ring, 1U, 56'000'000ULL, 0x500U, 0U, hwcan::CanMessageKind::Overload);

    canview::RingRecordSource source{*ring};
    canview::TimeContext context;
    canview::AnchorEvent anchor{};
    anchor.channel_id = 1U;
    anchor.epoch = 0U;
    anchor.utc_offset_ns = 0;
    context.anchors.upsert(anchor);

    std::array<canview::ChannelTiming, 32> timings{};
    timings[1] = canview::ChannelTiming{500'000U, 2'000'000U, true};
    canview::HeatmapWindow heatmap;
    heatmap.init();
    heatmap.rebuild(source, &context, canview::HeatmapAxis::SharedHardware, 0U, 100'000'000ULL, 1U,
                    timings);
    NX_CHECK(heatmap.column_count() == 10U);
    NX_CHECK(heatmap.effective_step_ns() == 10'000'000ULL);
    NX_CHECK(heatmap.axis() == canview::HeatmapAxis::SharedHardware);
    NX_CHECK(heatmap.follow());

    const auto load = heatmap.load(5U, 1U);
    NX_CHECK(load.measured_available);
    NX_CHECK(load.measured > 0.49 && load.measured < 0.51);
    NX_CHECK(load.estimated_available);
    NX_CHECK(load.estimated > 0.0);
    const auto* bucket = heatmap.column(5U);
    NX_CHECK(bucket != nullptr);
    if (bucket != nullptr) {
        NX_CHECK(bucket->channels[1].errors == 1U);
        NX_CHECK(bucket->channels[1].overloads == 1U);
        NX_CHECK(bucket->channels[1].measured);
        NX_CHECK(bucket->bins[1].frames > 0U);
        NX_CHECK(bucket->bins[5].frames > 0U);
    }
    const auto clicked = heatmap.click_record(5U);
    NX_CHECK(clicked.has_value());
    if (clicked.has_value()) {
        NX_CHECK(*clicked == 5U);
    }
    NX_CHECK(heatmap.click_record(50U) == std::nullopt);

    // Column cap and step controls.
    heatmap.step_finer();
    NX_CHECK(heatmap.requested_step_ns() < 10'000'000ULL);
    heatmap.step_coarser();
    heatmap.step_coarser();
    heatmap.rebuild(source, &context, canview::HeatmapAxis::SharedHardware, 0U, 1'000'000'000ULL,
                    1U, timings);
    NX_CHECK(heatmap.column_count() <= canview::HeatmapWindow::max_columns);
    NX_CHECK(heatmap.effective_step_ns() >= 1'000'000'000ULL / canview::HeatmapWindow::max_columns);

    // UTC requires an anchor; hardware lanes and shared mode both build.
    heatmap.rebuild(source, &context, canview::HeatmapAxis::Utc, 0U, 100'000'000ULL, 1U, timings);
    NX_CHECK(heatmap.column_count() > 0U);
    heatmap.rebuild(source, nullptr, canview::HeatmapAxis::Utc, 0U, 100'000'000ULL, 1U, timings);
    NX_CHECK(heatmap.column_count() == 0U);
    heatmap.rebuild(source, &context, canview::HeatmapAxis::HardwareLanes, 0U, 100'000'000ULL, 1U,
                    timings);
    NX_CHECK(heatmap.column_count() > 0U);

    // Missing bitrate is reported explicitly.
    std::array<canview::ChannelTiming, 32> none{};
    heatmap.rebuild(source, &context, canview::HeatmapAxis::SharedHardware, 0U, 100'000'000ULL, 1U,
                    none);
    const auto missing = heatmap.load(1U, 1U);
    NX_CHECK(missing.missing_bitrate);
    NX_CHECK(!missing.estimated_available);
}

void test_bit_activity() {
    auto ring = canview::CanRecordRing::create(8192U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    for (std::uint64_t index = 0U; index < 4U; ++index) {
        hwcan::CanMessage message{};
        message.channel_id = 1U;
        message.can_id = 0x200U;
        message.dlc = 8U;
        message.hardware_timestamp_ns = index * 10'000'000ULL;
        message.data[0] = static_cast<std::uint8_t>(index % 2U);
        const std::uint8_t meta = canview::pack_frame_meta(canview::CanRecordKind::Frame);
        static_cast<void>(ring->push(message, 0, canview::CanRecordView::no_node, meta, 0U, 0U));
    }
    canview::RingRecordSource source{*ring};
    canview::BitActivity activity;
    activity.build(source, 1U, 0x200U, false);
    NX_CHECK(activity.available());
    NX_CHECK(!activity.low_confidence());
    NX_CHECK(activity.bytes() == 8U);
    NX_CHECK(activity.frame_count() == 4U);
    NX_CHECK(activity.at(0U, 0U).transitions == 3U);
    NX_CHECK(activity.at(0U, 0U).frequency > 0.0);
    NX_CHECK(activity.at(1U, 0U).transitions == 0U);
    NX_CHECK(activity.span_ns() == 30'000'000ULL);

    canview::BitActivity sparse;
    sparse.build(source, 1U, 0x999U, false);
    NX_CHECK(!sparse.available());
    sparse.build(source, 1U, 0x200U, true);
    NX_CHECK(!sparse.available());

    // FD payloads use the 64-byte matrix; the window is capped at 4096 frames.
    for (std::uint64_t index = 4U; index < 4100U; ++index) {
        hwcan::CanMessage message{};
        message.channel_id = 1U;
        message.can_id = 0x300U;
        message.kind = hwcan::CanMessageKind::FdData;
        message.dlc = 12U;
        message.hardware_timestamp_ns = index * 1'000ULL;
        message.data[0] = static_cast<std::uint8_t>(index % 2U);
        const std::uint8_t meta = canview::pack_frame_meta(canview::CanRecordKind::Frame);
        static_cast<void>(ring->push(message, 0, canview::CanRecordView::no_node, meta, 0U, 0U));
    }
    activity.build(source, 1U, 0x300U, false);
    NX_CHECK(activity.bytes() == 64U);
    NX_CHECK(activity.frame_count() == canview::BitActivity::max_frames);

    // A gap larger than 10 s stops the backward scan.
    auto recent = canview::CanRecordRing::create(16U);
    NX_CHECK(recent.has_value());
    if (recent.has_value()) {
        for (const std::uint64_t stamp : {0ULL, 5'000'000'000ULL, 11'000'000'000ULL}) {
            hwcan::CanMessage message{};
            message.channel_id = 1U;
            message.can_id = 0x400U;
            message.dlc = 1U;
            message.hardware_timestamp_ns = stamp;
            const std::uint8_t meta = canview::pack_frame_meta(canview::CanRecordKind::Frame);
            static_cast<void>(
                recent->push(message, 0, canview::CanRecordView::no_node, meta, 0U, 0U));
        }
        canview::RingRecordSource recent_source{*recent};
        activity.build(recent_source, 1U, 0x400U, false);
        NX_CHECK(activity.frame_count() == 2U);
    }
}

} // namespace

namespace canview_test {

void run_heatmap_tests() {
    test_bit_timing();
    test_heatmap();
    test_bit_activity();
}

} // namespace canview_test
