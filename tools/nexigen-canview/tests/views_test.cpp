// Filter, visible index, view state, cloning, focus and persistence tests.
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/hw/can/can_message.hpp>

#include "can_record_ring.hpp"
#include "can_view.hpp"
#include "can_view_model.hpp"
#include "filter_criteria.hpp"
#include "record_meta.hpp"
#include "record_source.hpp"
#include "snapshot_source.hpp"
#include "suite.hpp"
#include "support/check.hpp"
#include "visible_index.hpp"

namespace {

namespace canview = nexigen::canview;
namespace hwcan = nexigen::hw::can;

struct FrameSpec {
    std::uint16_t channel{1U};
    std::uint32_t can_id{0x100U};
    std::uint64_t timestamp{1'000U};
    std::uint16_t node{canview::CanRecordView::no_node};
    hwcan::CanMessageOrigin origin{hwcan::CanMessageOrigin::ExternalDevice};
    hwcan::CanMessageKind kind{hwcan::CanMessageKind::ClassicData};
    std::uint8_t dlc{8U};
    std::array<std::uint8_t, 8> data{};
};

std::uint64_t push_frame(canview::CanRecordRing& ring, const FrameSpec& spec) {
    hwcan::CanMessage message{};
    message.channel_id = spec.channel;
    message.can_id = spec.can_id;
    message.hardware_timestamp_ns = spec.timestamp;
    message.origin = spec.origin;
    message.kind = spec.kind;
    message.dlc = spec.dlc;
    message.data.fill(0U);
    for (std::size_t index = 0; index < spec.data.size() && index < spec.dlc; ++index) {
        message.data[index] = spec.data[index];
    }
    message.frame_flags.bits =
        hwcan::CanFrameFlags::has_hardware_timestamp | hwcan::CanFrameFlags::has_hardware_sequence;
    const std::uint8_t meta = canview::pack_frame_meta(canview::CanRecordKind::Frame);
    return ring.push(message, static_cast<std::int64_t>(spec.timestamp), spec.node, meta, 0U, 0U);
}

std::uint64_t push_gap(canview::CanRecordRing& ring, std::uint16_t channel, std::uint64_t timestamp,
                       std::uint64_t count) {
    hwcan::CanMessage marker{};
    marker.channel_id = channel;
    marker.hardware_timestamp_ns = timestamp;
    const std::uint8_t meta = canview::pack_gap_meta(canview::CanRecordKind::TransportGap,
                                                     canview::GapReason::HardwareSequence, true);
    return ring.push(marker, static_cast<std::int64_t>(count), canview::CanRecordView::no_node,
                     meta, 0U, 0U);
}

std::uint64_t push_bus_idle(canview::CanRecordRing& ring, std::uint16_t channel,
                            std::uint64_t timestamp) {
    hwcan::CanMessage marker{};
    marker.channel_id = channel;
    marker.kind = hwcan::CanMessageKind::BusIdle;
    marker.hardware_timestamp_ns = timestamp;
    const std::uint8_t meta = canview::pack_frame_meta(canview::CanRecordKind::BusIdleHeartbeat);
    return ring.push(marker, 0, canview::CanRecordView::no_node, meta, 0U, 0U);
}

std::optional<canview::CanRecordView> record_at(const canview::CanRecordRing& ring,
                                                std::uint64_t seq) {
    return ring.find(seq);
}

void test_filter_criteria() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    FrameSpec text_frame{};
    text_frame.channel = 1U;
    text_frame.can_id = 0x100U;
    text_frame.data = {'A', 'B', 'C', 0U, 0U, 0U, 0U, 0U};
    const auto text_seq = push_frame(*ring, text_frame);

    FrameSpec hex_frame{};
    hex_frame.channel = 2U;
    hex_frame.can_id = 0x200U;
    hex_frame.data = {0x01U, 0x02U, 0xABU, 0U, 0U, 0U, 0U, 0U};
    const auto hex_seq = push_frame(*ring, hex_frame);

