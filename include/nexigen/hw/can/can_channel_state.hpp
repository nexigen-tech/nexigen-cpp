#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::hw::can {

/// @brief Health state of one CAN channel.
enum class CanChannelState : std::uint8_t {
    Active,       ///< Normal operation.
    Warning,      ///< Error-warning threshold reached.
    ErrorPassive, ///< Error-passive state.
    BusOff,       ///< Bus-off state.
    Stopped,      ///< Channel stopped by the application or driver.
    Unknown,      ///< Driver did not report a state.
};

} // namespace nexigen::hw::can

namespace nexigen::core {

/// @brief Vocabulary table for `CanChannelState`.
template <> struct EnumTraits<nexigen::hw::can::CanChannelState> {
    /// @brief One value/name row.
    struct Entry {
        nexigen::hw::can::CanChannelState value; ///< Enumerator.
        std::string_view name;                   ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 6> entries{{
        {nexigen::hw::can::CanChannelState::Active, "Active"},
        {nexigen::hw::can::CanChannelState::Warning, "Warning"},
        {nexigen::hw::can::CanChannelState::ErrorPassive, "ErrorPassive"},
        {nexigen::hw::can::CanChannelState::BusOff, "BusOff"},
        {nexigen::hw::can::CanChannelState::Stopped, "Stopped"},
        {nexigen::hw::can::CanChannelState::Unknown, "Unknown"},
    }};
};

} // namespace nexigen::core

namespace nexigen::hw::can {

/// @brief Canonical name of a channel state.
/// @param state State to render.
/// @return Canonical name or `Unknown`.
[[nodiscard]] constexpr std::string_view to_string(CanChannelState state) noexcept {
    return core::enum_name(state);
}

/// @brief Parse a canonical channel-state name.
/// @param name Candidate name.
/// @param out Destination, unchanged on a miss.
/// @return `true` when a name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, CanChannelState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write a channel state as JSON.
/// @param j Destination.
/// @param state State to serialize.
inline void to_json(nlohmann::json& j, const CanChannelState state) {
    core::mandate::enum_to_json(j, state);
}

/// @brief Read a channel state from JSON without throwing.
/// @param j Source.
/// @param state Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, CanChannelState& state) noexcept {
    core::mandate::enum_from_json(j, state);
}

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanChannelState`.
template <>
struct std::formatter<nexigen::hw::can::CanChannelState> : std::formatter<std::string_view> {
    /// @brief Format the state through its canonical name.
    /// @tparam Ctx Format context.
    /// @param state State to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx>
    auto format(const nexigen::hw::can::CanChannelState state, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::hw::can::to_string(state), ctx);
    }
};
