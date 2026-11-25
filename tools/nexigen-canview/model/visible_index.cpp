#include "visible_index.hpp"

#include <algorithm>
#include <format>

namespace nexigen::canview {

namespace {
constexpr std::uint64_t low_mask{0xFFFFFFFFULL};
constexpr std::uint64_t low_range{1ULL << 32U};
} // namespace

bool ViewScope::contains(const CanRecordView& record) const noexcept {
    if (kind == ViewKind::All) {
        return true;
    }
    if (record.message == nullptr || record.message->channel_id != channel_id) {
        return false;
    }
    if (kind == ViewKind::Channel || record.is_frame() == false) {
        return true;
    }
    return node_slot.has_value() && record.co_node_index == *node_slot;
}

double VisibleIndex::Progress::ratio() const noexcept {
    if (!active || total == 0U) {
        return 1.0;
    }
    return static_cast<double>(scanned) / static_cast<double>(total);
}

std::string VisibleIndex::Progress::to_string() const {
    return std::format("VisibleProgress(scanned={}, total={}, active={})", scanned, total, active);
}

void VisibleIndex::set_scope(const ViewScope& scope) noexcept {
    scope_ = scope;
    clear();
}

void VisibleIndex::init(std::size_t capacity) {
    visible_.assign(capacity, 0U);
    visible_start_ = 0;
    visible_count_ = 0;
    active_ = false;
    scan_seq_ = 0;
    target_seq_ = 0;
    target_ = 0;
}

void VisibleIndex::begin_rebuild(const RecordSource& source) noexcept {
    scan_seq_ = source.head();
    target_seq_ = source.tail();
    target_ = source.size();
    active_ = target_ > 0U;
    visible_start_ = 0;
    visible_count_ = 0;
}

VisibleIndex::Progress VisibleIndex::step_rebuild(const RecordSource& source,
                                                  std::size_t max_records,
                                                  const FilterLabels& labels) {
    if (!active_) {
        return progress();
    }
    const std::size_t chunk = max_records == 0U ? 1U : max_records;
    std::size_t examined = 0;
    while (scan_seq_ < target_seq_ && examined < chunk) {
        const std::uint64_t seq = scan_seq_++;
        ++examined;
        if (matches_record(source, seq, labels)) {
            append(seq);
        }
    }
    if (scan_seq_ >= target_seq_) {
        active_ = false;
        // Records that arrived during the rebuild append after the target.
        for (std::uint64_t seq = target_seq_; seq < source.tail(); ++seq) {
            if (matches_record(source, seq, labels)) {
                append(seq);
            }
        }
        target_seq_ = source.tail();
        scan_seq_ = target_seq_;
        target_ = source.size();
    }
    return progress();
}

VisibleIndex::Progress VisibleIndex::progress() const noexcept {
    Progress out{};
    out.total = target_;
    if (target_seq_ >= target_) {
        const std::uint64_t base = target_seq_ - target_;
        out.scanned = scan_seq_ > base ? std::min<std::uint64_t>(scan_seq_ - base, target_) : 0U;
    }
    out.active = active_;
    return out;
}

bool VisibleIndex::matches_record(const RecordSource& source, std::uint64_t seq,
                                  const FilterLabels& labels) const noexcept {
    const auto record = source.find(seq);
    return record.has_value() && scope_.contains(*record) && criteria_.matches(*record, labels);
}

bool VisibleIndex::append_if_visible(const RecordSource& source, std::uint64_t seq,
                                     const FilterLabels& labels) {
    if (active_ || !matches_record(source, seq, labels)) {
        return false;
    }
    append(seq);
    return true;
}

void VisibleIndex::append(std::uint64_t seq) noexcept {
    if (visible_.empty()) {
        return;
    }
    if (visible_count_ == visible_.size()) {
        visible_start_ = (visible_start_ + 1U) % visible_.size();
        --visible_count_;
    }
    visible_[(visible_start_ + visible_count_) % visible_.size()] =
        static_cast<std::uint32_t>(seq & low_mask);
    ++visible_count_;
}

void VisibleIndex::clear() noexcept {
    visible_start_ = 0;
    visible_count_ = 0;
    active_ = false;
    scan_seq_ = 0;
    target_seq_ = 0;
    target_ = 0;
}

std::uint32_t VisibleIndex::visible_low_at(std::size_t index) const noexcept {
    if (index >= visible_count_ || visible_.empty()) {
        return 0U;
    }
    return visible_[(visible_start_ + index) % visible_.size()];
}

std::uint64_t VisibleIndex::visible_seq_at(std::size_t index,
                                           const RecordSource& source) const noexcept {
    if (index >= visible_count_ || visible_.empty()) {
        return 0U;
    }
    const std::uint32_t low = visible_[(visible_start_ + index) % visible_.size()];
    const std::uint64_t head = source.head();
    std::uint64_t seq = (head & ~low_mask) | low;
    if (seq < head) {
        seq += low_range;
    }
    return seq;
}

std::size_t VisibleIndex::compact(const RecordSource& source) noexcept {
    if (visible_.empty()) {
        return 0U;
    }
    std::size_t kept = 0;
    for (std::size_t index = 0; index < visible_count_; ++index) {
        const std::uint64_t seq = visible_seq_at(index, source);
        if (source.resident(seq)) {
            visible_[(visible_start_ + kept) % visible_.size()] =
                static_cast<std::uint32_t>(seq & low_mask);
            ++kept;
        }
    }
    const std::size_t removed = visible_count_ - kept;
    visible_count_ = kept;
    return removed;
}

std::size_t VisibleIndex::stale(const RecordSource& source) const noexcept {
    std::size_t count = 0;
    for (std::size_t index = 0; index < visible_count_; ++index) {
        if (!source.resident(visible_seq_at(index, source))) {
            ++count;
        }
    }
    return count;
}

std::string VisibleIndex::to_string() const {
    return std::format("VisibleIndex(count={}, capacity={}, rebuilding={}, text='{}')",
                       visible_count_, visible_.size(), active_, criteria_.text);
}

} // namespace nexigen::canview
