// nexigen::chrono — provider mode selector (change add-clock-authority-core;
// spec chrono/chrono-settings "Provider kind setting").
//
// `daemon` wraps the threadless Provider in a ChronoWorker (periodic
// snapshots); `manual` leaves the bare Provider with snapshot_now() as the
// only trigger. Serialization trio (mandates 9-11): constexpr to_string/
// from_string and non-throwing ADL to_json/from_json under JSON_NOEXCEPTION=1,
// all one-line delegations over core::EnumTraits / core::mandate. The kind
// formats through the shared generic std::formatter (core/serialization
// mandate) — no per-type formatter specialization.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::chrono {

/// @brief How the clock authority is driven.
///
/// `daemon` wraps the threadless `Provider` in a `ChronoWorker` for periodic
/// snapshots; `manual` leaves the bare provider with explicit `snapshot_now()`
/// as the only trigger. Serialization flows through `core::EnumTraits` and
/// `core::mandate`: constexpr `to_string`/`from_string`, non-throwing
/// `to_json`/`from_json`, and the shared generic `std::formatter`.
///
/// @note Spec `chrono/chrono-settings`, requirement "Provider kind setting";
///       requirement "ProviderKind serializes through the standard conversion
///       set"; project mandates 9–11.
enum class ProviderKind : std::uint8_t {
    daemon, ///< A `ChronoWorker` delivers periodic snapshots.
    manual, ///< No worker; explicit `snapshot_now()` only.
};

} // namespace nexigen::chrono

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table wiring `ProviderKind` into the shared enum engine.
///
/// The rows are the canonical file spellings merged by the chrono settings
/// loader; the table is static constexpr storage with no allocation.
///
/// @note Spec `chrono/chrono-settings`, requirement "Provider kind setting";
///       project mandates 9 and 10.
template <> struct EnumTraits<nexigen::chrono::ProviderKind> {
    /// @brief One `ProviderKind` to canonical-name row.
    struct Entry {
        nexigen::chrono::ProviderKind value; ///< Enumerator described by this row.
        std::string_view name;               ///< Canonical wire spelling.
    };
    /// @brief Append-only rows in enumerator order.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::chrono::ProviderKind::daemon, "daemon"},
        {nexigen::chrono::ProviderKind::manual, "manual"},
    }};
};

} // namespace nexigen::core

namespace nexigen::chrono {

/// @brief Canonical name of a provider kind.
///
/// Allocation-free and `constexpr`; an out-of-table garbage value renders the
/// engine fallback `"Unknown"`.
///
/// @param kind Provider kind to name.
/// @return Static-storage view of the canonical spelling.
/// @note Spec `chrono/chrono-settings`, requirement "ProviderKind serializes
///       through the standard conversion set"; project mandate 9.
[[nodiscard]] constexpr std::string_view to_string(ProviderKind kind) noexcept {
    return core::enum_name(kind);
}

/// @brief Parse a canonical provider-kind name.
///
/// Exact, case-sensitive match; any other spelling returns `false` and leaves
/// `out` unchanged. The settings loader reports that miss as a diagnostic and
/// keeps the previous merged kind.
///
/// @param name Candidate canonical spelling.
/// @param out Destination enumerator, untouched on a miss.
/// @return `true` when `name` matched a row and `out` was assigned.
/// @note Spec `chrono/chrono-settings`, requirement "Provider kind setting";
///       requirement "ProviderKind serializes through the standard conversion
///       set"; project mandate 9.
[[nodiscard]] constexpr bool from_string(std::string_view name, ProviderKind& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for `ProviderKind` (cold path only).
///
/// The value is exactly `to_string(kind)`; used by settings serialization,
/// never by a delivery path.
///
/// @param j Destination JSON value, replaced by the canonical name string.
/// @param kind Provider kind to serialize.
/// @note Spec `chrono/chrono-settings`, requirement "ProviderKind serializes
///       through the standard conversion set"; project mandate 10.
inline void to_json(nlohmann::json& j, const ProviderKind kind) {
    core::mandate::enum_to_json(j, kind);
}

/// @brief ADL JSON reader for `ProviderKind`, strictly non-throwing.
///
/// Under `JSON_NOEXCEPTION=1` a non-string value or an unknown name leaves
/// `out` unchanged instead of terminating.
///
/// @param j Source JSON value.
/// @param out Destination enumerator, untouched unless a known name parses.
/// @note Spec `chrono/chrono-settings`, requirement "ProviderKind serializes
///       through the standard conversion set"; project mandate 10.
inline void from_json(const nlohmann::json& j, ProviderKind& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::chrono
