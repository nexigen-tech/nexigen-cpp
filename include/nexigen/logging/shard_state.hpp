// nexigen::logging — shard lifecycle state observed by the consumer drain
// loop (spec logging/logger-options).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::logging {

/// @brief Lifecycle state of one producer shard as observed by the consumer drain loop.
///
/// A shard advances `Active` → `Draining` → `Free`: the owner's `thread_local`
/// registration destructor marks it draining when the producer thread exits, and
/// the consumer returns it to the free-slot pool only after draining its ring.
/// Publishing the state is the only cross-thread interaction; the producer hot
/// path never re-reads it.
///
/// @note Spec `logging/multi-producer`, requirement "Slot exhaustion drops instead of blocking".
enum class ShardState : std::uint8_t {
    /// @brief Slot registered and accepting records from its owner thread.
    Active,
    /// @brief Producer thread exited; the consumer must drain the ring before freeing the slot.
    Draining,
    /// @brief Returned to the free-slot pool and reusable by a new producer thread.
    Free,
};

} // namespace nexigen::logging

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Enum-traits table mapping `ShardState` enumerators to canonical names.
///
/// Internal vocabulary plumbing for `core::enum_name`/`core::enum_value`; the
/// names are the spellings used by `to_string`, `from_string` and JSON.
/// @note Spec `logging/multi-producer`, requirement "Slot exhaustion drops instead of blocking".
template <> struct EnumTraits<nexigen::logging::ShardState> {
    /// @brief One enumerator-to-name pair in the table.
    struct Entry {
        /// @brief Enumerator being named.
        nexigen::logging::ShardState value;
        /// @brief Canonical lowercase spelling of `value`.
        std::string_view name;
    };
    /// @brief Table of every `ShardState` enumerator, in declaration order.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::logging::ShardState::Active, "active"},
        {nexigen::logging::ShardState::Draining, "draining"},
        {nexigen::logging::ShardState::Free, "free"},
    }};
};

} // namespace nexigen::core

namespace nexigen::logging {

/// @brief Compile-time canonical name of a shard state.
///
/// @param s State to name.
/// @return `"active"`, `"draining"` or `"free"`; never throws.
/// @note Project mandate 9 (enum `to_string` vocabulary).
[[nodiscard]] constexpr std::string_view to_string(ShardState s) noexcept {
    return core::enum_name(s);
}
/// @brief Parse a canonical shard-state name.
///
/// @param name Canonical lowercase spelling as produced by `to_string`.
/// @param out Destination; left unchanged when `name` is not recognized.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Project mandate 9 (enum `from_string` round-trip).
[[nodiscard]] constexpr bool from_string(std::string_view name, ShardState& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief ADL JSON writer: emits the canonical name as a JSON string.
///
/// @param j Destination JSON value; overwritten.
/// @param s State to serialize.
/// @note Project mandate 10 (JSON vocabulary for configuration enums).
inline void to_json(nlohmann::json& j, const ShardState& s) {
    j = std::string(to_string(s));
}
/// @brief ADL JSON reader that is hostile-input safe.
///
/// An unknown or ill-typed value leaves `out` unchanged instead of throwing.
///
/// @param j Source JSON value.
/// @param out Destination state.
/// @note Project mandate 10; non-throwing under `JSON_NOEXCEPTION=1`.
inline void from_json(const nlohmann::json& j, ShardState& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::logging
