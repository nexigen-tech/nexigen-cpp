#include "slot_bitset.hpp"

#include <bit>

namespace nexigen::canview {

void SlotBitset::init(std::size_t slots) {
    slots_ = slots;
    words_.assign((slots + 63U) / 64U, 0U);
    count_ = 0U;
}

bool SlotBitset::test(std::size_t slot) const noexcept {
    if (slot >= slots_) {
        return false;
    }
    return (words_[slot / 64U] & (1ULL << (slot % 64U))) != 0U;
}

bool SlotBitset::set(std::size_t slot, bool value) noexcept {
    if (slot >= slots_) {
        return false;
    }
    auto& word = words_[slot / 64U];
    const std::uint64_t mask = 1ULL << (slot % 64U);
    const bool previous = (word & mask) != 0U;
    if (previous == value) {
        return false;
    }
    if (value) {
        word |= mask;
        ++count_;
    } else {
        word &= ~mask;
        --count_;
    }
    return true;
}

void SlotBitset::clear() noexcept {
    for (auto& word : words_) {
        word = 0U;
    }
    count_ = 0U;
}

} // namespace nexigen::canview
