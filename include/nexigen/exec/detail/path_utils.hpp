// nexigen::exec — path, identifier, and join helpers shared by execution_manager TUs
// (change refactor-execution_manager-structure).
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <unistd.h>

namespace nexigen::exec::detail {

/// Maximum accepted path length. The logging settings loader caps
/// `log_root_directory` at 511 bytes, so instance/artifact paths must fit.
inline constexpr std::size_t max_path_bytes = 511;

[[nodiscard]] inline bool absolute_path(std::string_view path) noexcept {
    return !path.empty() && path.front() == '/';
}

/// Identifier alphabet: ASCII alphanumerics plus '.', '_' and '-'.
[[nodiscard]] constexpr bool is_allowed_id_char(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '_' || c == '-';
}

[[nodiscard]] inline bool safe_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 128U) {
        return false;
    }
    if (id == "." || id == ".." || id.find("..") != std::string_view::npos) {
        return false;
    }
    for (const char c : id) {
        if (!is_allowed_id_char(c)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool has_parent_segment(std::string_view path) noexcept {
    for (std::size_t pos = 0; pos < path.size();) {
        const std::size_t slash = path.find('/', pos);
        const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
        if (path.substr(pos, end - pos) == "..") {
            return true;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        pos = slash + 1;
    }
    return false;
}

[[nodiscard]] inline std::string current_directory() {
    char buffer[4096]{};
    if (::getcwd(buffer, sizeof(buffer)) == nullptr) {
        return {};
    }
    return std::string(buffer);
}

[[nodiscard]] inline std::string absolute_from_cwd(std::string_view path) {
    if (absolute_path(path)) {
        return std::string(path);
    }
    const std::string base = current_directory();
    if (base.empty()) {
        return std::string(path);
    }
    return base + "/" + std::string(path);
}

[[nodiscard]] inline std::string directory_of(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos) {
        return std::string(".");
    }
    if (slash == 0) {
        return std::string("/");
    }
    return std::string(path.substr(0, slash));
}

[[nodiscard]] inline std::string join_path(std::string_view left, std::string_view right) {
    return std::string(left) + "/" + std::string(right);
}

/// `<run_dir>/<app>/<instance>` — the suite's deterministic instance path.
[[nodiscard]] inline std::string instance_dir_path(std::string_view run_dir, std::string_view app,
                                                   std::string_view instance) {
    return std::string(run_dir) + "/" + std::string(app) + "/" + std::string(instance);
}

} // namespace nexigen::exec::detail
