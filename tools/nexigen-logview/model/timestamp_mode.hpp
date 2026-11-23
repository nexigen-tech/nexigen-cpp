// nexigen-logview — timestamp display modes.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::logview {

/// @brief How a record timestamp is rendered.
enum class TimestampMode : std::uint8_t {
    Absolute,   ///< Calibrated UTC (or the sampled-offset fallback).
    SinceBoot,  ///< Raw monotonic nanoseconds.
    RelativeT0, ///< Signed offset from the anchor record.
};

} // namespace nexigen::logview

namespace nexigen::core {

/// @brief Vocabulary table for `TimestampMode`.
template <> struct EnumTraits<nexigen::logview::TimestampMode> {
    /// @brief One table row.
    struct Entry {
        nexigen::logview::TimestampMode value; ///< Enumerator.
        std::string_view name;                 ///< Canonical lowercase wire name.
    };
    /// @brief Table covering every enumerator.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::logview::TimestampMode::Absolute, "absolute"},
        {nexigen::logview::TimestampMode::SinceBoot, "since_boot"},
        {nexigen::logview::TimestampMode::RelativeT0, "relative_t0"},
    }};
};
} // namespace nexigen::core

namespace nexigen::logview {

/// @brief Canonical name of a timestamp mode.
/// @param mode Mode to render.
/// @return Static-storage name.
[[nodiscard]] constexpr std::string_view to_string(TimestampMode mode) noexcept {
    return nexigen::core::enum_name(mode);
}

/// @brief Parse a canonical timestamp-mode name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
[[nodiscard]] constexpr bool from_string(std::string_view name, TimestampMode& out) noexcept {
    return nexigen::core::enum_value(name, out);
}

/// @brief ADL JSON writer.
/// @param j Destination JSON value.
/// @param mode Mode to serialize.
inline void to_json(nlohmann::json& j, TimestampMode mode) {
    j = to_string(mode);
}

/// @brief Non-throwing ADL JSON reader.
/// @param j Source JSON value.
/// @param out Destination, untouched on invalid input.
inline void from_json(const nlohmann::json& j, TimestampMode& out) noexcept {
    if (!j.is_string()) {
        return;
    }
    static_cast<void>(from_string(j.get_ref<const nlohmann::json::string_t&>(), out));
}

} // namespace nexigen::logview

/// @brief `std::formatter` for `TimestampMode`.
template <>
struct std::formatter<nexigen::logview::TimestampMode> : std::formatter<std::string_view> {
    /// @brief Format the canonical name.
    template <class Ctx> auto format(nexigen::logview::TimestampMode mode, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::logview::to_string(mode), ctx);
    }
};
