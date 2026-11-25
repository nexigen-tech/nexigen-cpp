// Time presentation, anchor table and Overwrite table tests.
#include <array>
#include <cstdint>
#include <string>

#include <nexigen/hw/can/can_message.hpp>

#include "anchor_table.hpp"
#include "can_record_ring.hpp"
#include "can_view.hpp"
#include "overwrite_table.hpp"
#include "suite.hpp"
#include "support/check.hpp"
#include "time_presenter.hpp"

namespace {

namespace canview = nexigen::canview;
namespace hwcan = nexigen::hw::can;

struct TimedFrame {
    std::uint16_t channel{1U};
    std::uint64_t timestamp{0U};
    std::uint16_t epoch{0U};
    std::int64_t delta{0};
    std::uint32_t can_id{0x100U};
    std::uint8_t dlc{8U};
    hwcan::CanMessageKind kind{hwcan::CanMessageKind::ClassicData};
    std::array<std::uint8_t, 8> data{};
};

std::uint64_t push_timed(canview::CanRecordRing& ring, const TimedFrame& frame) {
    hwcan::CanMessage message{};
    message.channel_id = frame.channel;
    message.can_id = frame.can_id;
    message.hardware_timestamp_ns = frame.timestamp;
    message.dlc = frame.dlc;
    message.kind = frame.kind;
    message.data.fill(0U);
    for (std::size_t index = 0; index < frame.data.size() && index < frame.dlc; ++index) {
        message.data[index] = frame.data[index];
    }
    message.frame_flags.bits =
        hwcan::CanFrameFlags::has_hardware_timestamp | hwcan::CanFrameFlags::has_hardware_sequence;
    const std::uint8_t meta = canview::pack_frame_meta(canview::CanRecordKind::Frame);
    return ring.push(message, frame.delta, canview::CanRecordView::no_node, meta, frame.epoch, 0U);
}

canview::AnchorEvent make_anchor(std::uint16_t channel, std::uint16_t epoch, std::int64_t offset,
                                 std::uint64_t valid_from, std::int32_t drift = 0) {
    canview::AnchorEvent anchor{};
    anchor.channel_id = channel;
    anchor.epoch = epoch;
    anchor.source = 0U;
    anchor.utc_offset_ns = offset;
    anchor.drift_ppb = drift;
    anchor.valid_from_hw_ns = valid_from;
    return anchor;
}

void test_anchor_table_and_utc() {
    canview::AnchorTable table;
    table.upsert(make_anchor(1U, 0U, 5'000'000'000, 1'000'000'000U));
    table.upsert(make_anchor(1U, 1U, 6'000'000'000, 1'060'000'000U));

    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    TimedFrame first{};
    first.timestamp = 1'000'000'000U;
    first.epoch = 0U;
    const auto first_seq = push_timed(*ring, first);
    TimedFrame jumped{};
    jumped.timestamp = 1'060'000'000U;
    jumped.epoch = 1U;
    const auto jumped_seq = push_timed(*ring, jumped);
    TimedFrame other{};
    other.channel = 2U;
    other.timestamp = 2'000'000'000U;
    const auto other_seq = push_timed(*ring, other);

    canview::TimeContext context;
    context.anchors = table;
    const auto first_record = ring->find(first_seq);
    const auto jumped_record = ring->find(jumped_seq);
    const auto other_record = ring->find(other_seq);
    NX_CHECK(first_record.has_value());
    NX_CHECK(jumped_record.has_value());
    NX_CHECK(other_record.has_value());
    if (!first_record || !jumped_record || !other_record) {
        return;
    }

    const auto utc_first = canview::TimePresenter::utc_ns(*first_record, context);
    NX_CHECK(utc_first.has_value() && *utc_first == 6'000'000'000);
    const auto utc_jumped = canview::TimePresenter::utc_ns(*jumped_record, context);
    NX_CHECK(utc_jumped.has_value() && *utc_jumped == 7'060'000'000);
    NX_CHECK(!canview::TimePresenter::utc_ns(*other_record, context).has_value());

    const auto utc_cell = canview::TimePresenter::utc(*first_record, context);
    NX_CHECK(utc_cell.available);
    NX_CHECK(utc_cell.estimated);
    NX_CHECK(utc_cell.text[0] != '-');
    NX_CHECK(!canview::TimePresenter::utc(*other_record, context).available);

    // Drift is applied over the elapsed hardware time.
    canview::AnchorTable drift_table;
    drift_table.upsert(make_anchor(1U, 1U, 6'000'000'000, 1'000'000'000U, 1'000'000));
    context.anchors = drift_table;
    const auto drifted = canview::TimePresenter::utc_ns(*jumped_record, context);
    NX_CHECK(drifted.has_value());
    if (drifted.has_value()) {
        NX_CHECK(*drifted >= 7'059'000'000);
        NX_CHECK(*drifted <= 7'061'000'000);
    }

    // Epoch 0 is overwritten by epoch 256 and renders unavailable.
    canview::AnchorTable evicting;
    evicting.upsert(make_anchor(1U, 0U, 1, 1U));
    evicting.upsert(make_anchor(1U, 256U, 2, 2U));
    NX_CHECK(!evicting.lookup(1U, 0U).has_value());
    NX_CHECK(evicting.lookup(1U, 256U).has_value());
    evicting.clear();
    NX_CHECK(evicting.count() == 0U);
}

void test_time_modes_and_deltas() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    TimedFrame first{};
    first.timestamp = 1'000'000'000U;
    const auto first_seq = push_timed(*ring, first);
    TimedFrame later{};
    later.timestamp = 1'060'000'000U;
    later.delta = 60'000'000;
    const auto later_seq = push_timed(*ring, later);
    TimedFrame cross{};
    cross.channel = 2U;
    cross.timestamp = 2'000'000'000U;
    const auto cross_seq = push_timed(*ring, cross);

    const auto first_record = ring->find(first_seq);
    const auto later_record = ring->find(later_seq);
    const auto cross_record = ring->find(cross_seq);
    NX_CHECK(first_record && later_record && cross_record);
    if (!first_record || !later_record || !cross_record) {
        return;
    }

    canview::TimeContext context;
    context.observe(*first_record);
    context.observe(*later_record);
    context.observe(*cross_record);
    canview::CanView view{};

    const auto driver = canview::TimePresenter::hardware(
        *first_record, canview::TimestampMode::Driver, view, context);
    NX_CHECK(driver.available);
    NX_CHECK(driver.to_string() == "1000000000");

    const auto since = canview::TimePresenter::hardware(
        *later_record, canview::TimestampMode::SinceSessionStart, view, context);
    NX_CHECK(since.available);
    NX_CHECK(since.to_string() == "60000000");

    view.set_t0(*first_record);
    const auto relative = canview::TimePresenter::hardware(
        *later_record, canview::TimestampMode::RelativeT0, view, context);
    NX_CHECK(relative.available);
    NX_CHECK(relative.to_string() == "60000000");
    NX_CHECK(relative.value == 60'000'000);
    NX_CHECK(!canview::TimePresenter::hardware(*cross_record, canview::TimestampMode::RelativeT0,
                                               view, context)
                  .available);
    context.assume_shared_timebase = true;
    NX_CHECK(canview::TimePresenter::hardware(*cross_record, canview::TimestampMode::RelativeT0,
                                              view, context)
                 .available);

    context.assume_shared_timebase = false;
    const auto previous = canview::TimePresenter::delta(
        *later_record, &*first_record, canview::DeltaMode::PreviousVisible, view, context);
    NX_CHECK(previous.available);
    NX_CHECK(previous.to_string() == "60000000");
    const auto cross_delta = canview::TimePresenter::delta(
        *cross_record, &*first_record, canview::DeltaMode::PreviousVisible, view, context);
    NX_CHECK(!cross_delta.available);
    NX_CHECK(std::string(cross_delta.reason.data()).find("another channel") != std::string::npos);
    context.assume_shared_timebase = true;
    NX_CHECK(canview::TimePresenter::delta(*cross_record, &*first_record,
                                           canview::DeltaMode::PreviousVisible, view, context)
                 .available);

    const auto per_id = canview::TimePresenter::delta(*later_record, nullptr,
                                                      canview::DeltaMode::PerCanId, view, context);
    NX_CHECK(per_id.available);
    NX_CHECK(per_id.to_string() == "60000000");

    NX_CHECK(canview::TimePresenter::is_anomalous(60'000'000, 20'000'000));
    NX_CHECK(!canview::TimePresenter::is_anomalous(10'000'000, 20'000'000));

    // Signed arithmetic keeps out-of-order deltas negative.
    const auto backwards = canview::TimePresenter::delta(
        *first_record, &*later_record, canview::DeltaMode::PreviousVisible, view, context);
    NX_CHECK(backwards.available);
    NX_CHECK(backwards.negative);
}

canview::OverwriteKey key_for(std::uint16_t channel, std::uint32_t can_id) {
    canview::OverwriteKey key{};
    key.channel_id = channel;
    key.can_id = can_id;
    key.kind = hwcan::CanMessageKind::ClassicData;
    return key;
}

void test_overwrite_table() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    canview::OverwriteTable table;
    table.init(8U);

    const auto key = key_for(1U, 0x100U);
    std::uint64_t seq = 0U;
    for (std::uint64_t step = 0U; step < 3U; ++step) {
        TimedFrame frame{};
        frame.timestamp = step * 10'000'000ULL;
        frame.data = {0x01U, 0x02U, 0U, 0U, 0U, 0U, 0U, 0U};
        seq = push_timed(*ring, frame);
        const auto record = ring->find(seq);
        NX_CHECK(record.has_value());
        if (record) {
            NX_CHECK(table.update(*record, frame.timestamp, 20'000'000) != nullptr);
        }
    }
    const auto* row = table.find(key);
    NX_CHECK(row != nullptr);
    if (row != nullptr) {
        auto stats = row->stats(20'000'000);
        NX_CHECK(stats.count == 3U);
        NX_CHECK(stats.msg_per_s == 3U);
        NX_CHECK(stats.has_delta);
        NX_CHECK(stats.delta_cur == 10'000'000);
        NX_CHECK(stats.delta_min == 10'000'000);
        NX_CHECK(stats.delta_max == 10'000'000);
        NX_CHECK(stats.delta_avg == 10'000'000);
        NX_CHECK(stats.jitter == 0);
        NX_CHECK(row->last_seq == seq);
    }

    // One-second window expires old buckets.
    TimedFrame late{};
    late.timestamp = 2'000'000'000ULL;
    const auto late_seq = push_timed(*ring, late);
    if (const auto late_record = ring->find(late_seq); late_record.has_value()) {
        static_cast<void>(table.update(*late_record, late.timestamp, 20'000'000));
    }
    row = table.find(key);
    NX_CHECK(row != nullptr);
    if (row != nullptr) {
        const auto stats = row->stats(2'000'000'000ULL);
        NX_CHECK(stats.count == 4U);
        NX_CHECK(stats.msg_per_s == 1U);
    }

    // Gap resets only the affected channel and excludes the next period.
    table.reset_channel(1U);
    NX_CHECK(table.gap_pending());
    NX_CHECK(table.consume_gap());
    table.reset_all();
    NX_CHECK(table.consume_gap());
    TimedFrame after_gap{};
    after_gap.timestamp = 2'010'000'000ULL;
    const auto after_seq = push_timed(*ring, after_gap);
    if (const auto after_record = ring->find(after_seq); after_record.has_value()) {
        static_cast<void>(table.update(*after_record, after_gap.timestamp, 20'000'000));
    }
    row = table.find(key);
    NX_CHECK(row != nullptr);
    if (row != nullptr) {
        NX_CHECK(row->stats(2'010'000'000ULL).anomalous);
        NX_CHECK(!row->stats(2'010'000'000ULL).has_delta);
    }

    // Error rows keep count/rate but expose no period.
    TimedFrame error{};
    error.timestamp = 3'000'000'000ULL;
    error.can_id = 0x7FFU;
    error.kind = hwcan::CanMessageKind::Error;
    error.dlc = 0U;
    const auto error_seq = push_timed(*ring, error);
    if (const auto error_record = ring->find(error_seq); error_record.has_value()) {
        NX_CHECK(table.update(*error_record, error.timestamp, 0) != nullptr);
    }
    auto error_key = key_for(1U, 0x7FFU);
    error_key.kind = hwcan::CanMessageKind::Error;
    const auto* error_row = table.find(error_key);
    NX_CHECK(error_row != nullptr);
    if (error_row != nullptr) {
        const auto stats = error_row->stats(3'000'000'000ULL);
        NX_CHECK(stats.error_row);
        NX_CHECK(!stats.has_delta);
        NX_CHECK(stats.count == 1U);
    }

    // Bus-idle and synthetic records never create rows.
    TimedFrame idle{};
    idle.kind = hwcan::CanMessageKind::BusIdle;
    idle.dlc = 0U;
    const auto idle_seq = push_timed(*ring, idle);
    if (const auto idle_record = ring->find(idle_seq); idle_record.has_value()) {
        NX_CHECK(table.update(*idle_record, 0U, 0) == nullptr);
    }

    // LRU eviction drops a selected key and counts it.
    canview::OverwriteTable small;
    small.init(2U);
    const auto a = key_for(1U, 0x10U);
    const auto b = key_for(1U, 0x11U);
    const auto c = key_for(1U, 0x12U);
    small.select_key(a);
    std::uint64_t small_seq = 0U;
    for (const auto can_id : {0x10U, 0x11U, 0x12U}) {
        TimedFrame frame{};
        frame.can_id = can_id;
        frame.timestamp = 1'000U + can_id;
        small_seq = push_timed(*ring, frame);
        if (const auto record = ring->find(small_seq); record.has_value()) {
            static_cast<void>(small.update(*record, frame.timestamp, 0));
        }
    }
    NX_CHECK(small.size() == 2U);
    NX_CHECK(small.evictions() == 1U);
    NX_CHECK(!small.is_key_selected(a));
    NX_CHECK(small.selected_evicted() == 1U);
    NX_CHECK(small.find(b) != nullptr);
    NX_CHECK(small.find(c) != nullptr);
}

void test_overwrite_bytes_and_selection() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    canview::OverwriteTable table;
    table.init(8U);
    const auto key = key_for(1U, 0x200U);

    TimedFrame first{};
    first.can_id = 0x200U;
    first.timestamp = 1'000U;
    first.data = {0x01U, 0x02U, 0U, 0U, 0U, 0U, 0U, 0U};
    if (const auto record = ring->find(push_timed(*ring, first)); record.has_value()) {
        static_cast<void>(table.update(*record, 1'000U, 0));
    }

    TimedFrame changed{};
    changed.can_id = 0x200U;
    changed.timestamp = 2'000U;
    changed.data = {0x01U, 0x03U, 0U, 0U, 0U, 0U, 0U, 0U};
    const auto changed_seq = push_timed(*ring, changed);
    if (const auto record = ring->find(changed_seq); record.has_value()) {
        static_cast<void>(table.update(*record, 2'000U, 0));
    }
    table.select_key(key);
    NX_CHECK(table.is_key_selected(key));

    const auto* row = table.find(key);
    NX_CHECK(row != nullptr);
    if (row != nullptr) {
        NX_CHECK(row->byte_fading(1U, 2'000U, 1'500U));
        NX_CHECK(!row->byte_fading(1U, 4'000U, 1'500U));
        NX_CHECK(!row->byte_changed[0]);
        NX_CHECK(row->latest.data[1] == 0x03U);
        NX_CHECK(row->last_seq == changed_seq);
    }

    // DLC shrink clears timers beyond the new length.
    TimedFrame shrunk{};
    shrunk.can_id = 0x200U;
    shrunk.timestamp = 3'000U;
    shrunk.dlc = 1U;
    shrunk.data = {0x01U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    if (const auto record = ring->find(push_timed(*ring, shrunk)); record.has_value()) {
        static_cast<void>(table.update(*record, 3'000U, 0));
    }
    row = table.find(key);
    NX_CHECK(row != nullptr);
    if (row != nullptr) {
        NX_CHECK(!row->byte_changed[1]);
        NX_CHECK(!row->byte_fading(1U, 3'000U, 1'500U));
    }

    // Selection survives new frames and toggles off.
    NX_CHECK(table.is_key_selected(key));
    NX_CHECK(!table.toggle_key(key));
    NX_CHECK(!table.is_key_selected(key));
    NX_CHECK(table.toggle_key(key));
    NX_CHECK(table.is_key_selected(key));
    table.clear_selection();
    NX_CHECK(table.selected_keys().empty());
    NX_CHECK(!table.to_string().empty());
}

} // namespace

namespace canview_test {

void run_time_tests() {
    test_anchor_table_and_utc();
    test_time_modes_and_deltas();
    test_overwrite_table();
    test_overwrite_bytes_and_selection();
}

} // namespace canview_test
