#include "filter.hpp"

#include <algorithm>
#include <format>

namespace nexigen::logview {

bool FilterCriteria::matches(const nexigen::logging::LogFrame& frame,
                             const AppRegistry& registry) const noexcept {
    const auto level_index = static_cast<std::size_t>(frame.log_level);
    if (level_index < levels.size() && !levels[level_index]) {
        return false;
    }
    if (!source.empty() && frame.app_id_text() != std::string_view(source).substr(0, 16)) {
        return false;
    }
    if (app_filter_active) {
        const std::size_t slot = registry.find(frame.app_id_text());
        if (slot < AppRegistry::capacity && !apps[slot]) {
            return false;
        }
    }
    if (!text.empty() && frame.payload_text().find(text) == std::string_view::npos) {
        return false;
    }
    return true;
}

bool FilterCriteria::matches(const HistorySource& history, const AppRegistry& registry,
                             std::uint64_t seq) const noexcept {
    const auto view = history.find(seq);
    if (!view.has_value()) {
        return false;
    }
    if (!view->is_frame()) {
        const std::size_t level_index = static_cast<std::size_t>(view->frame->log_level);
        if (level_index < levels.size() && !levels[level_index]) {
            return false;
        }
        return text.empty() || view->frame->payload_text().find(text) != std::string_view::npos;
    }
    if (!this->source.empty() &&
        view->frame->app_id_text() != std::string_view(this->source).substr(0, 16)) {
        return false;
    }
    return matches(*view->frame, registry);
}

std::string FilterCriteria::to_string() const {
    return std::format("FilterCriteria(text={}, source={}, app_filter={}, levels={})", text, source,
                       app_filter_active, levels.size());
}

double FilterIndex::Progress::ratio() const noexcept {
    if (!active) {
        return 1.0;
    }
    if (total == 0U) {
        return 1.0;
    }
    return static_cast<double>(scanned) / static_cast<double>(total);
}

std::string FilterIndex::Progress::to_string() const {
    return std::format("Progress(scanned={}, total={}, active={})", scanned, total, active);
}

void FilterIndex::init(std::size_t capacity, const AppRegistry& apps) {
    apps_ = &apps;
    visible_.assign(capacity, 0U);
    visible_start_ = 0;
    visible_count_ = 0;
}

void FilterIndex::begin_rebuild(const HistorySource& source) noexcept {
    scan_ = 0;
    target_ = source.size();
    scan_seq_ = source.head();
    target_seq_ = source.tail();
    live_rebuild_ = source.is_live();
    active_ = target_ > 0U;
    visible_start_ = 0;
    visible_count_ = 0;
}

FilterIndex::Progress FilterIndex::step_rebuild(const HistorySource& source,
                                                std::size_t max_records) noexcept {
    if (!active_) {
        return progress();
    }
    const std::size_t chunk = max_records == 0U ? 1U : max_records;
    std::size_t examined = 0;
    if (source.is_live()) {
        // Use absolute sequence numbers so an eviction during the rebuild does
        // not shift the scan window.
        while (scan_seq_ < target_seq_ && examined < chunk) {
            const std::uint64_t seq = scan_seq_++;
            ++examined;
            if (criteria_.matches(source, *apps_, seq)) {
                append(seq);
            }
        }
        if (scan_seq_ >= target_seq_) {
            active_ = false;
            for (std::uint64_t seq = target_seq_; seq < source.tail(); ++seq) {
                if (criteria_.matches(source, *apps_, seq)) {
                    append(seq);
                }
            }
            target_seq_ = source.tail();
            scan_seq_ = target_seq_;
            target_ = source.size();
            scan_ = target_;
        }
    } else {
        while (scan_ < target_ && examined < chunk) {
            const std::uint64_t seq = source.seq_at(scan_);
            ++scan_;
            ++examined;
            if (criteria_.matches(source, *apps_, seq)) {
                append(seq);
            }
        }
        if (scan_ >= target_) {
            active_ = false;
        }
    }
    return progress();
}

FilterIndex::Progress FilterIndex::progress() const noexcept {
    Progress out{};
    out.total = target_;
    if (live_rebuild_ && target_seq_ >= target_) {
        const std::uint64_t head = target_seq_ - target_;
        out.scanned = scan_seq_ > head ? std::min<std::uint64_t>(scan_seq_ - head, target_) : 0U;
    } else {
        out.scanned = active_ ? scan_ : target_;
    }
    out.active = active_;
    return out;
}

bool FilterIndex::append_if_visible(const HistorySource& source, std::uint64_t seq) noexcept {
    if (active_ || !criteria_.matches(source, *apps_, seq)) {
        return false;
    }
    append(seq);
    return true;
}

void FilterIndex::append(std::uint64_t seq) noexcept {
    if (visible_.empty()) {
        return;
    }
    if (visible_count_ == visible_.size()) {
        visible_start_ = (visible_start_ + 1U) % visible_.size();
        --visible_count_;
    }
    visible_[(visible_start_ + visible_count_) % visible_.size()] = seq;
    ++visible_count_;
}

void FilterIndex::clear() noexcept {
    visible_start_ = 0;
    visible_count_ = 0;
    active_ = false;
    scan_ = 0;
    target_ = 0;
    scan_seq_ = 0;
    target_seq_ = 0;
    live_rebuild_ = false;
}

std::uint64_t FilterIndex::visible_at(std::size_t index) const noexcept {
    if (index >= visible_count_ || visible_.empty()) {
        return 0U;
    }
    return visible_[(visible_start_ + index) % visible_.size()];
}

std::size_t FilterIndex::compact(const HistorySource& source) noexcept {
    std::size_t kept = 0;
    for (std::size_t i = 0; i < visible_count_; ++i) {
        const std::uint64_t seq = visible_[(visible_start_ + i) % visible_.size()];
        if (source.resident(seq)) {
            visible_[(visible_start_ + kept) % visible_.size()] = seq;
            ++kept;
        }
    }
    const std::size_t removed = visible_count_ - kept;
    visible_count_ = kept;
    return removed;
}

std::size_t FilterIndex::stale(const HistorySource& source) const noexcept {
    std::size_t stale_count = 0;
    for (std::size_t i = 0; i < visible_count_; ++i) {
        if (!source.resident(visible_[(visible_start_ + i) % visible_.size()])) {
            ++stale_count;
        }
    }
    return stale_count;
}

std::string FilterIndex::to_string() const {
    return std::format("FilterIndex(active={}, scanned={}, target={}, text={})", active_, scan_,
                       target_, criteria_.text);
}

std::string to_string(const FilterIndex& filter) {
    return filter.to_string();
}

} // namespace nexigen::logview
