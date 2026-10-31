// nexigen::chrono — global/local chrono settings loader (change
// add-chrono-clock-service; spec chrono/chrono-settings).
//
// COLD PATH ONLY. `load_chrono_settings` runs once before real-time
// execution: it reads an explicitly supplied global JSON file, then an
// explicitly supplied local JSON file, then applies environment overrides,
// merging field by field into a ChronoProvider_config. Every problem becomes a
// Chrono_settings_diagnostic; a bad value leaves the previous/default value in
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
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// Which source produced a value or a diagnostic.
enum class Chrono_settings_source : std::uint8_t {
    Global,      ///< Global JSON file.
    Local,       ///< Local JSON file.
    Environment, ///< NEXIGEN_CHRONO_* override.
    Computed,    ///< Derived check.
};

[[nodiscard]] constexpr std::string_view to_string(Chrono_settings_source src) noexcept {
    switch (src) {
        case Chrono_settings_source::Global:      return "global";
        case Chrono_settings_source::Local:       return "local";
        case Chrono_settings_source::Environment: return "environment";
        case Chrono_settings_source::Computed:    return "computed";
        default:                                  return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         Chrono_settings_source& out) noexcept {
    if (name == "global")      { out = Chrono_settings_source::Global; return true; }
    if (name == "local")       { out = Chrono_settings_source::Local; return true; }
    if (name == "environment") { out = Chrono_settings_source::Environment; return true; }
    if (name == "computed")    { out = Chrono_settings_source::Computed; return true; }
    return false;
}

inline void to_json(nlohmann::json& j, const Chrono_settings_source src) {
    j = std::string(to_string(src));
}

inline void from_json(const nlohmann::json& j, Chrono_settings_source& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// Explicit source paths. An empty path disables that source; the loader never
/// consults implicit locations (/etc, XDG, executable directory).
struct Chrono_settings_paths {
    std::string global_file{};
    std::string local_file{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Chrono_settings_paths& paths) {
    return paths.to_string();
}

/// One best-effort finding: which source, which field, what went wrong.
struct Chrono_settings_diagnostic {
    Chrono_settings_source source{Chrono_settings_source::Global};
    std::string field{};
    std::string message{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Chrono_settings_diagnostic& diag) {
    return diag.to_string();
}

/// Merged result of one load. `global_loaded`/`local_loaded` report file
/// presence, not success; `diagnostics` carries every non-fatal problem.
struct Chrono_settings {
    ChronoProvider_config options{};
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<Chrono_settings_diagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Chrono_settings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (bodies in src/chrono/chrono_settings.cpp)
void to_json(nlohmann::json& j, const Chrono_settings_paths& paths);
void from_json(const nlohmann::json& j, Chrono_settings_paths& out) noexcept;

void to_json(nlohmann::json& j, const Chrono_settings_diagnostic& diag);
void from_json(const nlohmann::json& j, Chrono_settings_diagnostic& out) noexcept;

void to_json(nlohmann::json& j, const Chrono_settings& settings);
// Deliberately no from_json(Chrono_settings&): reconstructed by the loader.

/// Read, merge and validate settings. `out` is always populated best-effort.
/// Returns Ok when no diagnostic was recorded, InvalidArgument when a value /
/// type / JSON problem was recorded, and SystemError when a supplied file
/// exists but cannot be read. Missing files are not errors.
[[nodiscard]] core::Status load_chrono_settings(const Chrono_settings_paths& paths,
                                                Chrono_settings& out) noexcept;

/// Map merged settings into a provider construction config.
[[nodiscard]] core::Status apply_to(const Chrono_settings& settings,
                                    ChronoProvider_config& out) noexcept;

} // namespace nexigen::chrono

template <>
struct std::formatter<nexigen::chrono::Chrono_settings_source>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::chrono::Chrono_settings_source src, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(
            nexigen::chrono::to_string(src), ctx);
    }
};

template <>
struct std::formatter<nexigen::chrono::Chrono_settings_paths>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::chrono::Chrono_settings_paths& paths, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(paths.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::chrono::Chrono_settings_diagnostic>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::chrono::Chrono_settings_diagnostic& diag, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(diag.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::chrono::Chrono_settings>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::chrono::Chrono_settings& settings, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(settings.to_string(), ctx);
    }
};
