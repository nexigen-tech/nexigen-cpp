// nexigen::metrics — ConcurrencyPolicy enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::metrics {

/// @brief How an instrument is written: by one thread, or sharded across threads.
///
/// The policy is a compile-time template argument: `SingleThreaded` selects
/// plain (non-atomic) storage and requires the caller to be the only writer,
/// while `MultiThreaded` selects atomic or sharded storage. Byte-sized and
/// append-only for wire compatibility.
///
/// @note Spec `metrics/policy-enums`, requirement "Policy enum catalogue".
/// @note Spec `metrics/counter-gauge`, requirement "Counter multi-threaded
///       sharding".
enum class ConcurrencyPolicy : std::uint8_t {
    SingleThreaded, ///< Plain storage; the caller is the only writer.
    MultiThreaded,  ///< Atomic/sharded storage for concurrent writers.
};

} // namespace nexigen::metrics

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table mapping `ConcurrencyPolicy` enumerators to names.
///
/// Backs `core::enum_name`/`core::enum_value`, hence `to_string`,
/// `from_string`, and the JSON adapters below. `constexpr` static storage with
/// a two-row linear scan; never allocates.
///
/// @note Spec `metrics/policy-enums`, requirement "Constexpr policy names".
template <> struct EnumTraits<nexigen::metrics::ConcurrencyPolicy> {
    /// @brief One enumerator/name pair of the vocabulary table.
    struct Entry {
        nexigen::metrics::ConcurrencyPolicy value; ///< Enumerator being named.
        std::string_view name;                     ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical enumerator table, in declaration order.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::metrics::ConcurrencyPolicy::SingleThreaded, "SingleThreaded"},
        {nexigen::metrics::ConcurrencyPolicy::MultiThreaded, "MultiThreaded"},
    }};
};

} // namespace nexigen::core

namespace nexigen::metrics {

/// @brief Render the canonical PascalCase name of a concurrency policy.
///
/// @param policy Enumerator to render.
/// @return Static-storage view of the canonical spelling; an out-of-table value
///         renders `"Unknown"`. Never allocates or throws.
/// @note Spec `metrics/policy-enums`, requirement "Constexpr policy names".
/// @note Diagnostic string only (project mandate 9).
[[nodiscard]] constexpr std::string_view to_string(ConcurrencyPolicy policy) noexcept {
    return core::enum_name(policy);
}

/// @brief Parse a canonical concurrency-policy spelling.
///
/// @param name View to parse; matching is exact and case-sensitive.
/// @param out Destination, assigned only on success.
/// @return `true` when `name` is a canonical spelling, `false` otherwise.
/// @note Spec `metrics/policy-enums`, requirement "Constexpr policy names".
[[nodiscard]] constexpr bool from_string(std::string_view name, ConcurrencyPolicy& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Parse a concurrency policy from a JSON string without throwing.
///
/// @param j JSON value to read; a non-string or unrecognized value is ignored.
/// @param out Destination, left at its prior value on any failure.
/// @note Spec `metrics/policy-enums`, requirement "Non-throwing policy JSON
///       serialization".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
inline void from_json(const nlohmann::json& j, ConcurrencyPolicy& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

/// @brief Write a concurrency policy as its canonical JSON string.
///
/// @param j Output JSON value, replaced with the name string.
/// @param concurrencyPolicy Enumerator to serialize.
/// @note Spec `metrics/policy-enums`, requirement "Non-throwing policy JSON
///       serialization".
/// @note Cold-path JSON only (project mandate 10).
inline void to_json(nlohmann::json& j, const ConcurrencyPolicy concurrencyPolicy) {
    core::mandate::enum_to_json(j, concurrencyPolicy);
}

} // namespace nexigen::metrics
