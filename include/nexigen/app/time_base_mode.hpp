// nexigen::app — time-base selection for App<Policy> (change
// add-app-runtime-facade; spec app/runtime "Time base is external by default
// and embeddable explicitly").
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::app {

/// @brief Where an App<Policy> gets its published time base.
///
/// `External` is the production default: attach to the segment published by
/// `nexigen-chronod` and fail when it is absent. `Embedded` is an explicit
/// example/test mode that publishes a local block before initialization.
enum class TimeBaseMode : std::uint8_t {
    External, ///< Attach to an already published base.
    Embedded, ///< Publish a local base for this process.
};

} // namespace nexigen::app

namespace nexigen::core {

/// @brief Vocabulary table for `TimeBaseMode`.
template <> struct EnumTraits<nexigen::app::TimeBaseMode> {
    /// @brief One mode and its canonical spelling.
    struct Entry {
        nexigen::app::TimeBaseMode value; ///< Mode described by the row.
        std::string_view name;            ///< Canonical lowercase name.
    };
    /// @brief Name table covering every enumerator.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::app::TimeBaseMode::External, "external"},
        {nexigen::app::TimeBaseMode::Embedded, "embedded"},
    }};
};

} // namespace nexigen::core

namespace nexigen::app {

/// @brief Canonical lowercase name of a time-base mode.
///
/// @param mode Mode to render.
/// @return `external`, `embedded`, or `Unknown`.
[[nodiscard]] constexpr std::string_view to_string(TimeBaseMode mode) noexcept {
    return core::enum_name(mode);
}

/// @brief Parse a canonical time-base mode name.
///
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
[[nodiscard]] constexpr bool from_string(std::string_view name, TimeBaseMode& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write a time-base mode as its canonical lowercase name.
///
/// @param j Destination JSON value.
/// @param mode Mode to serialize.
inline void to_json(nlohmann::json& j, const TimeBaseMode mode) {
    core::mandate::enum_to_json(j, mode);
}

/// @brief Read a time-base mode from JSON without throwing.
///
/// @param j Source value; a non-string or unknown name leaves `out` unchanged.
/// @param out Destination.
inline void from_json(const nlohmann::json& j, TimeBaseMode& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::app

/// @brief `std::formatter` for `TimeBaseMode`.
template <> struct std::formatter<nexigen::app::TimeBaseMode> : std::formatter<std::string_view> {
    /// @brief Format one mode via its canonical name.
    ///
    /// @tparam Ctx Format context type.
    /// @param mode Mode to render.
    /// @param ctx Format context receiving the text.
    /// @return Iterator past the last emitted character.
    ///
    /// @note Spec `app/runtime`, requirement "Runtime vocabulary is
    ///       serializable".
    template <class Ctx> auto format(const nexigen::app::TimeBaseMode mode, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::app::to_string(mode), ctx);
    }
};