    const auto idle_seq = push_bus_idle(*ring, 1U, 2'000U);
    const auto gap_seq = push_gap(*ring, 1U, 3'000U, 5U);

    FrameSpec error{};
    error.channel = 1U;
    error.kind = hwcan::CanMessageKind::Error;
    error.dlc = 0U;
    const auto error_seq = push_frame(*ring, error);

    canview::FilterCriteria criteria{};
    criteria.channel_mask = canview::FilterCriteria::channel_bit(1U);
    NX_CHECK(criteria.matches(*record_at(*ring, text_seq)));
    NX_CHECK(!criteria.matches(*record_at(*ring, hex_seq)));
    NX_CHECK(criteria.matches(*record_at(*ring, gap_seq)));

    criteria = {};
    criteria.id = 0x180U;
    criteria.id_mask = 0x780U;
    criteria.id_filter_active = true;
    NX_CHECK(!criteria.matches(*record_at(*ring, text_seq)));
    NX_CHECK(criteria.matches(*record_at(*ring, gap_seq))); // gaps bypass ID filters

    criteria = {};
    criteria.errors_only = true;
    NX_CHECK(!criteria.matches(*record_at(*ring, text_seq)));
    NX_CHECK(criteria.matches(*record_at(*ring, error_seq)));
    NX_CHECK(criteria.matches(*record_at(*ring, gap_seq)));

    criteria = {};
    criteria.show_bus_idle = true;
    NX_CHECK(criteria.matches(*record_at(*ring, idle_seq)));
    criteria.show_bus_idle = false;
    NX_CHECK(!criteria.matches(*record_at(*ring, idle_seq)));

    criteria = {};
    criteria.show_gaps = false;
    NX_CHECK(!criteria.matches(*record_at(*ring, gap_seq)));

    criteria = {};
    criteria.set_text("ab");
    NX_CHECK(criteria.matches(*record_at(*ring, hex_seq)));
    criteria.set_text("02 ab");
    NX_CHECK(criteria.matches(*record_at(*ring, hex_seq)));
    criteria.set_text("abc");
    NX_CHECK(criteria.matches(*record_at(*ring, text_seq)));
    criteria.set_text("ABC");
    NX_CHECK(criteria.matches(*record_at(*ring, text_seq)));
    criteria.set_text("hardware");
    NX_CHECK(criteria.matches(*record_at(*ring, gap_seq)));
    criteria.set_text("ch-one");
    NX_CHECK(criteria.matches(*record_at(*ring, text_seq), canview::FilterLabels{"ch-one", {}}));

    criteria = {};
    criteria.node_filter_active = true;
    criteria.set_node_visible(3U, false);
    NX_CHECK(!criteria.matches(*record_at(*ring, text_seq)));

    criteria = {};
    criteria.kind_mask = 0U;
    NX_CHECK(!criteria.matches(*record_at(*ring, text_seq)));
    NX_CHECK(criteria.matches(*record_at(*ring, gap_seq)));

    criteria = {};
    criteria.origin_mask = 0U;
    NX_CHECK(!criteria.matches(*record_at(*ring, text_seq)));

    const nlohmann::json encoded = criteria;
    canview::FilterCriteria decoded{};
    canview::from_json(encoded, decoded);
    NX_CHECK(decoded.origin_mask == criteria.origin_mask);
    NX_CHECK(!decoded.to_string().empty());
}

void test_visible_index() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    canview::RingRecordSource source{*ring};
    for (std::uint64_t index = 0U; index < 60U; ++index) {
        FrameSpec frame{};
        frame.channel = static_cast<std::uint16_t>(1U + (index % 2U));
        frame.can_id = static_cast<std::uint32_t>(0x100U + index);
        frame.timestamp = 1'000U + index;
        push_frame(*ring, frame);
    }
    canview::VisibleIndex index;
    index.init(8U);
    index.criteria().channel_mask = canview::FilterCriteria::channel_bit(1U);
    index.begin_rebuild(source);
    std::size_t steps = 0U;
    while (index.rebuilding() && steps < 10U) {
        const auto progress = index.step_rebuild(source, 20U, {});
        NX_CHECK(!progress.active || progress.ratio() <= 1.0);
        ++steps;
    }
    NX_CHECK(!index.rebuilding());
    NX_CHECK(index.visible_count() == 8U); // capacity reached
    NX_CHECK(index.stale(source) == 0U);

    const std::uint64_t first = index.visible_seq_at(0U, source);
    NX_CHECK(source.find(first).has_value());
    NX_CHECK(first < index.visible_seq_at(1U, source));

    FrameSpec extra{};
    extra.channel = 1U;
    extra.can_id = 0x400U;
    const std::uint64_t extra_seq = push_frame(*ring, extra);
    NX_CHECK(index.append_if_visible(source, extra_seq, {}));
    NX_CHECK(index.visible_count() == 8U);

    // Compact removes entries evicted from history.
    auto small = canview::CanRecordRing::create(8U);
    NX_CHECK(small.has_value());
    if (!small) {
        return;
    }
    for (std::uint64_t seq = 0U; seq < 8U; ++seq) {
        FrameSpec frame{};
        frame.channel = 1U;
        frame.timestamp = seq;
        push_frame(*small, frame);
    }
    canview::RingRecordSource small_source{*small};
    canview::VisibleIndex small_index;
    small_index.init(8U);
    small_index.begin_rebuild(small_source);
    std::size_t small_steps = 0U;
    while (small_index.rebuilding() && small_steps < 5U) {
        static_cast<void>(small_index.step_rebuild(small_source, 20U, {}));
        ++small_steps;
    }
    NX_CHECK(small_index.visible_count() == 8U);
    for (std::uint64_t seq = 0U; seq < 4U; ++seq) {
        FrameSpec frame{};
        frame.channel = 1U;
        frame.timestamp = 100U + seq;
        push_frame(*small, frame);
    }
    NX_CHECK(small_index.stale(small_source) == 4U);
    NX_CHECK(small_index.compact(small_source) == 4U);
    NX_CHECK(small_index.visible_count() == 4U);
}

void test_view_state_and_find() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    FrameSpec text{};
    text.channel = 1U;
    text.data = {'A', 'B', 'C', 0U, 0U, 0U, 0U, 0U};
    push_frame(*ring, text);
    FrameSpec other{};
    other.channel = 2U;
    other.can_id = 0x200U;
    other.data = {'x', 'y', 'z', 0U, 0U, 0U, 0U, 0U};
    push_frame(*ring, other);

