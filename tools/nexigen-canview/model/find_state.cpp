#include "find_state.hpp"

#include <algorithm>

namespace nexigen::canview {

namespace {
FilterCriteria make_matcher() {
    FilterCriteria criteria{};
    criteria.show_bus_idle = true;
    criteria.show_gaps = true;
    criteria.kind_mask = 0xFFU;
    criteria.origin_mask = 0xFFU;
    return criteria;
}
} // namespace

FindState::FindState() : matcher_(make_matcher()) {}

void FindState::set_query(std::string value) {
    query_ = std::move(value);
    matcher_.set_text(query_);
    clear();
}

void FindState::clear() noexcept {
    matches_.clear();
    cursor_ = 0;
}

void FindState::begin_rebuild(const VisibleIndex& index) noexcept {
    matches_.clear();
    cursor_ = 0;
    scan_order_ = 0;
    scan_total_ = index.visible_count();
    scanning_ = !query_.empty() && scan_total_ > 0U;
    if (!query_.empty()) {
        matches_.reserve(scan_total_ / 8U);
    }
}

bool FindState::step_rebuild(const RecordSource& source, const VisibleIndex& index,
                             const FilterLabels& labels, std::size_t max_records) {
    if (!scanning_) {
        return false;
    }
    const std::size_t chunk = max_records == 0U ? 1U : max_records;
    const std::size_t limit = std::min(scan_total_, scan_order_ + chunk);
    while (scan_order_ < limit) {
        const std::uint64_t seq = index.visible_seq_at(scan_order_, source);
        if (source.resident(seq) && matcher_.matches(source, seq, labels)) {
            matches_.push_back(seq);
        }
        ++scan_order_;
    }
    if (scan_order_ >= scan_total_) {
        scanning_ = false;
    }
    return scanning_;
}

void FindState::rebuild(const RecordSource& source, const VisibleIndex& index,
                        const FilterLabels& labels) {
    begin_rebuild(index);
    while (step_rebuild(source, index, labels, 20'000U)) {}
}

std::optional<std::uint64_t> FindState::current() const noexcept {
    if (matches_.empty()) {
        return std::nullopt;
    }
    return matches_[cursor_ % matches_.size()];
}

std::optional<std::uint64_t> FindState::next() noexcept {
    if (matches_.empty()) {
        return std::nullopt;
    }
    cursor_ = (cursor_ + 1U) % matches_.size();
    return matches_[cursor_];
}

std::optional<std::uint64_t> FindState::previous() noexcept {
    if (matches_.empty()) {
        return std::nullopt;
    }
    cursor_ = (cursor_ + matches_.size() - 1U) % matches_.size();
    return matches_[cursor_];
}

bool FindState::contains(std::uint64_t seq) const noexcept {
    return std::binary_search(matches_.begin(), matches_.end(), seq);
}

} // namespace nexigen::canview
