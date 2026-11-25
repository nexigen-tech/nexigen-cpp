#include "canopen_reindex.hpp"

#include <format>

namespace nexigen::canview {

double ReindexProgress::ratio() const noexcept {
    if (!active || total == 0U) {
        return 1.0;
    }
    return static_cast<double>(scanned) / static_cast<double>(total);
}

std::string ReindexProgress::to_string() const {
    return std::format("ReindexProgress(scanned={}, total={}, active={})", scanned, total, active);
}

void CanOpenReindexer::begin(const CanRecordRing& ring, const CobIdRouter& router) noexcept {
    router_ = &router;
    start_ = ring.head();
    scan_ = start_;
    target_ = ring.tail();
    assigned_ = 0U;
    cleared_ = 0U;
    active_ = target_ > scan_;
}

ReindexProgress CanOpenReindexer::step(CanRecordRing& ring, std::size_t max_records) noexcept {
    if (!active_ || router_ == nullptr) {
        return progress();
    }
    const std::size_t chunk = max_records == 0U ? 1U : max_records;
    std::size_t examined = 0;
    while (examined < chunk) {
        if (scan_ >= target_) {
            target_ = ring.tail();
            if (scan_ >= target_) {
                active_ = false;
                break;
            }
        }
        const std::uint64_t seq = scan_++;
        ++examined;
        const auto record = ring.find(seq);
        if (!record.has_value() || record->message == nullptr || !record->is_frame()) {
            continue;
        }
        const auto route = router_->lookup(record->message->channel_id, record->message->can_id,
                                           record->message->extended);
        const std::uint16_t slot = route.has_value() ? route->slot : CanRecordView::no_node;
        if (record->co_node_index == slot) {
            continue;
        }
        if (ring.set_co_node_index(seq, slot)) {
            if (slot == CanRecordView::no_node) {
                ++cleared_;
            } else {
                ++assigned_;
            }
        }
    }
    return progress();
}

ReindexProgress CanOpenReindexer::progress() const noexcept {
    ReindexProgress out{};
    out.total = target_ >= start_ ? target_ - start_ : 0U;
    out.scanned = scan_ >= start_ ? scan_ - start_ : 0U;
    if (out.scanned > out.total) {
        out.scanned = out.total;
    }
    out.active = active_;
    return out;
}

} // namespace nexigen::canview
