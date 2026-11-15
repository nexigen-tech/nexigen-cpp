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
///
/// The vocabulary is intentionally minimal: every rejection currently shares
/// one kind, because the daemon publishes *that* a claim failed, not why (the
/// reason stays in the `ClaimVerdict` on the cold path). Adding a kind is
/// additive for consumers that treat unknown names as conservative.
///
/// @note Spec `phm/identity`, requirement "Security event channel".
enum class SecurityEventKind : std::uint8_t {
    ClaimRejected, ///< A claim was foreign, unreadable, or named no process.
};

/// @brief Canonical name of a security-event kind.
///
/// @param value Kind to name.
/// @return Stable English name (`ClaimRejected`) borrowed from the vocabulary
///         table.
/// @note Spec `phm/identity`, requirement "Security event channel".
[[nodiscard]] constexpr std::string_view to_string(SecurityEventKind value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical security-event kind.
///
/// @param name Candidate name, as produced by `to_string`.
/// @param out Receives the kind only when the name matches.
/// @return True when `name` named a kind; false leaves `out` unchanged.
/// @note Spec `phm/identity`, requirement "Security event channel".
[[nodiscard]] constexpr bool from_string(std::string_view name, SecurityEventKind& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a security-event kind.
///
/// @param j Object that receives the canonical name.
/// @param value Kind to serialize.
/// @note Mandate 12 (JSON through nlohmann); spec `phm/identity`, requirement
///       "Security event channel".
inline void to_json(nlohmann::json& j, const SecurityEventKind value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a security-event kind (non-throwing).
///
/// @param j JSON value holding the canonical name.
/// @param out Receives the parsed kind; unchanged when parsing fails.
/// @note Mandate 12; spec `phm/identity`, requirement "Security event channel".
inline void from_json(const nlohmann::json& j, SecurityEventKind& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

namespace nexigen::core {

/// @brief `SecurityEventKind` vocabulary table.
template <> struct EnumTraits<nexigen::phm::SecurityEventKind> {
    /// @brief One row of the security-event vocabulary.
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
///
/// Makes `std::format("{}", kind)` equal `to_string(kind)`.
///
/// @note Mandate 11 (universal `std::formatter`); spec `phm/identity`,
///       requirement "Security event channel".
template <>
struct std::formatter<nexigen::phm::SecurityEventKind> : std::formatter<std::string_view> {
    /// @brief Write the canonical kind name into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param value Kind to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Mandate 11; spec `phm/identity`, requirement "Security event
    ///       channel".
    template <class Ctx> auto format(const nexigen::phm::SecurityEventKind value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::phm::to_string(value), ctx);
    }
};
