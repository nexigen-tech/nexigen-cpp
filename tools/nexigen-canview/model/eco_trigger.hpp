// nexigen-canview — reason reported when ECO mode is active.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::canview {

/// @brief What pushed the viewer into ECO mode.
enum class EcoTrigger : std::uint8_t {
    None,  ///< Not throttled.
    Cpu,   ///< Process CPU share exceeded the configured limit.
    Storm, ///< Incoming message rate exceeded the storm threshold.
    Both,  ///< CPU and rate both exceeded their thresholds.
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `EcoTrigger`.
template <> struct EnumTraits<nexigen::canview::EcoTrigger> {
    /// @brief One table row.
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::EcoTrigger value; ///< Enumerator. ///< Enumerator.
        std::string_view name;              ///< Canonical lowercase wire name.
    };
    /// @brief Table covering every enumerator.
    /// @brief Canonical table.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::canview::EcoTrigger::None, "none"},
        {nexigen::canview::EcoTrigger::Cpu, "cpu"},
        {nexigen::canview::EcoTrigger::Storm, "storm"},
        {nexigen::canview::EcoTrigger::Both, "both"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical name of an ECO trigger.
/// @param trigger Trigger to render.
/// @return Static-storage name.
[[nodiscard]] constexpr std::string_view to_string(EcoTrigger trigger) noexcept {
    return nexigen::core::enum_name(trigger);
}

/// @brief Parse a canonical ECO-trigger name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
[[nodiscard]] constexpr bool from_string(std::string_view name, EcoTrigger& out) noexcept {
    return nexigen::core::enum_value(name, out);
}

/// @brief ADL JSON writer.
/// @param j Destination JSON value.
/// @param trigger Trigger to serialize.
inline void to_json(nlohmann::json& j, EcoTrigger trigger) {
    j = to_string(trigger);
}

/// @brief Non-throwing ADL JSON reader.
/// @param j Source JSON value.
/// @param out Destination, untouched on invalid input.
inline void from_json(const nlohmann::json& j, EcoTrigger& out) noexcept {
    if (!j.is_string()) {
        return;
    }
    static_cast<void>(from_string(j.get_ref<const nlohmann::json::string_t&>(), out));
}

} // namespace nexigen::canview

/// @brief `std::formatter` for `EcoTrigger`.
template <> struct std::formatter<nexigen::canview::EcoTrigger> : std::formatter<std::string_view> {
    /// @brief Format the canonical name.
    template <class Ctx> auto format(nexigen::canview::EcoTrigger trigger, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::canview::to_string(trigger), ctx);
    }
};
