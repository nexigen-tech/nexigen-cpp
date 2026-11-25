// nexigen-canview — delta column selection (change add-can-monitor-view).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canview {

/// @brief Which previous record the delta column compares against.
enum class DeltaMode : std::uint8_t {
    PreviousVisible, ///< Previous visible record, regardless of channel.
    PerCanId,        ///< Previous frame with the same `(channel, id, extended)`.
    FromT0,          ///< From the view's T0 (channel-bound unless shared).
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `DeltaMode`.
template <> struct EnumTraits<nexigen::canview::DeltaMode> {
    /// @brief One value/name row.
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::DeltaMode value; ///< Enumerator. ///< Enumerator.
        std::string_view name;             ///< Canonical name.
    };
    /// @brief Canonical table.
    /// @brief Canonical table.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::canview::DeltaMode::PreviousVisible, "PreviousVisible"},
        {nexigen::canview::DeltaMode::PerCanId, "PerCanId"},
        {nexigen::canview::DeltaMode::FromT0, "FromT0"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical name of a delta mode.
/// @param mode Mode to render.
/// @return Canonical name or `Unknown`.
[[nodiscard]] constexpr std::string_view to_string(DeltaMode mode) noexcept {
    return core::enum_name(mode);
}

/// @brief Parse a canonical delta mode.
/// @param name Candidate name.
/// @param out Destination, unchanged on a miss.
/// @return `true` when a name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, DeltaMode& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write a delta mode as JSON.
/// @param j Destination.
/// @param mode Mode to serialize.
inline void to_json(nlohmann::json& j, const DeltaMode mode) {
    core::mandate::enum_to_json(j, mode);
}

/// @brief Read a delta mode from JSON without throwing.
/// @param j Source.
/// @param mode Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, DeltaMode& mode) noexcept {
    core::mandate::enum_from_json(j, mode);
}

} // namespace nexigen::canview
