// nexigen::com — event timestamp provenance.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::com {

/// @brief Domain in which an event timestamp is expressed.
///
/// `Hardware` means the value came from a device timer (for example an FPGA
/// counter converted to nanoseconds by its driver) and must not be compared
/// with host time without an explicit calibration.
///
/// @note Spec `com/event-system`, requirement "Event header and timestamp
///       provenance".
enum class TimestampDomain : std::uint16_t {
    Unknown,      ///< No domain was declared.
    MonotonicRaw, ///< Host `CLOCK_MONOTONIC_RAW` nanoseconds.
    Monotonic,    ///< Host `CLOCK_MONOTONIC` nanoseconds.
    Realtime,     ///< Host wall-clock nanoseconds.
    Hardware,     ///< Device/FPGA timer converted to nanoseconds.
};

} // namespace nexigen::com

namespace nexigen::core {

/// @brief Vocabulary table binding `TimestampDomain` to canonical names.
template <> struct EnumTraits<nexigen::com::TimestampDomain> {
    /// @brief One value/name pair.
    struct Entry {
        nexigen::com::TimestampDomain value; ///< Enumerator.
        std::string_view name;               ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::com::TimestampDomain::Unknown, "Unknown"},
        {nexigen::com::TimestampDomain::MonotonicRaw, "MonotonicRaw"},
        {nexigen::com::TimestampDomain::Monotonic, "Monotonic"},
        {nexigen::com::TimestampDomain::Realtime, "Realtime"},
        {nexigen::com::TimestampDomain::Hardware, "Hardware"},
    }};
};

} // namespace nexigen::core

namespace nexigen::com {

/// @brief Canonical name of a timestamp domain.
/// @param value Domain to render.
/// @return Canonical name.
[[nodiscard]] constexpr std::string_view to_string(TimestampDomain value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical timestamp-domain name.
/// @param name Candidate name.
/// @param out Destination, unchanged on failure.
/// @return `true` when the name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, TimestampDomain& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a timestamp domain.
/// @param j Destination JSON value.
/// @param value Domain to serialize.
inline void to_json(nlohmann::json& j, const TimestampDomain value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a timestamp domain, strictly non-throwing.
/// @param j Source JSON value.
/// @param value Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, TimestampDomain& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::com

/// @brief Formats a timestamp domain as its canonical name.
template <>
struct std::formatter<nexigen::com::TimestampDomain> : std::formatter<std::string_view> {
    /// @brief Write the domain name into the format context.
    /// @tparam Ctx Format context type.
    /// @param value Domain to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::com::TimestampDomain value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::com::to_string(value), ctx);
    }
};
