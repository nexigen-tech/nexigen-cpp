#include "filter.hpp"

#include <format>

namespace nexigen::logview {

bool FilterCriteria::matches(const nexigen::logging::LogFrame& frame,
                             const AppRegistry& registry) const noexcept {
    const auto level_index = static_cast<std::size_t>(frame.log_level);
    if (level_index < levels.size() && !levels[level_index]) {
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

double FilterIndex::Progress::ratio() const noexcept {
    if (!active) {
        return 1.0;
    }
    if (total == 0U) {
        return 1.0;
    }
    return static_cast<double>(scanned) / static_cast<double>(total);
}

void FilterIndex::begin_rebuild(const RecordRing& ring) noexcept {
    active_ = true;
    scan_ = ring.head();
    target_ = ring.tail();
    ring_clear_pending_ = true;
}

FilterIndex::Progress FilterIndex::step_rebuild(RecordRing& ring,
                                                std::size_t max_records) noexcept {
    if (ring_clear_pending_) {
        ring.clear_visible();
        ring_clear_pending_ = false;
    }
    if (!active_) {
        return progress();
    }
    const std::size_t chunk = max_records == 0U ? 1U : max_records;
    std::size_t examined = 0;
    while (scan_ < target_ && examined < chunk) {
        const std::uint64_t seq = scan_;
        ++scan_;
        ++examined;
        if (const auto view = ring.find(seq); view.has_value()) {
            if (criteria_.matches(*view->frame, ring.apps())) {
                ring.append_visible(seq);
            }
        }
    }
    if (scan_ >= target_) {
        active_ = false;
        // Records ingested while the rebuild was running are newer than the
        // fixed target; append them now so the visible order stays monotonic.
        for (std::uint64_t seq = target_; seq < ring.tail(); ++seq) {
            if (const auto view = ring.find(seq); view.has_value()) {
                if (criteria_.matches(*view->frame, ring.apps())) {
                    ring.append_visible(seq);
                }
            }
        }
        target_ = ring.tail();
    }
    return progress();
}

FilterIndex::Progress FilterIndex::progress() const noexcept {
    Progress out{};
    out.scanned = active_ ? scan_ : target_;
    out.total = target_;
    out.active = active_;
    return out;
}

void FilterIndex::append_if_visible(RecordRing& ring, std::uint64_t seq) noexcept {
    if (active_) {
        return;
    }
    const auto view = ring.find(seq);
    if (!view.has_value()) {
        return;
    }
    if (criteria_.matches(*view->frame, ring.apps())) {
        ring.append_visible(seq);
    }
}

void FilterIndex::clear(RecordRing& ring) noexcept {
    ring.clear_visible();
    active_ = false;
    scan_ = 0;
    target_ = 0;
    ring_clear_pending_ = false;
}

std::string FilterIndex::to_string() const {
    return std::format("FilterIndex(active={}, scanned={}, target={}, text={})", active_, scan_,
                       target_, criteria_.text);
}

std::string to_string(const FilterIndex& filter) {
    return filter.to_string();
}

} // namespace nexigen::logview
