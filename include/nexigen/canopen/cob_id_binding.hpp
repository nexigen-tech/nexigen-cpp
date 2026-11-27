// nexigen::canopen — neutral input for COB-ID routing.
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nexigen/canopen/eds_dictionary.hpp>

namespace nexigen::canopen {

/// @brief One node binding consumed by `CobIdRouter`.
///
/// The `slot` is opaque to the router; consumers use it to map routes back to
/// their own registry entries.
struct CobIdBinding {
    std::uint16_t slot{0};             ///< Opaque owner slot.
    std::uint16_t channel_id{0};       ///< Channel identity.
    std::uint8_t node_id{0};           ///< CANopen node id (1..127).
    const EdsDictionary* dictionary{}; ///< Optional dictionary for custom COB-IDs.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line binding summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("CobIdBinding(slot={}, channel={}, node={}, dictionary={})", slot,
                           channel_id, node_id, dictionary != nullptr);
    }
};

/// @brief Free helper mirroring `CobIdBinding::to_string`.
[[nodiscard]] inline std::string to_string(const CobIdBinding& binding) {
    return binding.to_string();
}

} // namespace nexigen::canopen
