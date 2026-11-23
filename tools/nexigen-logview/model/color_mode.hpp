// nexigen-logview — how application and severity colors combine.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::logview {

/// @brief Row coloring strategy.
enum class ColorMode : std::uint8_t {
    Combined,    ///< Application column uses its color; level/message use the level color.
    Criticality, ///< Error/Fatal color the whole row regardless of application color.
};

} // namespace nexigen::logview

namespace nexigen::core {

/// @brief Vocabulary table for `ColorMode`.
template <> struct EnumTraits<nexigen::logview::ColorMode> {
    /// @brief One table row.
    struct Entry {
        nexigen::logview::ColorMode value; ///< Enumerator.
        std::string_view name;             ///< Canonical lowercase wire name.
    };
    /// @brief Table covering every enumerator.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::logview::ColorMode::Combined, "combined"},
        {nexigen::logview::ColorMode::Criticality, "criticality"},
    }};
};
} // namespace nexigen::core

namespace nexigen::logview {

/// @brief Canonical name of a color mode.
/// @param mode Mode to render.
/// @return Static-storage name.
[[nodiscard]] constexpr std::string_view to_string(ColorMode mode) noexcept {
    return nexigen::core::enum_name(mode);
}

/// @brief Parse a canonical color-mode name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
[[nodiscard]] constexpr bool from_string(std::string_view name, ColorMode& out) noexcept {
    return nexigen::core::enum_value(name, out);
}

/// @brief ADL JSON writer.
/// @param j Destination JSON value.
/// @param mode Mode to serialize.
inline void to_json(nlohmann::json& j, ColorMode mode) {
    j = to_string(mode);
}

/// @brief Non-throwing ADL JSON reader.
/// @param j Source JSON value.
/// @param out Destination, untouched on invalid input.
inline void from_json(const nlohmann::json& j, ColorMode& out) noexcept {
    if (!j.is_string()) {
        return;
    }
    static_cast<void>(from_string(j.get_ref<const nlohmann::json::string_t&>(), out));
}

} // namespace nexigen::logview

/// @brief `std::formatter` for `ColorMode`.
template <> struct std::formatter<nexigen::logview::ColorMode> : std::formatter<std::string_view> {
    /// @brief Format the canonical name.
    template <class Ctx> auto format(nexigen::logview::ColorMode mode, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::logview::to_string(mode), ctx);
    }
};
