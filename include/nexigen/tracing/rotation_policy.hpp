// nexigen::tracing — RotationPolicy enum (change
// refactor-hub-model-config-headers; extracted verbatim from
// json_file_trace_sink.hpp).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::tracing {

/// @brief Rotation schedule selector for the JSONL trace sink.
///
/// The enum is the file-sink's window vocabulary and follows the tracing-wide
/// enum contract: `constexpr` name conversion, non-throwing JSON, and
/// `std::format`. Values are append-only byte-sized enumerators.
///
/// @note Spec `tracing/trace-sinks`, requirement "RotationPolicy enum
///       contract"; mandates 9/10/11 apply to the helpers below.
enum class RotationPolicy : std::uint8_t {
    None,   ///< Single file for the sink's lifetime.
    Hourly, ///< A new file every UTC hour.
    Daily,  ///< A new file every UTC day.
};

} // namespace nexigen::tracing

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief `core::enum_name`/`enum_value` vocabulary for `RotationPolicy`.
///
/// The table is the single source of the canonical spellings; it stays in sync
/// with the enumerators and is used by both name directions.
template <> struct EnumTraits<nexigen::tracing::RotationPolicy> {
    /// @brief One enumerator paired with its canonical name.
    struct Entry {
        nexigen::tracing::RotationPolicy value; ///< Enumerator.
        std::string_view name;                  ///< Exact canonical spelling.
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::tracing::RotationPolicy::None, "None"},
        {nexigen::tracing::RotationPolicy::Hourly, "Hourly"},
        {nexigen::tracing::RotationPolicy::Daily, "Daily"},
    }};
};

} // namespace nexigen::core

namespace nexigen::tracing {

/// @brief Render the canonical name of a rotation policy.
///
/// @param policy Policy to render.
/// @return The exact enumerator spelling (`"None"`, `"Hourly"`, `"Daily"`), or
///         `"Unknown"` for an out-of-range cast; never throws or allocates.
/// @note Spec `tracing/trace-sinks`, requirement "RotationPolicy enum
///       contract"; project mandate 9 (`to_string`).
[[nodiscard]] constexpr std::string_view to_string(RotationPolicy policy) noexcept {
    return core::enum_name(policy);
}

/// @brief Parse a canonical rotation-policy name.
///
/// Only the exact spelling is accepted; comparison is case-sensitive.
///
/// @param name Name to parse.
/// @param out Destination, written only on success.
/// @return `true` when `name` matched a canonical spelling; on `false` the
///         out-parameter is left unchanged.
/// @note Spec `tracing/trace-sinks`, requirement "RotationPolicy enum
///       contract".
[[nodiscard]] constexpr bool from_string(std::string_view name, RotationPolicy& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Serialize the policy as its canonical name string.
///
/// @param j Destination JSON value, overwritten with the name.
/// @param policy Policy to serialize.
/// @note Spec `tracing/trace-sinks`, requirement "RotationPolicy enum
///       contract"; project mandate 10 (JSON).
inline void to_json(nlohmann::json& j, const RotationPolicy policy) {
    core::mandate::enum_to_json(j, policy);
}
/// @brief Parse the policy from JSON without throwing.
///
/// A non-string or unrecognized value leaves `out` unchanged; the call never
/// throws or aborts under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON value to read.
/// @param out Destination, written only on a recognized string.
/// @note Spec `tracing/trace-sinks`, requirement "RotationPolicy enum
///       contract"; project mandate 10 (JSON).
inline void from_json(const nlohmann::json& j, RotationPolicy& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::tracing
