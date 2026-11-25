// nexigen-canview — time column selection (change add-can-monitor-view).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canview {

/// @brief Which time columns a view renders.
enum class TimeColumnMode : std::uint8_t {
    HwOnly,  ///< Hardware time only.
    UtcOnly, ///< Estimated UTC only.
    Both,    ///< Separate hardware and estimated-UTC columns.
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `TimeColumnMode`.
template <> struct EnumTraits<nexigen::canview::TimeColumnMode> {
    /// @brief One value/name row.
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::TimeColumnMode value; ///< Enumerator. ///< Enumerator.
        std::string_view name;                  ///< Canonical name.
    };
    /// @brief Canonical table.
    /// @brief Canonical table.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::canview::TimeColumnMode::HwOnly, "HwOnly"},
        {nexigen::canview::TimeColumnMode::UtcOnly, "UtcOnly"},
        {nexigen::canview::TimeColumnMode::Both, "Both"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical name of a time-column mode.
/// @param mode Mode to render.
/// @return Canonical name or `Unknown`.
[[nodiscard]] constexpr std::string_view to_string(TimeColumnMode mode) noexcept {
    return core::enum_name(mode);
}

/// @brief Parse a canonical time-column mode.
/// @param name Candidate name.
/// @param out Destination, unchanged on a miss.
/// @return `true` when a name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, TimeColumnMode& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write a time-column mode as JSON.
/// @param j Destination.
/// @param mode Mode to serialize.
inline void to_json(nlohmann::json& j, const TimeColumnMode mode) {
    core::mandate::enum_to_json(j, mode);
}

/// @brief Read a time-column mode from JSON without throwing.
/// @param j Source.
/// @param mode Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, TimeColumnMode& mode) noexcept {
    core::mandate::enum_from_json(j, mode);
}

} // namespace nexigen::canview
