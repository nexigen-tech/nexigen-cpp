// nexigen-canview — throttling mode reported by the resource guard.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::canview {

/// @brief Whether the viewer renders at full rate or throttles itself.
enum class ResourceMode : std::uint8_t {
    Normal, ///< Full frame rate.
    Eco,    ///< 15 FPS event-driven rendering with 250 ms recomputation.
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `ResourceMode`.
template <> struct EnumTraits<nexigen::canview::ResourceMode> {
    /// @brief One table row.
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::ResourceMode value; ///< Enumerator. ///< Enumerator.
        std::string_view name;                ///< Canonical lowercase wire name.
    };
    /// @brief Table covering every enumerator.
    /// @brief Canonical table.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::canview::ResourceMode::Normal, "normal"},
        {nexigen::canview::ResourceMode::Eco, "eco"},
    }};
};
} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical name of a resource mode.
/// @param mode Mode to render.
/// @return Static-storage name.
[[nodiscard]] constexpr std::string_view to_string(ResourceMode mode) noexcept {
    return nexigen::core::enum_name(mode);
}

/// @brief Parse a canonical resource-mode name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
[[nodiscard]] constexpr bool from_string(std::string_view name, ResourceMode& out) noexcept {
    return nexigen::core::enum_value(name, out);
}

/// @brief ADL JSON writer.
/// @param j Destination JSON value.
/// @param mode Mode to serialize.
inline void to_json(nlohmann::json& j, ResourceMode mode) {
    j = to_string(mode);
}

/// @brief Non-throwing ADL JSON reader.
/// @param j Source JSON value.
/// @param out Destination, untouched on invalid input.
inline void from_json(const nlohmann::json& j, ResourceMode& out) noexcept {
    if (!j.is_string()) {
        return;
    }
    static_cast<void>(from_string(j.get_ref<const nlohmann::json::string_t&>(), out));
}

} // namespace nexigen::canview

/// @brief `std::formatter` for `ResourceMode`.
template <>
struct std::formatter<nexigen::canview::ResourceMode> : std::formatter<std::string_view> {
    /// @brief Format the canonical name.
    template <class Ctx> auto format(nexigen::canview::ResourceMode mode, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::canview::to_string(mode), ctx);
    }
};
