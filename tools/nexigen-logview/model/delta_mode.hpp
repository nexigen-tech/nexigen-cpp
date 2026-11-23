// nexigen-logview — delta computation modes.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::logview {

/// @brief Which earlier record a delta is measured against.
enum class DeltaMode : std::uint8_t {
    PreviousVisible, ///< Previous record in the filtered visible order.
    PerApplication,  ///< Previous record of the same application.
    FromAnchor,      ///< Signed offset from the single anchor record.
};

} // namespace nexigen::logview

namespace nexigen::core {

/// @brief Vocabulary table for `DeltaMode`.
template <> struct EnumTraits<nexigen::logview::DeltaMode> {
    /// @brief One table row.
    struct Entry {
        nexigen::logview::DeltaMode value; ///< Enumerator.
        std::string_view name;             ///< Canonical lowercase wire name.
    };
    /// @brief Table covering every enumerator.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::logview::DeltaMode::PreviousVisible, "previous_visible"},
        {nexigen::logview::DeltaMode::PerApplication, "per_application"},
        {nexigen::logview::DeltaMode::FromAnchor, "from_anchor"},
    }};
};
} // namespace nexigen::core

namespace nexigen::logview {

/// @brief Canonical name of a delta mode.
/// @param mode Mode to render.
/// @return Static-storage name.
[[nodiscard]] constexpr std::string_view to_string(DeltaMode mode) noexcept {
    return nexigen::core::enum_name(mode);
}

/// @brief Parse a canonical delta-mode name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
[[nodiscard]] constexpr bool from_string(std::string_view name, DeltaMode& out) noexcept {
    return nexigen::core::enum_value(name, out);
}

/// @brief ADL JSON writer.
/// @param j Destination JSON value.
/// @param mode Mode to serialize.
inline void to_json(nlohmann::json& j, DeltaMode mode) {
    j = to_string(mode);
}

/// @brief Non-throwing ADL JSON reader.
/// @param j Source JSON value.
/// @param out Destination, untouched on invalid input.
inline void from_json(const nlohmann::json& j, DeltaMode& out) noexcept {
    if (!j.is_string()) {
        return;
    }
    static_cast<void>(from_string(j.get_ref<const nlohmann::json::string_t&>(), out));
}

} // namespace nexigen::logview

/// @brief `std::formatter` for `DeltaMode`.
template <> struct std::formatter<nexigen::logview::DeltaMode> : std::formatter<std::string_view> {
    /// @brief Format the canonical name.
    template <class Ctx> auto format(nexigen::logview::DeltaMode mode, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::logview::to_string(mode), ctx);
    }
};
