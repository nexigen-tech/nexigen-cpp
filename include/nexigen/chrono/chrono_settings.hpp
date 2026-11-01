// nexigen::chrono — global/local chrono settings loader (change
// add-chrono-clock-service; spec chrono/chrono-settings).
//
// COLD PATH ONLY. `load_chrono_settings` runs once before real-time
// execution: it reads an explicitly supplied global JSON file, then an
// explicitly supplied local JSON file, then applies environment overrides,
// merging field by field into a ChronoProviderConfig. Every problem becomes a
// SettingsDiagnostic; a bad value leaves the previous/default value in
// place so the caller can still initialize a provider.
//
// Module independence: chrono defines its own path/diagnostic vocabulary
// (mirroring nexigen::logging's) rather than depending on the logging module,
// whose dependency edge already points at chrono.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/chrono_provider.hpp>
#include <nexigen/chrono/json_file_chrono_sink.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// Which source produced a value or a diagnostic.
enum class SettingsSource : std::uint8_t {
    Global,      ///< Global JSON file.
    Local,       ///< Local JSON file.
    Environment, ///< NEXIGEN_CHRONO_* override.
    Computed,    ///< Derived check.
};

[[nodiscard]] constexpr std::string_view to_string(SettingsSource src) noexcept {
    switch (src) {
        case SettingsSource::Global:      return "global";
        case SettingsSource::Local:       return "local";
        case SettingsSource::Environment: return "environment";
        case SettingsSource::Computed:    return "computed";
        default:                                  return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         SettingsSource& out) noexcept {
    if (name == "global")      { out = SettingsSource::Global; return true; }
    if (name == "local")       { out = SettingsSource::Local; return true; }
    if (name == "environment") { out = SettingsSource::Environment; return true; }
    if (name == "computed")    { out = SettingsSource::Computed; return true; }
    return false;
}

inline void to_json(nlohmann::json& j, const SettingsSource src) {
    j = std::string(to_string(src));
}

inline void from_json(const nlohmann::json& j, SettingsSource& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// Explicit source paths. An empty path disables that source; the loader never
/// consults implicit locations (/etc, XDG, executable directory).
struct SettingsPaths {
    std::string global_file{};
    std::string local_file{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const SettingsPaths& paths) {
    return paths.to_string();
}

/// One best-effort finding: which source, which field, what went wrong.
struct SettingsDiagnostic {
    SettingsSource source{SettingsSource::Global};
    std::string field{};
    std::string message{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const SettingsDiagnostic& diag) {
    return diag.to_string();
}

/// Merged result of one load. `global_loaded`/`local_loaded` report file
/// presence, not success; `diagnostics` carries every non-fatal problem.
struct Settings {
    ChronoProviderConfig options{};             ///< Provider configuration.
    JsonFileChronoSinkConfig sink{};            ///< File-sink path and append flag.
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<SettingsDiagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Settings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (bodies in src/chrono/chrono_settings.cpp)
void to_json(nlohmann::json& j, const SettingsPaths& paths);
void from_json(const nlohmann::json& j, SettingsPaths& out) noexcept;

void to_json(nlohmann::json& j, const SettingsDiagnostic& diag);
void from_json(const nlohmann::json& j, SettingsDiagnostic& out) noexcept;

void to_json(nlohmann::json& j, const Settings& settings);
// Deliberately no from_json(Settings&): reconstructed by the loader.

/// Read, merge and validate settings. `out` is always populated best-effort.
/// Returns Ok when no diagnostic was recorded, InvalidArgument when a value /
/// type / JSON problem was recorded, and SystemError when a supplied file
/// exists but cannot be read. Missing files are not errors.
[[nodiscard]] core::Status load_chrono_settings(const SettingsPaths& paths,
                                                Settings& out) noexcept;

/// Map merged settings into a provider construction config.
[[nodiscard]] core::Status apply_to(const Settings& settings,
                                    ChronoProviderConfig& out) noexcept;

} // namespace nexigen::chrono

template <>
struct std::formatter<nexigen::chrono::SettingsSource>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::chrono::SettingsSource src, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(
            nexigen::chrono::to_string(src), ctx);
    }
};

template <>
struct std::formatter<nexigen::chrono::SettingsPaths>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::chrono::SettingsPaths& paths, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(paths.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::chrono::SettingsDiagnostic>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::chrono::SettingsDiagnostic& diag, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(diag.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::chrono::Settings>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::chrono::Settings& settings, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(settings.to_string(), ctx);
    }
};
