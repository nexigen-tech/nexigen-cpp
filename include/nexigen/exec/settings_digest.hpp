// nexigen::exec — settings-file digest entry (change verify-settings-digests).
#pragma once

#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::exec {

/// @brief One verified settings file: absolute path plus expected SHA-256.
///
/// @note Spec `exec/app-suite`, requirement "Settings files are
///       digest-verified before launch".
struct SettingsDigest {
    std::string path{};   ///< Absolute settings-file path.
    std::string sha256{}; ///< Expected 64-character hex digest.

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("SettingsDigest(path={}, sha256={})", path, sha256);
    }
};

/// @brief Free helper mirroring `SettingsDigest::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const SettingsDigest& digest) {
    return digest.to_string();
}

/// @brief ADL JSON writer for one settings digest.
inline void to_json(nlohmann::json& j, const SettingsDigest& digest) {
    j = nlohmann::json{{"path", digest.path}, {"sha256", digest.sha256}};
}

/// @brief ADL JSON reader for one settings digest (non-throwing).
inline void from_json(const nlohmann::json& j, SettingsDigest& digest) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto path = j.find("path");
    if (path != j.end() && path->is_string()) {
        digest.path = path->get<std::string>();
    }
    const auto sha = j.find("sha256");
    if (sha != j.end() && sha->is_string()) {
        digest.sha256 = sha->get<std::string>();
    }
}

} // namespace nexigen::exec

/// @brief `std::formatter` for a settings digest (project mandate 11).
template <> struct std::formatter<nexigen::exec::SettingsDigest> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::exec::SettingsDigest& digest,
                                     Ctx& ctx) const {
        return std::formatter<std::string_view>::format(digest.to_string(), ctx);
    }
};
