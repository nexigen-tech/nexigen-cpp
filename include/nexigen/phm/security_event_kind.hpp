// nexigen::phm — security event vocabulary (change checkpoint-identity).
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::phm {

/// @brief What triggered a security event.
enum class SecurityEventKind : std::uint8_t {
    ClaimRejected, ///< A claim was foreign, unreadable, or named no process.
};

/// @brief Canonical name of a security-event kind.
[[nodiscard]] constexpr std::string_view to_string(SecurityEventKind value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical security-event kind.
[[nodiscard]] constexpr bool from_string(std::string_view name, SecurityEventKind& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a security-event kind.
inline void to_json(nlohmann::json& j, const SecurityEventKind value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a security-event kind (non-throwing).
inline void from_json(const nlohmann::json& j, SecurityEventKind& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

namespace nexigen::core {

/// @brief `SecurityEventKind` vocabulary table.
template <> struct EnumTraits<nexigen::phm::SecurityEventKind> {
    struct Entry {
        nexigen::phm::SecurityEventKind value; ///< Enumerator.
        std::string_view name;                 ///< Canonical name.
    };
    static constexpr std::array<Entry, 1> entries{{
        {nexigen::phm::SecurityEventKind::ClaimRejected, "ClaimRejected"},
    }};
};

} // namespace nexigen::core

/// @brief `std::formatter` for a security-event kind (project mandate 11).
template <>
struct std::formatter<nexigen::phm::SecurityEventKind> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::SecurityEventKind value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::phm::to_string(value), ctx);
    }
};
