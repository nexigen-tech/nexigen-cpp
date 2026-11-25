// nexigen-canview — fixed bitset keyed by a history slot.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nexigen::canview {

/// @brief Fixed bitset keyed by a ring slot.
///
/// Selection, bookmarks and export selection use 2 bits per slot in the memory
/// ledger. Callers clear a bit when the slot is reused or drop it through
/// `init()` after a history reset.
class SlotBitset {
public:
    /// @brief Allocate storage for a slot count.
    /// @param slots Number of addressable slots.
    void init(std::size_t slots);

    /// @brief Whether one slot is set.
    /// @param slot Slot index.
    /// @return `true` when set.
    [[nodiscard]] bool test(std::size_t slot) const noexcept;
    /// @brief Set or clear one slot.
    /// @param slot Slot index.
    /// @param value New state.
    /// @return `true` when the stored value changed.
    bool set(std::size_t slot, bool value) noexcept;
    /// @brief Clear every slot.
    void clear() noexcept;
    /// @brief Number of set slots.
    /// @return Popcount.
    [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
    /// @brief Number of addressable slots.
    /// @return Slot count.
    [[nodiscard]] std::size_t slots() const noexcept { return slots_; }

private:
    std::vector<std::uint64_t> words_{}; ///< Bit storage.
    std::size_t slots_{0};               ///< Addressable slots.
    std::uint64_t count_{0};             ///< Set-slot count.
};

} // namespace nexigen::canview
