// nexigen::logging — overflow policy for a full SPSC shard ring (spec
// logging/logger-options).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::logging {

/// @brief Policy applied when a producer finds its SPSC shard ring full.
///
/// Cold-path vocabulary read once at logger initialization by
/// `LoggerOptions::overflow_policy`; the chosen value decides whether the
/// incoming record or the oldest unread record is sacrificed, and which counter
/// the drop lands in.
///
/// @note Spec `logging/logger-options`, requirement
///       "OverflowPolicy enum controls full-ring behavior".
enum class OverflowPolicy : std::uint8_t {
    /// @brief Drop the incoming record; the drop is counted in `buffer_full`.
    DropNewest,
    /// @brief Atomically overwrite the oldest unread record; `dropped_count_` is incremented.
    OverwriteOldest,
};

} // namespace nexigen::logging

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Enum-traits table mapping `OverflowPolicy` enumerators to canonical names.
///
/// Internal vocabulary plumbing for `core::enum_name`/`core::enum_value`; the
/// names are the spellings used by `to_string`, `from_string` and JSON.
/// @note Spec `logging/logger-options`, requirement
///       "OverflowPolicy enum controls full-ring behavior".
template <> struct EnumTraits<nexigen::logging::OverflowPolicy> {
    /// @brief One enumerator-to-name pair in the table.
    struct Entry {
        /// @brief Enumerator being named.
        nexigen::logging::OverflowPolicy value;
        /// @brief Canonical lowercase spelling of `value`.
        std::string_view name;
    };
    /// @brief Table of every `OverflowPolicy` enumerator, in declaration order.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::logging::OverflowPolicy::DropNewest, "drop_newest"},
        {nexigen::logging::OverflowPolicy::OverwriteOldest, "overwrite_oldest"},
    }};
};

} // namespace nexigen::core

namespace nexigen::logging {

/// @brief Compile-time canonical name of an overflow policy.
///
/// @param p Policy to name.
/// @return `"drop_newest"` or `"overwrite_oldest"`; never throws.
/// @note Project mandate 9 (enum `to_string` vocabulary).
[[nodiscard]] constexpr std::string_view to_string(OverflowPolicy p) noexcept {
    return core::enum_name(p);
}
/// @brief Parse a canonical overflow-policy name.
///
/// @param name Canonical lowercase spelling as produced by `to_string`.
/// @param out Destination; left unchanged when `name` is not recognized.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Project mandate 9 (enum `from_string` round-trip).
[[nodiscard]] constexpr bool from_string(std::string_view name, OverflowPolicy& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief ADL JSON writer: emits the canonical name as a JSON string.
///
/// @param j Destination JSON value; overwritten.
/// @param p Policy to serialize.
/// @note Project mandate 10 (JSON vocabulary for configuration enums).
inline void to_json(nlohmann::json& j, const OverflowPolicy& p) {
    j = std::string(to_string(p));
}
/// @brief ADL JSON reader that is hostile-input safe.
///
/// An unknown or ill-typed value leaves `out` unchanged instead of throwing.
///
/// @param j Source JSON value.
/// @param out Destination policy.
/// @note Project mandate 10; non-throwing under `JSON_NOEXCEPTION=1`.
inline void from_json(const nlohmann::json& j, OverflowPolicy& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::logging
