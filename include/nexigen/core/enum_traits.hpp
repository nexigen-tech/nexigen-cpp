// nexigen::core — generic enum vocabulary engine (change
// refactor-enum-traits-helper; mandates 9-11 carry the OUTCOMES, this header
// carries their cost once instead of per enum).
//
// Every enum class keeps its full serialization contract: constexpr
// `to_string`/`from_string` in its own namespace (ADL requires the enum's
// immediate namespace — see serialization_mandate.hpp), non-throwing
// `to_json`/`from_json`, and a `std::formatter` through the generic
// MandateFormatter. What collapses here is the per-enum BODY: the name/value
// table lives in a `core::EnumTraits<E>` specialization (in the enum's own
// header, beside the enumerators) and the four entry points become one-line
// delegations. Byte-identical wire: same vocabulary strings, same case,
// same "Unknown" fallback for values outside the table, same
// "leave `out` untouched on unknown name" parse semantics.
//
// A type opts out of the engine by simply keeping its hand-written bodies —
// existing per-type definitions outrank any template (non-template beats
// template), the documented escape hatch of the mandate machinery.
//
// Cold path only (mandates 4/12): initialization and diagnostics serialize
// enums; no producer, tick, harvest or sink hot path ever reaches here. The
// tables are static constexpr storage and lookup is a constexpr scan over a
// handful of rows — the same cost class as the switch it replaces.
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace nexigen::core {

/// Vocabulary table for enum `E`: specialize with `Entry` + `entries` in the
/// enum's own header. The primary template intentionally fails: an enum
/// reaching the engine without a specialization is a programming error.
template <class E> struct EnumTraits {
    static_assert(sizeof(E) == 0,
                  "nexigen::core::EnumTraits<E> requires a specialization with Entry + entries");
};

/// Canonical name of `value`; out-of-table values render the fallback: the
/// specialization's optional `fallback` member, else "Unknown" (mirrors the
/// legacy `default:` arm of the per-enum switches).
template <class E>
    requires std::is_enum_v<E>
[[nodiscard]] constexpr std::string_view enum_name(E value) noexcept {
    using Traits = EnumTraits<E>;
    for (const typename Traits::Entry& row : Traits::entries) {
        if (row.value == value) {
            return row.name;
        }
    }
    if constexpr (requires { Traits::fallback; }) {
        return Traits::fallback;
    } else {
        return "Unknown";
    }
}

/// Parse `name` into `out` on an exact, case-sensitive match; a miss leaves
/// `out` untouched and returns false (legacy `from_string` semantics).
template <class E>
    requires std::is_enum_v<E>
[[nodiscard]] constexpr bool enum_value(std::string_view name, E& out) noexcept {
    using Traits = EnumTraits<E>;
    for (const typename Traits::Entry& row : Traits::entries) {
        if (row.name == name) {
            out = row.value;
            return true;
        }
    }
    return false;
}

} // namespace nexigen::core

namespace nexigen::core::mandate {

/// Serialize an enum as its canonical name via ADL `to_string` (mandate 10).
/// Lifted from nexigen::exec::detail (refactor-enum-traits-helper): one
/// engine for every module; the enum's own header keeps the one-line ADL glue.
template <class Enum> void enum_to_json(nlohmann::json& j, Enum value) {
    j = std::string(to_string(value));
}

/// Parse an enum from its canonical name via ADL `from_string`; a missing,
/// non-string or unknown value leaves `value` unchanged (non-throwing under
/// JSON_NOEXCEPTION).
template <class Enum> void enum_from_json(const nlohmann::json& j, Enum& value) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), value));
    }
}

/// Read an enum field from a JSON object by key, delegating to
/// `enum_from_json`. A missing or wrong-typed key leaves `target` unchanged.
template <class Enum>
void read_enum(const nlohmann::json& document, const char* key, Enum& target) noexcept {
    if (!document.is_object()) {
        return;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_string()) {
        return;
    }
    const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
    if (name == nullptr) {
        return;
    }
    Enum parsed = target;
    enum_from_json(nlohmann::json(*name), parsed);
    target = parsed;
}

} // namespace nexigen::core::mandate
