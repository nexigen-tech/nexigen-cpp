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

/// How the clock authority is driven.
enum class ProviderKind : std::uint8_t {
    daemon, ///< a ChronoWorker delivers periodic snapshots
    manual, ///< no worker; explicit snapshot_now() only
};

} // namespace nexigen::chrono

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::chrono::ProviderKind> {
    struct Entry {
        nexigen::chrono::ProviderKind value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::chrono::ProviderKind::daemon, "daemon"},
        {nexigen::chrono::ProviderKind::manual, "manual"},
    }};
};

} // namespace nexigen::core

namespace nexigen::chrono {

/// Canonical name of a kind; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(ProviderKind kind) noexcept {
    return core::enum_name(kind);
}

/// Parse a canonical ProviderKind name; false leaves the out-parameter
/// UNCHANGED on any other spelling.
[[nodiscard]] constexpr bool from_string(std::string_view name, ProviderKind& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the value is exactly to_string(kind).
inline void to_json(nlohmann::json& j, const ProviderKind kind) {
    core::mandate::enum_to_json(j, kind);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, ProviderKind& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::chrono
