// nexigen::exec — atomic JSON file I/O for instance metadata
// (change refactor-execution_manager-structure; spec execution_manager/instance-lifecycle).
#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>

namespace nexigen::exec {

/// @brief Write a JSON document atomically.
///
/// A temporary file is written in the same directory and renamed over the
/// destination, so a concurrent reader observes either the previous complete
/// document or the new one.
///
/// @param path Destination file path.
/// @param document Document to serialize.
/// @return Success or `SystemError` when opening, writing, or renaming fails.
/// @note Spec `exec/instance-lifecycle`, scenario "Metadata writes are atomic" (`exec/app-suite`).
/// @warning Cold path only; performs blocking file I/O.
[[nodiscard]] core::Status write_json_file(std::string_view path,
                                           const nlohmann::json& document) noexcept;

/// @brief Read and parse a JSON document.
///
/// @param path Source file path.
/// @return The parsed document on success; `SystemError` when the file cannot
///         be read and `InvalidArgument` when it is not valid JSON.
/// @note Spec `exec/instance-lifecycle`, requirement "Instance directory layout
///       is deterministic".
[[nodiscard]] core::Result<nlohmann::json> read_json_file(std::string_view path) noexcept;

/// @brief Read and parse one JSON document into `out`.
///
/// The target is reset before parsing, so a failed read leaves a default
/// value rather than a partially merged one.
///
/// @tparam T Destination type; must provide an ADL `from_json` overload.
/// @param path Source file path.
/// @param out Receives the parsed value.
/// @return `true` on success; `false` when the file cannot be read or parsed.
/// @note Spec `exec/instance-lifecycle`, scenario "Hostile JSON leaves
///       metadata unchanged"; project mandate 10.
template <class T> [[nodiscard]] bool read_json_into(std::string_view path, T& out) noexcept {
    const core::Result<nlohmann::json> document = read_json_file(path);
    if (!document) {
        return false;
    }
    out = T{};
    from_json(*document, out);
    return true;
}

} // namespace nexigen::exec