    canview::CanViewModel model{*ring};
    canview::ViewDefaults defaults{};
    const auto first = model.create_view(canview::ViewKind::All, 0U, 0U, "All", defaults);
    const auto second = model.create_view(canview::ViewKind::All, 0U, 0U, "Second", defaults);
    NX_CHECK(first.has_value());
    NX_CHECK(second.has_value());
    if (!first || !second) {
        return;
    }
    model.rebuild_all();
    canview::CanView* view_a = model.view(*first);
    canview::CanView* view_b = model.view(*second);
    NX_CHECK(view_a != nullptr);
    NX_CHECK(view_b != nullptr);
    if (view_a == nullptr || view_b == nullptr) {
        return;
    }

    view_a->filter().channel_mask = canview::FilterCriteria::channel_bit(1U);
    view_a->begin_rebuild();
    model.step_rebuilds();
    NX_CHECK(view_a->display_count() == 1U);
    NX_CHECK(view_b->display_count() == 2U);
    view_a->rebuild_minimap();
    NX_CHECK(view_a->minimap().visible == view_a->display_count());
    std::uint32_t density = 0U;
    for (const std::uint32_t value : view_a->minimap().density) {
        density += value;
    }
    NX_CHECK(density > 0U);

    view_a->set_table_mode(canview::TableMode::Overwrite);
    view_a->set_timestamp_mode(canview::TimestampMode::RelativeT0);
    view_a->set_delta_mode(canview::DeltaMode::PerCanId);
    view_a->set_time_columns(canview::TimeColumnMode::Both);
    NX_CHECK(view_b->table_mode() == canview::TableMode::Trace);
    NX_CHECK(view_b->timestamp_mode() == canview::TimestampMode::Driver);

