#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::hw::can {

/// @brief Optional CAN frame attributes and presence bits.
struct CanFrameFlags {
    static constexpr std::uint16_t bit_rate_switch = 1U << 0U;        ///< CAN FD BRS.
    static constexpr std::uint16_t error_state_indicator = 1U << 1U;  ///< CAN FD ESI.
    static constexpr std::uint16_t has_hardware_timestamp = 1U << 2U; ///< Timestamp is valid.
    static constexpr std::uint16_t has_hardware_sequence = 1U << 3U;  ///< Sequence is valid.

    std::uint16_t bits{0}; ///< Set of `CanFrameFlags` bits.

    /// @brief Whether one bit is set.
    /// @param flag Bit mask to test.
    /// @return `true` when the bit is present.
    [[nodiscard]] constexpr bool has(std::uint16_t flag) const noexcept {
        return (bits & flag) != 0U;
    }

    /// @brief Render the set as comma-separated canonical names.
    /// @return Human-readable summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `CanFrameFlags::to_string`.
/// @param flags Flags to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const CanFrameFlags& flags) {
    return flags.to_string();
}

/// @brief Write frame flags as a numeric JSON value.
/// @param j Destination.
/// @param flags Flags to serialize.
inline void to_json(nlohmann::json& j, const CanFrameFlags& flags) {
    j = flags.bits;
}

/// @brief Read frame flags from JSON without throwing.
/// @param j Source; non-numeric values leave `flags` unchanged.
/// @param flags Destination.
inline void from_json(const nlohmann::json& j, CanFrameFlags& flags) noexcept {
    if (j.is_number_unsigned()) {
        flags.bits = j.get<std::uint16_t>();
    }
}

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanFrameFlags`.
template <>
struct std::formatter<nexigen::hw::can::CanFrameFlags> : std::formatter<std::string_view> {
    /// @brief Format the flags through `to_string`.
    /// @tparam Ctx Format context.
    /// @param flags Flags to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx> auto format(const nexigen::hw::can::CanFrameFlags& flags, Ctx& ctx) const {
        const std::string text = flags.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
