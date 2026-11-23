#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::hw::can {

/// @brief Kind of one CAN driver message.
enum class CanMessageKind : std::uint8_t {
    ClassicData, ///< Classic CAN 2.0A/B data frame.
    FdData,      ///< CAN FD data frame.
    Remote,      ///< Classic CAN remote frame.
    Error,       ///< CAN error frame.
    Overload,    ///< CAN overload frame.
    BusIdle,     ///< Bus-idle heartbeat from the driver.
};

} // namespace nexigen::hw::can

namespace nexigen::core {

/// @brief Vocabulary table for `CanMessageKind`.
template <> struct EnumTraits<nexigen::hw::can::CanMessageKind> {
    /// @brief One value/name row.
    struct Entry {
        nexigen::hw::can::CanMessageKind value; ///< Enumerator.
        std::string_view name;                  ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 6> entries{{
        {nexigen::hw::can::CanMessageKind::ClassicData, "ClassicData"},
        {nexigen::hw::can::CanMessageKind::FdData, "FdData"},
        {nexigen::hw::can::CanMessageKind::Remote, "Remote"},
        {nexigen::hw::can::CanMessageKind::Error, "Error"},
        {nexigen::hw::can::CanMessageKind::Overload, "Overload"},
        {nexigen::hw::can::CanMessageKind::BusIdle, "BusIdle"},
    }};
};

} // namespace nexigen::core

namespace nexigen::hw::can {

/// @brief Canonical name of a message kind.
/// @param kind Kind to render.
/// @return Canonical name or `Unknown`.
[[nodiscard]] constexpr std::string_view to_string(CanMessageKind kind) noexcept {
    return core::enum_name(kind);
}

/// @brief Parse a canonical message-kind name.
/// @param name Candidate name.
/// @param out Destination, unchanged on a miss.
/// @return `true` when a name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, CanMessageKind& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write a message kind as JSON.
/// @param j Destination.
/// @param kind Kind to serialize.
inline void to_json(nlohmann::json& j, const CanMessageKind kind) {
    core::mandate::enum_to_json(j, kind);
}

/// @brief Read a message kind from JSON without throwing.
/// @param j Source.
/// @param kind Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, CanMessageKind& kind) noexcept {
    core::mandate::enum_from_json(j, kind);
}

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanMessageKind`.
template <>
struct std::formatter<nexigen::hw::can::CanMessageKind> : std::formatter<std::string_view> {
    /// @brief Format the kind through its canonical name.
    /// @tparam Ctx Format context.
    /// @param kind Kind to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx> auto format(const nexigen::hw::can::CanMessageKind kind, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::hw::can::to_string(kind), ctx);
    }
};
