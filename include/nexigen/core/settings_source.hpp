// nexigen::core — which source produced a value or a diagnostic (spec
// core/settings-vocabulary).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::core {

/// @brief Which merge layer produced a value or a diagnostic.
///
/// Global and local name the two explicit JSON files, Environment an override
/// variable, and Computed a derived check such as a capacity limit. Cold path
/// only.
///
/// @note Spec `core/settings-vocabulary`, requirement "Settings source
///       enumerates the merge layers".
enum class SettingsSource : std::uint8_t {
    Global,      ///< Global JSON file.
    Local,       ///< Local JSON file.
    Environment, ///< Environment override.
    Computed,    ///< Derived check (capacity limits).
};

/// @brief Vocabulary table for `SettingsSource`
///        (`core::enum_name`/`enum_value`).
///
/// The single source of truth for the canonical lowercase layer names.
template <> struct EnumTraits<SettingsSource> {
    /// @brief One table row: a merge layer and its canonical spelling.
    struct Entry {
        SettingsSource value;  ///< Layer described by the row.
        std::string_view name; ///< Canonical lowercase wire name.
    };
    /// @brief Name table covering every enumerator in declaration order.
    static constexpr std::array<Entry, 4> entries{{
        {SettingsSource::Global, "global"},
        {SettingsSource::Local, "local"},
        {SettingsSource::Environment, "environment"},
        {SettingsSource::Computed, "computed"},
    }};
};

/// @brief Compile-time name of a settings source.
///
/// @param src Merge layer to render.
/// @return Static-storage view of the canonical lowercase name; out-of-table
///         values render `"Unknown"`.
/// @note Spec `core/settings-vocabulary`, requirement "Settings source
///       enumerates the merge layers"; mandate 9. Allocation-free.
[[nodiscard]] constexpr std::string_view to_string(SettingsSource src) noexcept {
    return enum_name(src);
}

/// @brief Parse a canonical source name.
///
/// @param name Candidate spelling (the exact `to_string` output).
/// @param out Destination, untouched on any other spelling.
/// @return `true` with the enumerator on an exact, case-sensitive match;
///         `false` otherwise.
/// @note Spec `core/settings-vocabulary`, requirement "Settings source
///       enumerates the merge layers".
[[nodiscard]] constexpr bool from_string(std::string_view name, SettingsSource& out) noexcept {
    return enum_value(name, out);
}

/// @brief ADL JSON writer: the value is exactly `to_string(src)`.
///
/// @param j JSON value overwritten with the lowercase layer name.
/// @param src Merge layer to serialize.
/// @note Spec `core/settings-vocabulary`, requirement "Settings source
///       enumerates the merge layers"; mandate 10. Cold path only.
inline void to_json(nlohmann::json& j, const SettingsSource src) {
    mandate::enum_to_json(j, src);
}

/// @brief ADL JSON reader, strictly non-throwing under `JSON_NOEXCEPTION=1`.
///
/// A non-string value or an unknown name leaves `out` unchanged; never aborts
/// regardless of input.
///
/// @param j JSON value to read.
/// @param out Destination, untouched on rejection.
/// @note Spec `core/settings-vocabulary`, requirement "Settings source
///       enumerates the merge layers"; mandate 10.
inline void from_json(const nlohmann::json& j, SettingsSource& out) noexcept {
    mandate::enum_from_json(j, out);
}

} // namespace nexigen::core