    const std::uint64_t selected = view_a->display_at(0U);
    view_a->select_only(selected);
    view_a->toggle_bookmark(selected);
    view_a->mark_export(selected);
    NX_CHECK(view_a->is_selected(selected));
    NX_CHECK(!view_b->is_selected(selected));
    NX_CHECK(view_a->is_bookmarked(selected));
    NX_CHECK(!view_b->is_bookmarked(selected));
    NX_CHECK(view_a->is_export_marked(selected));
    view_a->clear_export();
    NX_CHECK(!view_a->is_export_marked(selected));

    if (const auto record = ring->find(selected); record.has_value()) {
        view_a->set_t0(*record);
        NX_CHECK(view_a->has_t0());
    }
    NX_CHECK(!view_b->has_t0());

    view_a->find().set_query("abc");
    view_a->find().begin_rebuild(view_a->index());
    NX_CHECK(view_a->find().rebuilding());
    NX_CHECK(!view_a->find().step_rebuild(view_a->source(), view_a->index(), view_a->labels(), 1U));
    NX_CHECK(!view_a->find().rebuilding());
    NX_CHECK(view_a->find().count() == 1U);
    NX_CHECK(view_a->find().current().has_value());
    NX_CHECK(view_a->find().next().has_value());
    NX_CHECK(view_a->find().contains(selected));

    NX_CHECK(view_a->focus_record(selected));
    NX_CHECK(view_a->paused());
    NX_CHECK(view_a->scroll() == 0U);
    NX_CHECK(!view_a->auto_scroll());
    view_a->set_paused(false);

    view_a->set_paused(true);
    const std::uint64_t frozen_end = view_a->display_end();
    FrameSpec third{};
    third.channel = 1U;
    third.can_id = 0x300U;
    const std::uint64_t third_seq = push_frame(*ring, third);
    model.ingest(third_seq);
    NX_CHECK(view_a->display_end() == frozen_end);
    NX_CHECK(view_b->display_end() == ring->tail());
    view_a->set_paused(false);
    NX_CHECK(view_a->display_end() == ring->tail());
}

struct BindingTable {
    std::optional<std::uint16_t> slot{};
};

std::optional<std::uint16_t> resolve_binding(std::uint16_t channel_id, std::uint8_t node_id,
                                             void* context) {
    auto* table = static_cast<BindingTable*>(context);
    if (channel_id == 1U && node_id == 2U) {
        return table->slot;
    }
    return std::nullopt;
}

void test_view_limits_and_scopes() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    for (std::uint64_t seq = 0U; seq < 4U; ++seq) {
        FrameSpec frame{};
        frame.channel = static_cast<std::uint16_t>(1U + (seq % 2U));
        frame.node = static_cast<std::uint16_t>(seq);
        push_frame(*ring, frame);
    }
    canview::CanViewModel model{*ring};
    canview::ViewDefaults defaults{};
    for (std::uint32_t index = 0U; index < canview::CanViewModel::max_live_views; ++index) {
        NX_CHECK(model.create_view(canview::ViewKind::All, 0U, 0U, "View", defaults).has_value());
    }
    const auto ninth = model.create_view(canview::ViewKind::All, 0U, 0U, "Ninth", defaults);
    NX_CHECK(!ninth);
    NX_CHECK(ninth.error() == nexigen::core::ErrorCode::Conflict);

    const auto channel_view =
        model.create_view(canview::ViewKind::Channel, 2U, 0U, "Channel", defaults);
    NX_CHECK(!channel_view); // still at the LIVE limit

