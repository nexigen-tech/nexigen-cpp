#include "find_state.hpp"

#include <algorithm>
#include <format>

namespace nexigen::logview {

void FindState::init(std::size_t capacity) {
    matches_.reserve(capacity);
}

void FindState::begin(std::string_view query, const RecordRing& ring) {
    query_.assign(query);
    matches_.clear();
    scan_ = 0;
    target_ = ring.visible_count();
    current_ = 0;
    have_current_ = false;
    active_ = !query_.empty() && target_ > 0U;
}

bool FindState::step(const RecordRing& ring, std::size_t max_records) {
    if (!active_) {
        return true;
    }
    const std::size_t chunk = max_records == 0U ? 1U : max_records;
    std::size_t examined = 0;
    while (scan_ < target_ && examined < chunk) {
        const std::uint64_t seq = ring.visible_at(scan_);
        ++scan_;
        ++examined;
        if (const auto view = ring.find(seq);
            view.has_value() && view->is_frame() &&
            view->frame->payload_text().find(query_) != std::string_view::npos) {
            matches_.push_back(seq);
        }
    }
    if (scan_ >= target_) {
        active_ = false;
    }
    return !active_;
}

std::optional<std::uint64_t> FindState::next() noexcept {
    if (matches_.empty()) {
        return std::nullopt;
    }
    if (!have_current_) {
        current_ = 0;
        have_current_ = true;
    } else {
        current_ = (current_ + 1U) % matches_.size();
    }
    return matches_[current_];
}

std::optional<std::uint64_t> FindState::previous() noexcept {
    if (matches_.empty()) {
        return std::nullopt;
    }
    if (!have_current_) {
        current_ = matches_.size() - 1U;
        have_current_ = true;
    } else {
        current_ = (current_ + matches_.size() - 1U) % matches_.size();
    }
    return matches_[current_];
}

std::optional<std::uint64_t> FindState::current() const noexcept {
    if (!have_current_ || current_ >= matches_.size()) {
        return std::nullopt;
    }
    return matches_[current_];
}

bool FindState::contains(std::uint64_t seq) const noexcept {
    return std::binary_search(matches_.begin(), matches_.end(), seq);
}

void FindState::clear() noexcept {
    query_.clear();
    matches_.clear();
    scan_ = 0;
    target_ = 0;
    current_ = 0;
    active_ = false;
    have_current_ = false;
}

std::string FindState::to_string() const {
    return std::format("FindState(query={}, matches={}, active={})", query_, matches_.size(),
                       active_);
}

std::string to_string(const FindState& find) {
    return find.to_string();
}

} // namespace nexigen::logview
