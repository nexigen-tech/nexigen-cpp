#include "anchor_table.hpp"

namespace nexigen::canview {

void AnchorTable::upsert(const AnchorEvent& event) noexcept {
    const std::size_t channel = event.channel_id % channels;
    Slot& slot = slots_[channel][event.epoch % epochs];
    if (!slot.valid) {
        ++count_;
    }
    slot.event = event;
    slot.valid = true;
}

std::optional<AnchorEvent> AnchorTable::lookup(std::uint16_t channel_id,
                                               std::uint16_t epoch) const noexcept {
    const std::size_t channel = channel_id % channels;
    const Slot& slot = slots_[channel][epoch % epochs];
    if (!slot.valid || slot.event.epoch != epoch) {
        return std::nullopt;
    }
    return slot.event;
}

void AnchorTable::clear() noexcept {
    for (auto& channel : slots_) {
        for (auto& slot : channel) {
            slot.valid = false;
        }
    }
    count_ = 0U;
}

} // namespace nexigen::canview
