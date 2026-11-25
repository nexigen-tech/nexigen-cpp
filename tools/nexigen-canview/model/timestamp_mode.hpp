// nexigen-canview — time origin selection (change add-can-monitor-view).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canview {

/// @brief What the primary time column expresses.
enum class TimestampMode : std::uint8_t {
    Driver,            ///< Raw hardware timestamp from the driver.
    SinceSessionStart, ///< Hardware time minus the channel's first record.
    RelativeT0,        ///< Hardware time minus the view's T0.
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `TimestampMode`.
template <> struct EnumTraits<nexigen::canview::TimestampMode> {
    /// @brief One value/name row.
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::TimestampMode value; ///< Enumerator. ///< Enumerator.
        std::string_view name;                 ///< Canonical name.
    };
    /// @brief Canonical table.
    /// @brief Canonical table.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::canview::TimestampMode::Driver, "Driver"},
        {nexigen::canview::TimestampMode::SinceSessionStart, "SinceSessionStart"},
        {nexigen::canview::TimestampMode::RelativeT0, "RelativeT0"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical name of a timestamp mode.
/// @param mode Mode to render.
/// @return Canonical name or `Unknown`.
[[nodiscard]] constexpr std::string_view to_string(TimestampMode mode) noexcept {
    return core::enum_name(mode);
}

/// @brief Parse a canonical timestamp mode.
/// @param name Candidate name.
/// @param out Destination, unchanged on a miss.
/// @return `true` when a name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, TimestampMode& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write a timestamp mode as JSON.
/// @param j Destination.
/// @param mode Mode to serialize.
inline void to_json(nlohmann::json& j, const TimestampMode mode) {
    core::mandate::enum_to_json(j, mode);
}

/// @brief Read a timestamp mode from JSON without throwing.
/// @param j Source.
/// @param mode Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, TimestampMode& mode) noexcept {
    core::mandate::enum_from_json(j, mode);
}

} // namespace nexigen::canview
