// nexigen::core — which source produced a value or a diagnostic (spec
// core/settings-vocabulary).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::core {

/// Which source produced a value or a diagnostic.
enum class SettingsSource : std::uint8_t {
    Global,      ///< Global JSON file.
    Local,       ///< Local JSON file.
    Environment, ///< Environment override.
    Computed,    ///< Derived check (capacity limits).
};

/// Compile-time name of a settings source. Static-storage view, no allocation.
[[nodiscard]] constexpr std::string_view to_string(SettingsSource src) noexcept {
    switch (src) {
        case SettingsSource::Global:      return "global";
        case SettingsSource::Local:       return "local";
        case SettingsSource::Environment: return "environment";
        case SettingsSource::Computed:    return "computed";
        default:                          return "Unknown";
    }
}

/// Parse a canonical source name (the exact to_string spelling). Returns false
/// leaving the out-parameter UNCHANGED on any other spelling.
[[nodiscard]] constexpr bool from_string(std::string_view name, SettingsSource& out) noexcept {
    if (name == "global") {
        out = SettingsSource::Global;
        return true;
    }
    if (name == "local") {
        out = SettingsSource::Local;
        return true;
    }
    if (name == "environment") {
        out = SettingsSource::Environment;
        return true;
    }
    if (name == "computed") {
        out = SettingsSource::Computed;
        return true;
    }
    return false;
}

/// ADL JSON writer (cold path only): the value is exactly to_string(src).
inline void to_json(nlohmann::json& j, const SettingsSource src) {
    j = std::string(to_string(src));
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1: a non-string
/// value or an unknown name leaves `out` unchanged. Never aborts regardless of
/// input.
inline void from_json(const nlohmann::json& j, SettingsSource& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

} // namespace nexigen::core