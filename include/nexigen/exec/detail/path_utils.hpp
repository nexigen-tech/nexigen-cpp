// nexigen::exec — path, identifier, and join helpers shared by execution_manager TUs
// (change refactor-execution_manager-structure).
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <unistd.h>

namespace nexigen::exec::detail {

/// @brief Maximum accepted path length.
///
/// The logging settings loader caps `log_root_directory` at 511 bytes, so
/// instance and artifact paths must fit.
///
/// @note Spec `exec/instance-lifecycle`, requirement "Instance directory layout
///       is deterministic".
inline constexpr std::size_t max_path_bytes = 511;

/// @brief Whether a path is absolute (starts with `/`).
///
/// @param path Path to test.
/// @return `true` for a non-empty path whose first character is `/`.
[[nodiscard]] inline bool absolute_path(std::string_view path) noexcept {
    return !path.empty() && path.front() == '/';
}

/// @brief Identifier alphabet: ASCII alphanumerics plus `.`, `_`, and `-`.
///
/// @param c Character to test.
/// @return `true` when `c` is allowed in a suite, application, or instance id.
[[nodiscard]] constexpr bool is_allowed_id_char(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '_' || c == '-';
}

/// @brief Whether an identifier is safe to use as one path component.
///
/// Rejects empty names, names over 128 bytes, `.`, `..`, any embedded `..`,
/// and characters outside `is_allowed_id_char`.
///
/// @param id Identifier to check.
/// @return `true` when the identifier is a single safe path component.
/// @note Spec `exec/app-suite`, scenario "Unsafe identity or relative run directory is rejected".
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

/// @brief Whether a path contains a `..` component.
///
/// @param path Path to scan.
/// @return `true` when any slash-delimited segment is exactly `..`.
/// @note Spec `exec/app-suite`, scenario "Invalid settings paths are rejected".
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

/// @brief Current working directory of the calling process.
///
/// @return The absolute working directory, or an empty string when `getcwd`
///         fails.
[[nodiscard]] inline std::string current_directory() {
    char buffer[4096]{};
    if (::getcwd(buffer, sizeof(buffer)) == nullptr) {
        return {};
    }
    return std::string(buffer);
}

/// @brief Make a path absolute relative to the current working directory.
///
/// An already-absolute path is returned unchanged; a `getcwd` failure also
/// returns the input unchanged rather than fabricating a root.
///
/// @param path Path to resolve.
/// @return The resolved absolute path, or the input when it cannot be resolved.
/// @note Spec `exec/app-suite`, requirement "Suite manifest has a fixed
///       schema" (absolute `run_dir`/`artifacts_path`).
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

/// @brief Directory part of a path.
///
/// @param path Path to split.
/// @return `.` when there is no slash, `/` for a root-level file, otherwise the
///         substring before the last slash.
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

/// @brief Join two path components with exactly one slash.
///
/// @param left Left component; not normalized.
/// @param right Right component; not normalized.
/// @return `left + "/" + right`.
[[nodiscard]] inline std::string join_path(std::string_view left, std::string_view right) {
    return std::string(left) + "/" + std::string(right);
}

/// @brief Deterministic instance control directory.
///
/// @param run_dir Absolute run directory.
/// @param app Application name.
/// @param instance Instance id.
/// @return `<run_dir>/<app>/<instance>`.
/// @note Spec `exec/instance-lifecycle`, requirement "Instance directory layout
///       is deterministic".
[[nodiscard]] inline std::string instance_dir_path(std::string_view run_dir, std::string_view app,
                                                   std::string_view instance) {
    return std::string(run_dir) + "/" + std::string(app) + "/" + std::string(instance);
}

} // namespace nexigen::exec::detail
