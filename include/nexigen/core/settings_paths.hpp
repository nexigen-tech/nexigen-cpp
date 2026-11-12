// nexigen::core — explicit source paths for settings loading (spec
// core/settings-vocabulary).
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::core {

/// @brief Explicit source paths for settings loading.
///
/// An empty path disables that source; the loader never consults implicit
/// locations such as `/etc`, XDG directories, or the executable directory.
/// Cold-path configuration type only.
///
/// @note Spec `core/settings-vocabulary`, requirement "Settings paths carry an
///       explicit global/local pair".
struct SettingsPaths {
    std::string global_file{}; ///< Global JSON file; empty disables the layer.
    std::string local_file{};  ///< Local JSON file; empty disables the layer.

    /// @brief Render both file paths.
    ///
    /// @return Newly allocated `SettingsPaths(global_file="...",
    ///         local_file="...")` text.
    /// @note Spec `core/settings-vocabulary`, requirement "Settings paths carry
    ///       an explicit global/local pair"; mandate 9.
    [[nodiscard]] std::string to_string() const {
        return "SettingsPaths(global_file=\"" + global_file + "\", local_file=\"" + local_file +
               "\")";
    }
};
/// @brief Free mirror of the member `to_string`.
///
/// @param paths Paths to render.
/// @return The same text as `paths.to_string()`.
/// @note Spec `core/settings-vocabulary`, requirement "Settings paths carry an
///       explicit global/local pair"; mandate 9.
[[nodiscard]] inline std::string to_string(const SettingsPaths& paths) {
    return paths.to_string();
}

/// @brief ADL JSON writer for the global/local file pair.
///
/// @param j JSON value overwritten with `global_file`/`local_file`.
/// @param paths Paths to serialize.
/// @note Spec `core/settings-vocabulary`, requirement "Settings paths carry an
///       explicit global/local pair"; mandate 10. Cold path only.
inline void to_json(nlohmann::json& j, const SettingsPaths& paths) {
    j = nlohmann::json{{"global_file", paths.global_file}, {"local_file", paths.local_file}};
}

/// @brief ADL JSON reader for the pair, non-throwing under
///        `JSON_NOEXCEPTION=1`.
///
/// A non-object value or a mistyped field leaves the target unchanged.
///
/// @param j JSON value to read.
/// @param out Destination updated field by field only from valid strings.
/// @note Spec `core/settings-vocabulary`, requirement "Settings paths carry an
///       explicit global/local pair"; mandate 10.
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