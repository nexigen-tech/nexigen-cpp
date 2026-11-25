#include "can_view.hpp"

#include <format>

namespace nexigen::canview {

std::string CloneAdjustments::to_string() const {
    return std::format("CloneAdjustments(selection_dropped={}, bookmarks_dropped={}, "
                       "t0_cleared={})",
                       selection_dropped, bookmarks_dropped, t0_cleared);
}

void CanView::init_live(std::uint32_t id, ViewKind kind, std::uint16_t channel_id,
                        std::uint8_t node_id, std::optional<std::uint16_t> node_slot,
                        std::string title, const RecordSource& source, std::size_t slot_count,
                        const ViewDefaults& defaults) {
    id_ = id;
    kind_ = kind;
    channel_id_ = channel_id;
    node_id_ = node_id;
    node_slot_ = node_slot;
    title_ = std::move(title);
    live_ = true;
    orphaned_ = kind == ViewKind::Node && !node_slot.has_value();
    paused_ = false;
    pause_tail_ = 0;
    source_ = &source;
    snapshot_.reset();
    slot_mask_ = slot_count - 1U;
    index_.init(slot_count);
    index_.set_scope(ViewScope{kind, channel_id, node_slot});
    selection_.init(slot_count);
    bookmarks_.init(slot_count);
    export_selection_.init(slot_count);
    find_ = FindState{};
    table_mode_ = defaults.table_mode;
    timestamp_mode_ = defaults.timestamp_mode;
    delta_mode_ = defaults.delta_mode;
    time_columns_ = defaults.time_columns;
    anomaly_threshold_ns_ = defaults.anomaly_threshold_ns;
    auto_scroll_ = defaults.auto_scroll;
    scroll_ = 0;
}

void CanView::init_paused(std::uint32_t id, ViewKind kind, std::uint16_t channel_id,
                          std::uint8_t node_id, std::optional<std::uint16_t> node_slot,
                          std::string title, std::unique_ptr<SnapshotSource> snapshot,
                          std::size_t slot_count, const ViewDefaults& defaults) {
    id_ = id;
    kind_ = kind;
    channel_id_ = channel_id;
    node_id_ = node_id;
    node_slot_ = node_slot;
    title_ = std::move(title);
    live_ = false;
    orphaned_ = kind == ViewKind::Node && !node_slot.has_value();
    paused_ = true;
    pause_tail_ = 0;
    snapshot_ = std::move(snapshot);
    source_ = snapshot_.get();
    slot_mask_ = slot_count - 1U;
    index_.init(slot_count);
    index_.set_scope(ViewScope{kind, channel_id, node_slot});
    selection_.init(slot_count);
    bookmarks_.init(slot_count);
    export_selection_.init(slot_count);
    find_ = FindState{};
    table_mode_ = defaults.table_mode;
    timestamp_mode_ = defaults.timestamp_mode;
    delta_mode_ = defaults.delta_mode;
    time_columns_ = defaults.time_columns;
    anomaly_threshold_ns_ = defaults.anomaly_threshold_ns;
    auto_scroll_ = false;
    scroll_ = 0;
}

void CanView::set_aliases(std::string channel, std::string node) {
    channel_alias_ = std::move(channel);
    node_alias_ = std::move(node);
}

void CanView::set_node_binding(std::optional<std::uint16_t> slot) noexcept {
    if (node_slot_ == slot) {
        return;
    }
    node_slot_ = slot;
    index_.set_scope(ViewScope{kind_, channel_id_, slot});
    const bool now_orphaned = kind_ == ViewKind::Node && !slot.has_value();
    if (now_orphaned != orphaned_) {
        orphaned_ = now_orphaned;
        index_.clear();
        find_.clear();
    }
}

void CanView::rebuild_minimap() {
    minimap_.clear();
    minimap_.visible = index_.visible_count();
    if (minimap_.visible == 0U) {
        return;
    }
    const std::uint64_t head = display_floor();
    const std::uint64_t end = display_end();
    const std::uint64_t span = end > head ? end - head : 1U;
    for (std::size_t bucket = 0U; bucket < Minimap::buckets; ++bucket) {
        const std::size_t order = bucket * (minimap_.visible - 1U) / (Minimap::buckets - 1U);
        const std::uint64_t seq = index_.visible_seq_at(order, *source_);
        const std::uint64_t relative = seq >= head ? seq - head : 0U;
        const std::size_t mapped = relative * (Minimap::buckets - 1U) / span;
        ++minimap_.density[std::min(mapped, Minimap::buckets - 1U)];
    }
}

void CanView::rebuild_all() {
    index_.begin_rebuild(*source_);
    while (index_.rebuilding()) {
        static_cast<void>(index_.step_rebuild(*source_, 20'000U, labels()));
    }
}

CloneAdjustments CanView::copy_state_from(const CanView& other,
                                          const CloneOptions& options) noexcept {
    CloneAdjustments adjustments{};
    if (options.filter) {
        index_.criteria() = other.index_.criteria();
    }
    if (options.table_time) {
        table_mode_ = other.table_mode_;
        timestamp_mode_ = other.timestamp_mode_;
        delta_mode_ = other.delta_mode_;
        time_columns_ = other.time_columns_;
        anomaly_threshold_ns_ = other.anomaly_threshold_ns_;
        auto_scroll_ = other.auto_scroll_;
    }

    if (options.selection) {
        selection_.clear();
        for (std::uint64_t seq = other.source_->head(); seq < other.source_->tail(); ++seq) {
            if (!other.is_selected(seq)) {
                continue;
            }
            if (source_->find(seq).has_value()) {
                selection_.set(slot_of(seq), true);
            } else {
                ++adjustments.selection_dropped;
            }
        }
    }
    if (options.bookmarks) {
        bookmarks_.clear();
        for (std::uint64_t seq = other.source_->head(); seq < other.source_->tail(); ++seq) {
            if (!other.is_bookmarked(seq)) {
                continue;
            }
            if (source_->find(seq).has_value()) {
                bookmarks_.set(slot_of(seq), true);
            } else {
                ++adjustments.bookmarks_dropped;
            }
        }
    }
    if (options.t0 && other.has_t0()) {
        const std::uint64_t seq = *other.t0_seq();
        if (source_->find(seq).has_value()) {
            t0_seq_ = seq;
            t0_ns_ = other.t0_ns_;
            t0_channel_ = other.t0_channel_;
        } else {
            clear_t0();
            adjustments.t0_cleared = true;
        }
    }
    return adjustments;
}

std::size_t CanView::display_count() const noexcept {
    if (!live_ || !paused_) {
        return index_.visible_count();
    }
    const std::uint64_t end = pause_tail_;
    std::size_t low = 0U;
    std::size_t high = index_.visible_count();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (index_.visible_seq_at(middle, *source_) < end) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low;
}

std::uint64_t CanView::display_at(std::size_t order) const noexcept {
    if (order >= display_count()) {
        return 0U;
    }
    return index_.visible_seq_at(order, *source_);
}

std::uint64_t CanView::display_floor() const noexcept {
    return source_->head();
}

std::uint64_t CanView::display_end() const noexcept {
    return live_ && paused_ ? pause_tail_ : source_->tail();
}

bool CanView::focus_record(std::uint64_t seq) noexcept {
    for (std::size_t order = 0; order < index_.visible_count(); ++order) {
        if (index_.visible_seq_at(order, *source_) == seq) {
            if (live_) {
                set_paused(true);
            }
            scroll_ = order;
            auto_scroll_ = false;
            last_selected_ = seq;
            return true;
        }
    }
    return false;
}

void CanView::set_paused(bool paused) noexcept {
    if (paused == paused_) {
        return;
    }
    paused_ = paused;
    pause_tail_ = paused ? source_->tail() : 0U;
}

void CanView::set_t0(const CanRecordView& record) noexcept {
    if (record.message == nullptr) {
        return;
    }
    t0_seq_ = record.seq;
    t0_ns_ = static_cast<std::int64_t>(record.message->hardware_timestamp_ns);
    t0_channel_ = record.message->channel_id;
}

void CanView::select_only(std::uint64_t seq) noexcept {
    selection_.clear();
    last_selected_.reset();
    if (source_->resident(seq)) {
        selection_.set(slot_of(seq), true);
        last_selected_ = seq;
    }
}

void CanView::toggle_selection(std::uint64_t seq) noexcept {
    if (!source_->resident(seq)) {
        return;
    }
    const std::size_t slot = slot_of(seq);
    const bool next = !selection_.test(slot);
    selection_.set(slot, next);
    if (next) {
        last_selected_ = seq;
    } else if (last_selected_ == seq) {
        last_selected_.reset();
    }
}

void CanView::select_all_displayed() noexcept {
    const std::size_t count = display_count();
    for (std::size_t order = 0; order < count; ++order) {
        const std::uint64_t seq = display_at(order);
        if (seq != 0U) {
            selection_.set(slot_of(seq), true);
        }
    }
}

void CanView::clear_selection() noexcept {
    selection_.clear();
    last_selected_.reset();
}

bool CanView::is_selected(std::uint64_t seq) const noexcept {
    return selection_.test(slot_of(seq));
}

bool CanView::toggle_bookmark(std::uint64_t seq) noexcept {
    if (!source_->resident(seq)) {
        return false;
    }
    const std::size_t slot = slot_of(seq);
    const bool next = !bookmarks_.test(slot);
    static_cast<void>(bookmarks_.set(slot, next));
    return next;
}

bool CanView::is_bookmarked(std::uint64_t seq) const noexcept {
    return bookmarks_.test(slot_of(seq));
}

void CanView::mark_export(std::uint64_t seq) noexcept {
    if (source_->resident(seq)) {
        export_selection_.set(slot_of(seq), true);
    }
}

bool CanView::is_export_marked(std::uint64_t seq) const noexcept {
    return export_selection_.test(slot_of(seq));
}

std::string CanView::to_string() const {
    return std::format(
        "CanView(id={}, kind={}, channel={}, node={}, title='{}', live={}, orphaned={}, "
        "paused={}, visible={}, selected={}, bookmarks={})",
        id_, kind_, channel_id_, node_id_, title_, live_, orphaned_, paused_,
        index_.visible_count(), selection_.count(), bookmarks_.count());
}

} // namespace nexigen::canview
