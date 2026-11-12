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

/// @brief Vocabulary table for enum `E`: specialize with `Entry` + `entries`.
///
/// A specialization lives in the enum's own header, beside the enumerators, and
/// provides a nested `Entry` type plus a `static constexpr` `entries` range; an
/// optional `fallback` member overrides the `"Unknown"` rendering. The primary
/// template intentionally fails to compile: an enum reaching the engine without
/// a specialization is a programming error.
///
/// @tparam E Enum type described by the specialization.
/// @note Spec `core/serialization`, requirement "Universal enum string and JSON
///       vocabulary"; cold path only.
template <class E> struct EnumTraits {
    static_assert(sizeof(E) == 0,
                  "nexigen::core::EnumTraits<E> requires a specialization with Entry + entries");
};

/// @brief Canonical name of `value`, read from the `EnumTraits<E>` table.
///
/// @tparam E Enum type constrained by `std::is_enum_v<E>`.
/// @param value Enumerator to render.
/// @return Static-storage view of the matching row; an out-of-table value
///         renders the specialization's optional `fallback`, else `"Unknown"`.
/// @note Spec `core/serialization`, requirement "Universal enum string and JSON
///       vocabulary". `constexpr`, allocation-free and cold path only.
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

/// @brief Parse `name` into `out` on an exact, case-sensitive table match.
///
/// @tparam E Enum type constrained by `std::is_enum_v<E>`.
/// @param name Candidate vocabulary spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` when a row matched exactly and `out` was assigned; `false`
///         otherwise.
/// @note Spec `core/serialization`, requirement "Universal enum string and JSON
///       vocabulary". Misses leave the destination unchanged, matching the
///       legacy `from_string` semantics.
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

/// @brief Serialize an enum as its canonical name via ADL `to_string`.
///
/// @tparam Enum Enum type providing an ADL-visible `to_string(Enum)`.
/// @param j JSON value overwritten with the vocabulary string.
/// @param value Enumerator to serialize.
/// @note Spec `core/serialization`, requirement "Universal enum string and JSON
///       vocabulary"; mandate 10. Cold path only.
template <class Enum> void enum_to_json(nlohmann::json& j, Enum value) {
    j = std::string(to_string(value));
}

/// @brief Parse an enum from its canonical name via ADL `from_string`.
///
/// @tparam Enum Enum type providing an ADL-visible `from_string` taking a
///         `std::string_view` and the out-parameter.
/// @param j JSON value to read; a missing, non-string, or unknown value leaves
///        `value` unchanged.
/// @param value Destination, untouched unless the name parses.
/// @note Spec `core/serialization`, requirement "Non-throwing enum from_json
///       under -fno-exceptions"; mandate 10. Never aborts under
///       `JSON_NOEXCEPTION=1`.
template <class Enum> void enum_from_json(const nlohmann::json& j, Enum& value) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), value));
    }
}

/// @brief Read an enum field from a JSON object by key.
///
/// Delegates to `enum_from_json` only for a present string field, so a missing
/// or wrong-typed key leaves `target` unchanged.
///
/// @tparam Enum Enum type providing the ADL `from_string` vocabulary.
/// @param document JSON object to probe; a non-object is ignored.
/// @param key Field name to look up.
/// @param target Destination, untouched unless the field parses.
/// @note Spec `core/serialization`, requirement "Non-throwing enum from_json
///       under -fno-exceptions"; cold path only.
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
