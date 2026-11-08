// nexigen::core — explicit source paths for settings loading (spec
// core/settings-vocabulary).
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::core {

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
    const auto gf_it = j.find("global_file");
    if (gf_it != j.end()) {
        const auto* const gf = gf_it->get_ptr<const nlohmann::json::string_t*>();
        if (gf != nullptr) {
            out.global_file = *gf;
        }
    }
    const auto lf_it = j.find("local_file");
    if (lf_it != j.end()) {
        const auto* const lf = lf_it->get_ptr<const nlohmann::json::string_t*>();
        if (lf != nullptr) {
            out.local_file = *lf;
        }
    }
}

} // namespace nexigen::core