    canview::CanViewModel scope_model{*ring};
    const auto channel =
        scope_model.create_view(canview::ViewKind::Channel, 2U, 0U, "Channel 2", defaults);
    NX_CHECK(channel.has_value());
    if (channel.has_value()) {
        canview::CanView* view = scope_model.view(*channel);
        NX_CHECK(view != nullptr);
        if (view != nullptr) {
            view->begin_rebuild();
            scope_model.step_rebuilds();
            NX_CHECK(view->display_count() == 2U);
            for (std::size_t order = 0; order < view->display_count(); ++order) {
                const auto record = view->source().find(view->display_at(order));
                NX_CHECK(record.has_value());
                if (record.has_value()) {
                    NX_CHECK(record->message->channel_id == 2U);
                }
            }
        }
    }

    const auto node = scope_model.create_view(canview::ViewKind::Node, 1U, 2U, "Node", defaults,
                                              std::uint16_t{2U});
    NX_CHECK(node.has_value());
    if (node.has_value()) {
        canview::CanView* view = scope_model.view(*node);
        NX_CHECK(view != nullptr);
        if (view != nullptr) {
            view->begin_rebuild();
            scope_model.step_rebuilds();
            NX_CHECK(view->display_count() == 1U);
            const auto record = view->source().find(view->display_at(0U));
            NX_CHECK(record.has_value());
            if (record.has_value()) {
                NX_CHECK(record->co_node_index == 2U);
                NX_CHECK(record->message->channel_id == 1U);
            }
        }
    }
}

void test_node_orphan_recovery() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    FrameSpec frame{};
    frame.channel = 1U;
    frame.node = 7U;
    push_frame(*ring, frame);

    BindingTable table{};
    canview::CanViewModel model{*ring};
    model.set_node_resolver(&resolve_binding, &table);
    const auto node =
        model.create_view(canview::ViewKind::Node, 1U, 2U, "Node", canview::ViewDefaults{});
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    canview::CanView* view = model.view(*node);
    NX_CHECK(view != nullptr);
    if (view == nullptr) {
        return;
    }
    NX_CHECK(view->orphaned());
    NX_CHECK(view->display_count() == 0U);

    table.slot = 7U;
    model.refresh_node_views();
    NX_CHECK(!view->orphaned());
    model.rebuild_all();
    NX_CHECK(view->display_count() == 1U);

    table.slot.reset();
    model.refresh_node_views();
    NX_CHECK(view->orphaned());
    NX_CHECK(view->display_count() == 0U);
}

