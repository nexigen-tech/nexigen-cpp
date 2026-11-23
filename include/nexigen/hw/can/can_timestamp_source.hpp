#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::hw::can {

/// @brief Origin of a CAN message timestamp.
enum class CanTimestampSource : std::uint8_t {
    Hardware,       ///< Driver-supplied hardware timestamp in nanoseconds.
    DriverFallback, ///< Driver-supplied fallback timestamp.
    Simulated,      ///< Virtual-bus timestamp.
};

} // namespace nexigen::hw::can

namespace nexigen::core {

/// @brief Vocabulary table for `CanTimestampSource`.
template <> struct EnumTraits<nexigen::hw::can::CanTimestampSource> {
    /// @brief One value/name row.
    struct Entry {
        nexigen::hw::can::CanTimestampSource value; ///< Enumerator.
        std::string_view name;                      ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::hw::can::CanTimestampSource::Hardware, "Hardware"},
        {nexigen::hw::can::CanTimestampSource::DriverFallback, "DriverFallback"},
        {nexigen::hw::can::CanTimestampSource::Simulated, "Simulated"},
    }};
};

} // namespace nexigen::core

namespace nexigen::hw::can {

/// @brief Canonical name of a timestamp source.
/// @param source Source to render.
/// @return Canonical name or `Unknown`.
[[nodiscard]] constexpr std::string_view to_string(CanTimestampSource source) noexcept {
    return core::enum_name(source);
}

/// @brief Parse a canonical timestamp-source name.
/// @param name Candidate name.
/// @param out Destination, unchanged on a miss.
/// @return `true` when a name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, CanTimestampSource& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write a timestamp source as JSON.
/// @param j Destination.
/// @param source Source to serialize.
inline void to_json(nlohmann::json& j, const CanTimestampSource source) {
    core::mandate::enum_to_json(j, source);
}

/// @brief Read a timestamp source from JSON without throwing.
/// @param j Source.
/// @param source Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, CanTimestampSource& source) noexcept {
    core::mandate::enum_from_json(j, source);
}

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanTimestampSource`.
template <>
struct std::formatter<nexigen::hw::can::CanTimestampSource> : std::formatter<std::string_view> {
    /// @brief Format the source through its canonical name.
    /// @tparam Ctx Format context.
    /// @param source Source to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx>
    auto format(const nexigen::hw::can::CanTimestampSource source, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::hw::can::to_string(source), ctx);
    }
};
