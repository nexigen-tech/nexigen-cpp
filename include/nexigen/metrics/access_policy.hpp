// nexigen::metrics — AccessPolicy enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::metrics {

/// @brief Whether the read side (`.read()` / `.snapshot()`) is part of the API.
///
/// The policy is a compile-time template argument of every instrument, so
/// `WriteOnly` instantiation removes read and snapshot entry points entirely
/// without runtime checks. Byte-sized and append-only for wire compatibility.
///
/// @note Spec `metrics/policy-enums`, requirement "Policy enum catalogue".
/// @note Spec `metrics/metric-concepts`, requirement "Compile-time access
///       gating".
enum class AccessPolicy : std::uint8_t {
    Readable,  ///< Exposes `read()` and `snapshot()`.
    WriteOnly, ///< Hides the whole read surface.
};

} // namespace nexigen::metrics

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table mapping `AccessPolicy` enumerators to canonical names.
///
/// Backs `core::enum_name`/`core::enum_value`, hence `to_string`,
/// `from_string`, and the JSON adapters below. `constexpr` static storage with
/// a two-row linear scan; never allocates.
///
/// @note Spec `metrics/policy-enums`, requirement "Constexpr policy names".
template <> struct EnumTraits<nexigen::metrics::AccessPolicy> {
    /// @brief One enumerator/name pair of the vocabulary table.
    struct Entry {
        nexigen::metrics::AccessPolicy value; ///< Enumerator being named.
        std::string_view name;                ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical enumerator table, in declaration order.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::metrics::AccessPolicy::Readable, "Readable"},
        {nexigen::metrics::AccessPolicy::WriteOnly, "WriteOnly"},
    }};
};

} // namespace nexigen::core

namespace nexigen::metrics {

/// @brief Render the canonical PascalCase name of an access policy.
///
/// @param policy Enumerator to render.
/// @return Static-storage view of the canonical spelling; an out-of-table value
///         renders `"Unknown"`. Never allocates or throws.
/// @note Spec `metrics/policy-enums`, requirement "Constexpr policy names".
/// @note Diagnostic string only (project mandate 9).
[[nodiscard]] constexpr std::string_view to_string(AccessPolicy policy) noexcept {
    return core::enum_name(policy);
}

/// @brief Parse a canonical access-policy spelling.
///
/// @param name View to parse; matching is exact and case-sensitive.
/// @param out Destination, assigned only on success.
/// @return `true` when `name` is a canonical spelling, `false` otherwise.
/// @note Spec `metrics/policy-enums`, requirement "Constexpr policy names".
[[nodiscard]] constexpr bool from_string(std::string_view name, AccessPolicy& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Parse an access policy from a JSON string without throwing.
///
/// @param j JSON value to read; a non-string or unrecognized value is ignored.
/// @param out Destination, left at its prior value on any failure.
/// @note Spec `metrics/policy-enums`, requirement "Non-throwing policy JSON
///       serialization".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
inline void from_json(const nlohmann::json& j, AccessPolicy& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

/// @brief Write an access policy as its canonical JSON string.
///
/// @param j Output JSON value, replaced with the name string.
/// @param accessPolicy Enumerator to serialize.
/// @note Spec `metrics/policy-enums`, requirement "Non-throwing policy JSON
///       serialization".
/// @note Cold-path JSON only (project mandate 10).
inline void to_json(nlohmann::json& j, const AccessPolicy accessPolicy) {
    core::mandate::enum_to_json(j, accessPolicy);
}

} // namespace nexigen::metrics
