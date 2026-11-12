// nexigen::core — one best-effort settings finding (spec
// core/settings-vocabulary).
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/settings_source.hpp>

namespace nexigen::core {

/// @brief One best-effort settings finding: source, field, and message.
///
/// Loaders append a finding instead of failing hard, so a malformed field keeps
/// the previous value while the operator still learns what was rejected. Cold
/// path only: constructing, reading, or serializing a diagnostic never happens
/// in a producer or worker loop.
///
/// @note Spec `core/settings-vocabulary`, requirement "Settings diagnostic
///       records a finding".
struct SettingsDiagnostic {
    SettingsSource source{SettingsSource::Global}; ///< Merge layer that found it.
    std::string field{};                           ///< Offending field name, if any.
    std::string message{};                         ///< Human-readable finding.

    /// @brief Render source, field, and message as one diagnostic line.
    ///
    /// @return Newly allocated `SettingsDiagnostic(source=..., field=...,
    ///         message=...)` text.
    /// @note Spec `core/settings-vocabulary`, requirement "Settings diagnostic
    ///       records a finding"; mandate 9.
    [[nodiscard]] std::string to_string() const {
        return "SettingsDiagnostic(source=" + std::string(nexigen::core::to_string(source)) +
               ", field=" + field + ", message=" + message + ")";
    }
};
/// @brief Free mirror of the member `to_string`.
///
/// @param diag Diagnostic to render.
/// @return The same text as `diag.to_string()`.
/// @note Spec `core/settings-vocabulary`, requirement "Settings diagnostic
///       records a finding"; mandate 9.
[[nodiscard]] inline std::string to_string(const SettingsDiagnostic& diag) {
    return diag.to_string();
}

/// @brief ADL JSON writer for a diagnostic: source, field, and message.
///
/// @param j JSON value overwritten with the diagnostic object.
/// @param diag Diagnostic to serialize.
/// @note Spec `core/settings-vocabulary`, requirement "Settings diagnostic
///       records a finding"; mandate 10. Cold path only.
inline void to_json(nlohmann::json& j, const SettingsDiagnostic& diag) {
    j = nlohmann::json{{"source", std::string(to_string(diag.source))},
                       {"field", diag.field},
                       {"message", diag.message}};
}

/// @brief ADL JSON reader, non-throwing under `JSON_NOEXCEPTION=1`.
///
/// A non-object value and any mistyped field leave the target unchanged.
///
/// @param j JSON value to read.
/// @param out Destination updated field by field only from valid strings.
/// @note Spec `core/settings-vocabulary`, requirement "Settings diagnostic
///       records a finding"; mandate 10.
inline void from_json(const nlohmann::json& j, SettingsDiagnostic& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto src_it = j.find("source");
    if (src_it != j.end()) {
        const auto* const src = src_it->get_ptr<const nlohmann::json::string_t*>();
        if (src != nullptr) {
            static_cast<void>(from_string(std::string_view(*src), out.source));
        }
    }
    const auto field_it = j.find("field");
    if (field_it != j.end()) {
        const auto* const field = field_it->get_ptr<const nlohmann::json::string_t*>();
        if (field != nullptr) {
            out.field = *field;
        }
    }
    const auto msg_it = j.find("message");
    if (msg_it != j.end()) {
        const auto* const message = msg_it->get_ptr<const nlohmann::json::string_t*>();
        if (message != nullptr) {
            out.message = *message;
        }
    }
}

} // namespace nexigen::core