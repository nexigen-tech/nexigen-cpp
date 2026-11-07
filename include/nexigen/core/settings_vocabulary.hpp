// nexigen::core — shared settings-loading vocabulary (change
// refactor-settings-into-subsystem-modules; spec core/settings-vocabulary).
//
// SettingsSource / SettingsPaths / SettingsDiagnostic are the cold-path
// vocabulary every subsystem settings loader uses to name configuration
// sources, carry explicit global/local file pairs, and report best-effort
// findings. They lived byte-for-byte in both nexigen::logging and
// nexigen::chrono; consolidating them here removes the duplication and the
// chrono->logging dependency it was avoiding.
//
// COLD PATH ONLY — these types allocate (std::string) and are used during
// initialization, diagnostics, and serialization. Never call them from a
// producer, tick, snapshot, or sink hot path.
#pragma once

#include <cstdint>
#include <format>
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

/// Explicit source paths. An empty path disables that source; the loader never
/// consults implicit locations (/etc, XDG, executable directory).
struct SettingsPaths {
    std::string global_file{};
    std::string local_file{};

    [[nodiscard]] std::string to_string() const {
        return "SettingsPaths(global_file=\"" + global_file + "\", local_file=\"" + local_file +
               "\")";
    }
};
[[nodiscard]] inline std::string to_string(const SettingsPaths& paths) {
    return paths.to_string();
}

/// One best-effort finding: which source, which field, what went wrong.
struct SettingsDiagnostic {
    SettingsSource source{SettingsSource::Global};
    std::string field{};
    std::string message{};

    [[nodiscard]] std::string to_string() const {
        return "SettingsDiagnostic(source=" + std::string(nexigen::core::to_string(source)) +
               ", field=" + field + ", message=" + message + ")";
    }
};
[[nodiscard]] inline std::string to_string(const SettingsDiagnostic& diag) {
    return diag.to_string();
}

/// ADL JSON writer for the global/local file pair (cold path only).
inline void to_json(nlohmann::json& j, const SettingsPaths& paths) {
    j = nlohmann::json{{"global_file", paths.global_file}, {"local_file", paths.local_file}};
}

/// ADL JSON reader for the pair, non-throwing under JSON_NOEXCEPTION=1: a
/// non-object value or a mistyped field leaves the target unchanged.
inline void from_json(const nlohmann::json& j, SettingsPaths& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    auto it = j.find("global_file");
    if (it != j.end() && it->is_string()) {
        out.global_file = it->get<std::string>();
    }
    it = j.find("local_file");
    if (it != j.end() && it->is_string()) {
        out.local_file = it->get<std::string>();
    }
}

/// ADL JSON writer for a diagnostic (cold path only).
inline void to_json(nlohmann::json& j, const SettingsDiagnostic& diag) {
    j = nlohmann::json{{"source", std::string(to_string(diag.source))},
                       {"field", diag.field},
                       {"message", diag.message}};
}

/// ADL JSON reader for a diagnostic, non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, SettingsDiagnostic& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    auto it = j.find("source");
    if (it != j.end()) {
        const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
        if (name != nullptr) {
            static_cast<void>(from_string(std::string_view(*name), out.source));
        }
    }
    it = j.find("field");
    if (it != j.end() && it->is_string()) {
        out.field = it->get<std::string>();
    }
    it = j.find("message");
    if (it != j.end() && it->is_string()) {
        out.message = it->get<std::string>();
    }
}

} // namespace nexigen::core