void test_clone_clipping_and_immutability() {
    auto ring = canview::CanRecordRing::create(16384U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    constexpr std::uint64_t total = 9000U;
    for (std::uint64_t seq = 0U; seq < total; ++seq) {
        FrameSpec frame{};
        frame.channel = 1U;
        frame.timestamp = 1'000U + seq;
        push_frame(*ring, frame);
    }
    canview::CanViewModel model{*ring};
    const auto live =
        model.create_view(canview::ViewKind::All, 0U, 0U, "Live", canview::ViewDefaults{});
    NX_CHECK(live.has_value());
    if (!live) {
        return;
    }
    canview::CanView* live_view = model.view(*live);
    NX_CHECK(live_view != nullptr);
    if (live_view == nullptr) {
        return;
    }
    model.rebuild_all();
    live_view->select_only(0U);
    live_view->toggle_bookmark(0U);
    live_view->select_only(total - 1U);
    const auto t0_record = ring->find(0U);
    NX_CHECK(t0_record.has_value());
    if (t0_record.has_value()) {
        live_view->set_t0(*t0_record);
    }

    canview::CloneAdjustments adjustments{};
    live_view->set_table_mode(canview::TableMode::Overwrite);
    const std::array<float, 3> source_widths{80.0F, 160.0F, 240.0F};
    live_view->set_table_widths(canview::TableMode::Trace, source_widths);
    const auto paused = model.clone_view(*live, true, "Paused", canview::CloneOptions{},
                                         canview::ViewDefaults{}, &adjustments);
    NX_CHECK(paused.has_value());
    NX_CHECK(adjustments.selection_dropped == 0U);
    NX_CHECK(adjustments.bookmarks_dropped == 1U);
    NX_CHECK(adjustments.t0_cleared);
    if (!paused) {
        return;
    }
    canview::CanView* paused_view = model.view(*paused);
    NX_CHECK(paused_view != nullptr);
    if (paused_view == nullptr) {
        return;
    }
    NX_CHECK(!paused_view->live());
    NX_CHECK(paused_view->paused());
    NX_CHECK(paused_view->table_mode() == canview::TableMode::Trace);
    const std::span<const float> copied_widths =
        paused_view->table_widths(canview::TableMode::Trace);
    NX_CHECK(copied_widths.size() == source_widths.size());
    if (copied_widths.size() == source_widths.size()) {
        for (std::size_t index = 0; index < source_widths.size(); ++index) {
            NX_CHECK(copied_widths[index] == source_widths[index]);
        }
    }
    NX_CHECK(paused_view->consume_table_layout_request());
    NX_CHECK(paused_view->source().size() <= canview::CanViewModel::snapshot_bytes / 128U);
    NX_CHECK(paused_view->display_count() > 0U);
    NX_CHECK(paused_view->display_count() <= canview::CanViewModel::snapshot_bytes / 128U);
    NX_CHECK(paused_view->is_selected(total - 1U));
    NX_CHECK(!paused_view->is_selected(0U));
    NX_CHECK(!paused_view->is_bookmarked(0U));
    NX_CHECK(!paused_view->has_t0());

    canview::CloneOptions no_state{};
    no_state.selection = false;
    no_state.bookmarks = false;
    no_state.t0 = false;
    const auto bare = model.clone_view(*live, true, "Bare", no_state, canview::ViewDefaults{});
    NX_CHECK(bare.has_value());
    if (bare.has_value()) {
        const canview::CanView* bare_view = model.view(*bare);
        NX_CHECK(bare_view != nullptr);
        if (bare_view != nullptr) {
            NX_CHECK(!bare_view->is_selected(total - 1U));
            NX_CHECK(!bare_view->is_bookmarked(0U));
            NX_CHECK(!bare_view->has_t0());
        }
    }

    const std::size_t frozen = paused_view->display_count();
    FrameSpec extra{};
    extra.channel = 1U;
    const std::uint64_t extra_seq = push_frame(*ring, extra);
    model.ingest(extra_seq);
    NX_CHECK(paused_view->display_count() == frozen);
    NX_CHECK(live_view->display_count() >= frozen);

    // The LIVE clone keeps following new records.
    const auto live_clone = model.clone_view(*live, false, "Live clone", canview::CloneOptions{},
                                             canview::ViewDefaults{});
    NX_CHECK(live_clone.has_value());
    if (live_clone.has_value()) {
        canview::CanView* clone_view = model.view(*live_clone);
        NX_CHECK(clone_view != nullptr);
        if (clone_view != nullptr) {
            NX_CHECK(clone_view->live());
            NX_CHECK(clone_view->is_selected(total - 1U));
        }
    }

    // Sixteen PAUSED clones are allowed; the seventeenth is rejected.
    for (std::size_t index = 2U; index < canview::CanViewModel::max_paused_views; ++index) {
        NX_CHECK(model
                     .clone_view(*live, true, "P" + std::to_string(index), canview::CloneOptions{},
                                 canview::ViewDefaults{})
                     .has_value());
    }
    const auto too_many =
        model.clone_view(*live, true, "P17", canview::CloneOptions{}, canview::ViewDefaults{});
    NX_CHECK(!too_many);
    NX_CHECK(too_many.error() == nexigen::core::ErrorCode::Conflict);
}

void test_focus_and_persistence() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    FrameSpec frame{};
    frame.channel = 2U;
    push_frame(*ring, frame);

    canview::CanViewModel model{*ring};
    canview::ViewDefaults defaults{};
    const auto first = model.create_view(canview::ViewKind::All, 0U, 0U, "A", defaults);
    const auto second = model.create_view(canview::ViewKind::All, 0U, 0U, "B", defaults);
    NX_CHECK(first.has_value());
    NX_CHECK(second.has_value());
    if (!first || !second) {
        return;
    }
    canview::CanView* view_a = model.view(*first);
    canview::CanView* view_b = model.view(*second);
    NX_CHECK(view_a != nullptr);
    NX_CHECK(view_b != nullptr);
    if (view_a == nullptr || view_b == nullptr) {
        return;
    }

    NX_CHECK(!model.set_focused_channel_visible(2U, false));
    model.focus(*first);
    NX_CHECK(model.focused() == view_a);
    NX_CHECK(model.set_focused_channel_visible(2U, false));
    NX_CHECK(!view_a->filter().channel_visible(2U));
    NX_CHECK(view_b->filter().channel_visible(2U));
    NX_CHECK(model.set_focused_node_visible(5U, false));
    NX_CHECK(!view_a->filter().node_visible(5U));
    NX_CHECK(view_a->filter().node_filter_active);

    view_a->filter().set_text("needle");
    view_a->set_table_mode(canview::TableMode::Overwrite);
    view_a->set_timestamp_mode(canview::TimestampMode::SinceSessionStart);
    view_a->set_delta_mode(canview::DeltaMode::FromT0);
    view_a->set_time_columns(canview::TimeColumnMode::Both);
    view_a->set_auto_scroll(false);
    view_a->set_anomaly_threshold_ns(50'000);

    const nlohmann::json persisted = model.to_json();
    NX_CHECK(persisted["views"].size() == 2U);

    canview::CanViewModel restored{*ring};
    const auto report = restored.load_json(persisted, defaults);
    NX_CHECK(report.loaded == 2U);
    NX_CHECK(report.skipped == 0U);
    NX_CHECK(report.failed == 0U);
    const canview::CanView* restored_a = restored.view(1U);
    NX_CHECK(restored_a != nullptr);
    if (restored_a != nullptr) {
        NX_CHECK(restored_a->title() == "A");
        NX_CHECK(restored_a->filter().text == "needle");
        NX_CHECK(restored_a->table_mode() == canview::TableMode::Overwrite);
        NX_CHECK(restored_a->timestamp_mode() == canview::TimestampMode::SinceSessionStart);
        NX_CHECK(restored_a->delta_mode() == canview::DeltaMode::FromT0);
        NX_CHECK(restored_a->time_columns() == canview::TimeColumnMode::Both);
        NX_CHECK(!restored_a->auto_scroll());
        NX_CHECK(restored_a->anomaly_threshold_ns() == 50'000);
    }

    nlohmann::json over_limit = {{"views", nlohmann::json::array()}};
    for (std::size_t index = 0U; index < 10U; ++index) {
        over_limit["views"].push_back({{"kind", "All"}, {"title", "V" + std::to_string(index)}});
    }
    const auto limited = restored.load_json(over_limit, defaults);
    NX_CHECK(limited.loaded == canview::CanViewModel::max_live_views);
    NX_CHECK(limited.skipped == 2U);

    nlohmann::json missing_binding = {
        {"views",
         nlohmann::json::array(
             {{{"kind", "Node"}, {"channel_id", 1U}, {"node_id", 2U}, {"title", "Orphan"}}})}};
    const auto orphan_report = restored.load_json(missing_binding, defaults);
    NX_CHECK(orphan_report.loaded == 1U);
    const canview::CanView* orphan =
        restored.views().empty() ? nullptr : restored.views().front().get();
    NX_CHECK(orphan != nullptr);
    if (orphan != nullptr) {
        NX_CHECK(orphan->orphaned());
    }
}

} // namespace

namespace canview_test {

void run_views_tests() {
    test_filter_criteria();
    test_visible_index();
    test_view_state_and_find();
    test_view_limits_and_scopes();
    test_node_orphan_recovery();
    test_clone_clipping_and_immutability();
    test_focus_and_persistence();
}

} // namespace canview_test
