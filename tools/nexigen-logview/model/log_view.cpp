#include "log_view.hpp"

#include <algorithm>
#include <format>
#include <limits>

namespace nexigen::logview {

namespace {

std::size_t index_of(const LogView& view, std::uint64_t seq, std::size_t limit) noexcept {
    for (std::size_t i = 0; i < limit; ++i) {
        if (view.display_at(i) == seq) {
            return i;
        }
    }
    return limit;
}

} // namespace

void SlotBitset::init(std::size_t slots) {
    slots_ = slots;
    words_.assign((slots + 63U) / 64U, 0U);
    count_ = 0;
}

bool SlotBitset::test(std::size_t slot) const noexcept {
    if (slot >= slots_) {
        return false;
    }
    return (words_[slot / 64U] & (1ULL << (slot % 64U))) != 0ULL;
}

bool SlotBitset::set(std::size_t slot, bool value) noexcept {
    if (slot >= slots_) {
        return false;
    }
    const std::uint64_t mask = 1ULL << (slot % 64U);
    const bool current = (words_[slot / 64U] & mask) != 0ULL;
    if (current == value) {
        return false;
    }
    if (value) {
        words_[slot / 64U] |= mask;
        ++count_;
    } else {
        words_[slot / 64U] &= ~mask;
        if (count_ > 0U) {
            --count_;
        }
    }
    return true;
}

void SlotBitset::clear() noexcept {
    std::fill(words_.begin(), words_.end(), 0U);
    count_ = 0;
}

void LogView::init_live(ViewId id, ViewKind kind, std::string source, std::string title,
                        const RecordRing& ring, const AppRegistry& registry,
                        const TimeContext& base, const ViewDefaults& defaults) {
    id_ = id;
    kind_ = kind;
    source_ = std::move(source);
    title_ = std::move(title);
    live_ = true;
    frozen_.reset();
    apps_ = &registry;
    history_ = HistorySource::live(ring);
    time_ = base;
    apply_defaults(defaults);
    allocate_state();
    begin_rebuild();
}

void LogView::init_paused(ViewId id, ViewKind kind, std::string source, std::string title,
                          std::shared_ptr<const FrozenHistory> frozen, const AppRegistry& registry,
                          const TimeContext& base, const ViewDefaults& defaults) {
    id_ = id;
    kind_ = kind;
    source_ = std::move(source);
    title_ = std::move(title);
    live_ = false;
    frozen_ = std::move(frozen);
    apps_ = &registry;
    history_ = frozen_ ? HistorySource::frozen(*frozen_) : HistorySource{};
    time_ = base;
    apply_defaults(defaults);
    allocate_state();
    paused_ = true;
    pause_tail_ = history_.tail();
    pause_floor_ = history_.head();
    pause_overwritten_ = 0;
    rebuild_now();
    refresh_pause_visible_count();
}

void LogView::apply_defaults(const ViewDefaults& defaults) noexcept {
    filter_.criteria() = defaults.criteria;
    filter_.criteria().source = source_;
    if (kind_ == ViewKind::Source) {
        filter_.criteria().app_filter_active = false;
        filter_.criteria().apps.fill(true);
    }
    timestamp_mode_ = defaults.timestamp_mode;
    delta_mode_ = defaults.delta_mode;
    time_.anomaly_threshold_ns = static_cast<std::int64_t>(defaults.anomaly_threshold_ns);
}

void LogView::allocate_state() noexcept {
    if (apps_ == nullptr) {
        return;
    }
    filter_.init(history_.slot_count(), *apps_);
    selection_.init(history_.slot_count());
    bookmarks_.init(history_.slot_count());
    minimap_.clear();
}

void LogView::begin_rebuild() noexcept {
    filter_.begin_rebuild(history_);
    find_.clear();
}

void LogView::step_rebuild(std::size_t max_records) noexcept {
    static_cast<void>(filter_.step_rebuild(history_, max_records));
    if (!filter_.rebuilding() && paused_) {
        refresh_pause_visible_count();
    }
}

void LogView::rebuild_now() noexcept {
    while (filter_.rebuilding()) {
        static_cast<void>(filter_.step_rebuild(history_, 20'000U));
    }
    if (paused_) {
        refresh_pause_visible_count();
    }
}

void LogView::note_ingest(std::uint64_t seq) noexcept {
    if (!live_) {
        return;
    }
    const auto record = history_.find(seq);
    if (!record.has_value() || !record->is_frame()) {
        return;
    }
    if (!source_.empty() &&
        record->frame->app_id_text() != std::string_view(source_).substr(0, 16)) {
        return;
    }
    minimap_.on_ingest(seq, record->frame->log_level);
}

bool LogView::append_live(std::uint64_t seq) noexcept {
    return live_ && filter_.append_if_visible(history_, seq);
}

std::size_t LogView::slot_of(std::uint64_t seq) const noexcept {
    if (live_) {
        const std::size_t slots = history_.slot_count();
        if (slots != 0U && (slots & (slots - 1U)) == 0U) {
            return static_cast<std::size_t>(seq) & (slots - 1U);
        }
    }
    return history_.slot_for(seq).value_or(history_.slot_count());
}

void LogView::clear_slot(std::size_t slot) noexcept {
    static_cast<void>(selection_.set(slot, false));
    static_cast<void>(bookmarks_.set(slot, false));
}

void LogView::on_evict(std::uint64_t seq, const RecordView& view) noexcept {
    const std::size_t slot = slot_of(seq);
    if (paused_ && seq >= pause_floor_) {
        pause_floor_ = seq + 1U;
        ++pause_overwritten_;
    }
    if (slot >= selection_.slots()) {
        return;
    }
    const bool was_bookmarked = bookmarks_.test(slot);
    if (view.is_frame()) {
        minimap_.on_evict(seq, view.frame->log_level, was_bookmarked);
    }
    clear_slot(slot);
}

std::size_t LogView::compact() noexcept {
    const std::size_t removed = filter_.compact(history_);
    if (paused_) {
        refresh_pause_visible_count();
    }
    return removed;
}

void LogView::set_paused(bool paused) noexcept {
    if (!live_ || paused_ == paused) {
        return;
    }
    paused_ = paused;
    if (paused_) {
        pause_tail_ = history_.tail();
        pause_floor_ = history_.head();
        pause_overwritten_ = 0;
        refresh_pause_visible_count();
    } else {
        pause_tail_ = 0;
        pause_floor_ = 0;
        pause_overwritten_ = 0;
        pause_visible_count_ = 0;
    }
}

std::uint64_t LogView::new_since_pause() const noexcept {
    return live_ && paused_ && history_.tail() > pause_tail_ ? history_.tail() - pause_tail_ : 0U;
}

std::uint64_t LogView::view_floor() const noexcept {
    return paused_ ? pause_floor_ : history_.head();
}

std::uint64_t LogView::view_end() const noexcept {
    return paused_ ? std::min(pause_tail_, history_.tail()) : history_.tail();
}

std::size_t LogView::display_count() const noexcept {
    if (!paused_) {
        return filter_.visible_count();
    }
    return std::min(pause_visible_count_, filter_.visible_count());
}

std::uint64_t LogView::display_at(std::size_t index) const noexcept {
    return filter_.visible_at(index);
}

void LogView::refresh_pause_visible_count() noexcept {
    if (!paused_) {
        pause_visible_count_ = 0;
        return;
    }
    std::size_t count = 0;
    const std::size_t limit = filter_.visible_count();
    while (count < limit && filter_.visible_at(count) < pause_tail_) {
        ++count;
    }
    pause_visible_count_ = count;
}

void LogView::select_only(std::uint64_t seq) noexcept {
    selection_.clear();
    selection_anchor_.reset();
    last_selected_seq_.reset();
    if (const auto slot = history_.slot_for(seq); slot.has_value()) {
        static_cast<void>(selection_.set(*slot, true));
        selection_anchor_ = seq;
        last_selected_seq_ = seq;
    }
}

void LogView::toggle_selection(std::uint64_t seq) noexcept {
    if (const auto slot = history_.slot_for(seq); slot.has_value()) {
        static_cast<void>(selection_.set(*slot, !selection_.test(*slot)));
    }
    last_selected_seq_ = seq;
}

void LogView::select_to(std::uint64_t seq) noexcept {
    if (!selection_anchor_.has_value()) {
        select_only(seq);
        return;
    }
    const std::size_t limit = display_count();
    const std::size_t first = index_of(*this, *selection_anchor_, limit);
    const std::size_t second = index_of(*this, seq, limit);
    if (first == limit || second == limit) {
        select_only(seq);
        return;
    }
    const std::size_t begin = std::min(first, second);
    const std::size_t end = std::max(first, second);
    for (std::size_t i = begin; i <= end; ++i) {
        if (const auto slot = history_.slot_for(display_at(i)); slot.has_value()) {
            static_cast<void>(selection_.set(*slot, true));
        }
    }
    last_selected_seq_ = seq;
}

void LogView::select_all_visible() noexcept {
    selection_.clear();
    const std::size_t limit = display_count();
    for (std::size_t i = 0; i < limit; ++i) {
        if (const auto slot = history_.slot_for(display_at(i)); slot.has_value()) {
            static_cast<void>(selection_.set(*slot, true));
        }
    }
}

void LogView::clear_selection() noexcept {
    selection_.clear();
    selection_anchor_.reset();
    last_selected_seq_.reset();
}

bool LogView::is_selected(std::uint64_t seq) const noexcept {
    const auto slot = history_.slot_for(seq);
    return slot.has_value() && selection_.test(*slot);
}

bool LogView::toggle_bookmark(std::uint64_t seq) noexcept {
    const auto slot = history_.slot_for(seq);
    if (!slot.has_value()) {
        return false;
    }
    const bool next = !bookmarks_.test(*slot);
    static_cast<void>(bookmarks_.set(*slot, next));
    minimap_.on_bookmark(seq, next);
    return next;
}

bool LogView::is_bookmarked(std::uint64_t seq) const noexcept {
    const auto slot = history_.slot_for(seq);
    return slot.has_value() && bookmarks_.test(*slot);
}

std::optional<std::uint64_t>
LogView::next_bookmark(std::optional<std::uint64_t> seq) const noexcept {
    const std::size_t limit = display_count();
    if (limit == 0U) {
        return std::nullopt;
    }
    std::size_t start = 0U;
    if (seq.has_value()) {
        const std::size_t position = index_of(*this, *seq, limit);
        start = position < limit ? position + 1U : 0U;
    }
    for (std::size_t step = 0; step < limit; ++step) {
        const std::size_t index = (start + step) % limit;
        if (is_bookmarked(display_at(index))) {
            return display_at(index);
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t>
LogView::previous_bookmark(std::optional<std::uint64_t> seq) const noexcept {
    const std::size_t limit = display_count();
    if (limit == 0U) {
        return std::nullopt;
    }
    std::size_t start = limit - 1U;
    if (seq.has_value()) {
        const std::size_t position = index_of(*this, *seq, limit);
        start = position > 0U ? position - 1U : limit - 1U;
    }
    for (std::size_t step = 0; step < limit; ++step) {
        const std::size_t index = (start + limit - step) % limit;
        if (is_bookmarked(display_at(index))) {
            return display_at(index);
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t>
LogView::next_attention(std::optional<std::uint64_t> seq) const noexcept {
    const std::size_t limit = display_count();
    if (limit == 0U) {
        return std::nullopt;
    }
    std::size_t start = 0U;
    if (seq.has_value()) {
        const std::size_t position = index_of(*this, *seq, limit);
        start = position < limit ? position + 1U : 0U;
    }
    for (std::size_t step = 0; step < limit; ++step) {
        const std::size_t index = (start + step) % limit;
        if (const auto view = history_.find(display_at(index));
            view.has_value() && view->is_frame() &&
            view->frame->log_level >= nexigen::core::LogLevel::Warning) {
            return view->seq;
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t>
LogView::previous_attention(std::optional<std::uint64_t> seq) const noexcept {
    const std::size_t limit = display_count();
    if (limit == 0U) {
        return std::nullopt;
    }
    std::size_t start = limit - 1U;
    if (seq.has_value()) {
        const std::size_t position = index_of(*this, *seq, limit);
        start = position > 0U ? position - 1U : limit - 1U;
    }
    for (std::size_t step = 0; step < limit; ++step) {
        const std::size_t index = (start + limit - step) % limit;
        if (const auto view = history_.find(display_at(index));
            view.has_value() && view->is_frame() &&
            view->frame->log_level >= nexigen::core::LogLevel::Warning) {
            return view->seq;
        }
    }
    return std::nullopt;
}

void LogView::copy_filter_criteria(const LogView& other, const CloneOptions& options) noexcept {
    if (options.levels) {
        filter_.criteria().levels = other.filter_.criteria().levels;
    }
    if (options.text) {
        filter_.criteria().text = other.filter_.criteria().text;
    }
    if (kind_ == ViewKind::All && other.kind_ == ViewKind::All) {
        filter_.criteria().apps = other.filter_.criteria().apps;
        filter_.criteria().app_filter_active = other.filter_.criteria().app_filter_active;
    }
}

bool LogView::copy_time_settings(const LogView& other, const CloneOptions& options) noexcept {
    if (options.timestamp_mode) {
        timestamp_mode_ = other.timestamp_mode_;
    }
    if (options.delta_mode) {
        delta_mode_ = other.delta_mode_;
    }
    if (options.anomaly_threshold) {
        time_.anomaly_threshold_ns = other.time_.anomaly_threshold_ns;
    }
    if (!options.t0 || !other.time_.anchor_ns.has_value()) {
        time_.anchor_ns.reset();
        return false;
    }
    if (!contains_stamp(static_cast<std::uint64_t>(*other.time_.anchor_ns))) {
        return true;
    }
    time_.anchor_ns = other.time_.anchor_ns;
    return false;
}

void LogView::replay_view_state(const LogView& other, const CloneOptions& options,
                                std::uint64_t& selection_copied,
                                std::uint64_t& bookmarks_copied) noexcept {
    selection_.clear();
    bookmarks_.clear();
    minimap_.clear();
    for (std::size_t i = 0; i < history_.size(); ++i) {
        const std::uint64_t seq = history_.seq_at(i);
        const auto slot = history_.slot_for(seq);
        if (!slot.has_value()) {
            continue;
        }
        if (options.selection && other.is_selected(seq)) {
            static_cast<void>(selection_.set(*slot, true));
            ++selection_copied;
        }
        if (options.bookmarks && other.is_bookmarked(seq)) {
            static_cast<void>(bookmarks_.set(*slot, true));
            minimap_.on_bookmark(seq, true);
            ++bookmarks_copied;
        }
        if (live_) {
            note_ingest(seq);
        } else if (const auto record = history_.find(seq);
                   record.has_value() && record->is_frame()) {
            minimap_.on_ingest(seq, record->frame->log_level);
        }
    }
}

void LogView::rebuild_find(const LogView& other, const CloneOptions& options) noexcept {
    if (!options.find || other.find_.query().empty()) {
        find_.clear();
        return;
    }
    find_.begin(other.find_.query(), filter_);
    while (find_.active()) {
        static_cast<void>(find_.step(filter_, history_, 20'000U));
    }
}

CloneAdjustments LogView::copy_state_from(const LogView& other,
                                          const CloneOptions& options) noexcept {
    CloneAdjustments adjustments{};
    const std::uint64_t selection_before = other.selected_count();
    const std::uint64_t bookmarks_before = other.bookmarked_count();
    std::uint64_t selection_copied = 0;
    std::uint64_t bookmarks_copied = 0;
    copy_filter_criteria(other, options);
    adjustments.t0_cleared = copy_time_settings(other, options);

    begin_rebuild();
    rebuild_now();
    replay_view_state(other, options, selection_copied, bookmarks_copied);
    rebuild_find(other, options);

    if (options.selection && selection_before > selection_copied) {
        adjustments.selection_dropped = selection_before - selection_copied;
    }
    if (options.bookmarks && bookmarks_before > bookmarks_copied) {
        adjustments.bookmarks_dropped = bookmarks_before - bookmarks_copied;
    }
    return adjustments;
}

std::string CloneAdjustments::to_string() const {
    if (!any()) {
        return {};
    }
    std::string text = "clone clipped:";
    if (selection_dropped > 0U) {
        text += std::format(" selection={}", selection_dropped);
    }
    if (bookmarks_dropped > 0U) {
        text += std::format(" bookmarks={}", bookmarks_dropped);
    }
    if (t0_cleared) {
        text += " t0 cleared";
    }
    return text;
}

void LogView::sync_time_base(const TimeContext& base) noexcept {
    time_.time_base_attached = base.time_base_attached;
    time_.utc_offset_ns = base.utc_offset_ns;
    time_.mixed_boot = base.mixed_boot;
}

void LogView::reset_after_clear() noexcept {
    filter_.clear();
    find_.clear();
    selection_.clear();
    bookmarks_.clear();
    minimap_.clear();
    paused_ = false;
    pause_tail_ = 0;
    pause_floor_ = 0;
    pause_overwritten_ = 0;
    pause_visible_count_ = 0;
    selection_anchor_.reset();
    last_selected_seq_.reset();
    time_.clear_anchor();
    begin_rebuild();
}

void LogView::reset_to_defaults(const ViewDefaults& defaults) noexcept {
    apply_defaults(defaults);
    find_.clear();
    selection_.clear();
    bookmarks_.clear();
    minimap_.clear();
    paused_ = false;
    pause_tail_ = 0;
    pause_floor_ = 0;
    pause_overwritten_ = 0;
    pause_visible_count_ = 0;
    selection_anchor_.reset();
    last_selected_seq_.reset();
    time_.clear_anchor();
    begin_rebuild();
}

bool LogView::contains_stamp(std::uint64_t stamp_ns) const noexcept {
    for (std::size_t i = 0; i < history_.size(); ++i) {
        const auto view = history_.find(history_.seq_at(i));
        if (view.has_value() && view->is_frame() && view->frame->timestamp_ns == stamp_ns) {
            return true;
        }
    }
    return false;
}

std::string LogView::to_string() const {
    return std::format(
        "LogView(id={}, title={}, kind={}, live={}, source={}, visible={}, selected={}, "
        "paused={})",
        id_, title_, static_cast<int>(kind_), live_, source_, filter_.visible_count(),
        selection_.count(), paused_);
}

std::string to_string(const LogView& view) {
    return view.to_string();
}

} // namespace nexigen::logview
