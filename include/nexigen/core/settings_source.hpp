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

/// Which source produced a value or a diagnostic.
enum class SettingsSource : std::uint8_t {
    Global,      ///< Global JSON file.
    Local,       ///< Local JSON file.
    Environment, ///< Environment override.
    Computed,    ///< Derived check (capacity limits).
};

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
template <> struct EnumTraits<SettingsSource> {
    struct Entry {
        SettingsSource value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 4> entries{{
        {SettingsSource::Global, "global"},
        {SettingsSource::Local, "local"},
        {SettingsSource::Environment, "environment"},
        {SettingsSource::Computed, "computed"},
    }};
};

/// Compile-time name of a settings source. Static-storage view, no allocation.
[[nodiscard]] constexpr std::string_view to_string(SettingsSource src) noexcept {
    return enum_name(src);
}

/// Parse a canonical source name (the exact to_string spelling). Returns false
/// leaving the out-parameter UNCHANGED on any other spelling.
[[nodiscard]] constexpr bool from_string(std::string_view name, SettingsSource& out) noexcept {
    return enum_value(name, out);
}

/// ADL JSON writer (cold path only): the value is exactly to_string(src).
inline void to_json(nlohmann::json& j, const SettingsSource src) {
    mandate::enum_to_json(j, src);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1: a non-string
/// value or an unknown name leaves `out` unchanged. Never aborts regardless of
/// input.
inline void from_json(const nlohmann::json& j, SettingsSource& out) noexcept {
    mandate::enum_from_json(j, out);
}

} // namespace nexigen::core
