// nexigen-canview — per-channel UTC anchor table.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "can_ingress_record.hpp"

namespace nexigen::canview {

/// @brief Bounded epoch-indexed anchor store.
///
/// Each channel keeps 256 epochs at `epoch % 256`; a newer epoch overwrites the
/// slot, so an evicted epoch renders `UTC -` instead of using a stale offset.
class AnchorTable {
public:
    /// @brief Addressable channels.
    static constexpr std::size_t channels{32U};
    /// @brief Epochs retained per channel.
    static constexpr std::size_t epochs{256U};

    /// @brief Store or replace one anchor epoch.
    /// @param event Anchor to store.
    void upsert(const AnchorEvent& event) noexcept;

    /// @brief Look up one epoch.
    /// @param channel_id Channel identity.
    /// @param epoch Anchor epoch.
    /// @return The anchor when the epoch is still resident.
    [[nodiscard]] std::optional<AnchorEvent> lookup(std::uint16_t channel_id,
                                                    std::uint16_t epoch) const noexcept;

    /// @brief Drop every anchor.
    void clear() noexcept;

    /// @brief Number of resident slots.
    /// @return Count.
    [[nodiscard]] std::size_t count() const noexcept { return count_; }

private:
    /// @brief One resident anchor slot.
    struct Slot {
        AnchorEvent event{}; ///< Stored anchor.
        bool valid{false};   ///< Whether `event` is resident.
    };

    std::array<std::array<Slot, epochs>, channels> slots_{}; ///< Slot storage.
    std::size_t count_{0};                                   ///< Resident count.
};

} // namespace nexigen::canview
