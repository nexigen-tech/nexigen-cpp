#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::hw::can {

/// @brief Bit mask of CAN driver error conditions.
///
/// The driver reports these flags; the CAN layer preserves them unchanged.
struct CanErrorFlags {
    static constexpr std::uint32_t bit_error = 1U << 0U;           ///< Bit error.
    static constexpr std::uint32_t stuff_error = 1U << 1U;         ///< Stuff error.
    static constexpr std::uint32_t crc_error = 1U << 2U;           ///< CRC error.
    static constexpr std::uint32_t form_error = 1U << 3U;          ///< Form error.
    static constexpr std::uint32_t ack_error = 1U << 4U;           ///< Acknowledge error.
    static constexpr std::uint32_t bus_off = 1U << 5U;             ///< Bus-off condition.
    static constexpr std::uint32_t error_passive = 1U << 6U;       ///< Error-passive state.
    static constexpr std::uint32_t error_warning = 1U << 7U;       ///< Error-warning state.
    static constexpr std::uint32_t arbitration_lost = 1U << 8U;    ///< Arbitration lost.
    static constexpr std::uint32_t tx_timeout = 1U << 9U;          ///< Transmit timeout.
    static constexpr std::uint32_t controller_restart = 1U << 10U; ///< Controller restarted.
    static constexpr std::uint32_t overrun = 1U << 11U;            ///< Driver overrun.
    static constexpr std::uint32_t other = 1U << 31U;              ///< Other/unspecified error.

    std::uint32_t bits{0}; ///< Set of `CanErrorFlags` bits.

    /// @brief Whether any error bit is set.
    /// @return `true` when `bits` is non-zero.
    [[nodiscard]] constexpr bool any() const noexcept { return bits != 0U; }

    /// @brief Whether one bit is set.
    /// @param flag Bit mask to test.
    /// @return `true` when the bit is present.
    [[nodiscard]] constexpr bool has(std::uint32_t flag) const noexcept {
        return (bits & flag) != 0U;
    }

    /// @brief Render the set as comma-separated canonical names.
    /// @return Human-readable summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `CanErrorFlags::to_string`.
/// @param flags Flags to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const CanErrorFlags& flags) {
    return flags.to_string();
}

/// @brief Write error flags as a numeric JSON value.
/// @param j Destination.
/// @param flags Flags to serialize.
inline void to_json(nlohmann::json& j, const CanErrorFlags& flags) {
    j = flags.bits;
}

/// @brief Read error flags from JSON without throwing.
/// @param j Source; non-numeric values leave `flags` unchanged.
/// @param flags Destination.
inline void from_json(const nlohmann::json& j, CanErrorFlags& flags) noexcept {
    if (j.is_number_unsigned()) {
        flags.bits = j.get<std::uint32_t>();
    }
}

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanErrorFlags`.
template <>
struct std::formatter<nexigen::hw::can::CanErrorFlags> : std::formatter<std::string_view> {
    /// @brief Format the flags through `to_string`.
    /// @tparam Ctx Format context.
    /// @param flags Flags to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx> auto format(const nexigen::hw::can::CanErrorFlags& flags, Ctx& ctx) const {
        const std::string text = flags.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
