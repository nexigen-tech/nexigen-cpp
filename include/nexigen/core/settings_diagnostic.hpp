// nexigen::core — one best-effort settings finding (spec
// core/settings-vocabulary).
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/settings_source.hpp>

namespace nexigen::core {

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

/// ADL JSON writer for a diagnostic (cold path only).
inline void to_json(nlohmann::json& j, const SettingsDiagnostic& diag) {
    j = nlohmann::json{{"source", std::string(to_string(diag.source))},
                       {"field", diag.field},
                       {"message", diag.message}};
}

/// ADL JSON reader, non-throwing under JSON_NOEXCEPTION=1: non-object values
/// and mistyped fields leave the target unchanged.
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