// nexigen::runner — atomic JSON file I/O for instance metadata
// (change refactor-runner-structure; spec runner/instance-lifecycle).
#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>

namespace nexigen::runner {

/// Write a JSON document atomically (temporary file in the same directory,
/// then rename). Returns SystemError when any step fails.
[[nodiscard]] core::Status write_json_file(std::string_view path,
                                           const nlohmann::json& document) noexcept;

/// Read and parse a JSON document. Returns SystemError when the file cannot
/// be read and InvalidArgument when it is not valid JSON.
[[nodiscard]] core::Result<nlohmann::json> read_json_file(std::string_view path) noexcept;

/// Read and parse one JSON document into `out`. Returns false when the file
/// cannot be read or parsed; `out` is reset before parsing.
template <class T> [[nodiscard]] bool read_json_into(std::string_view path, T& out) noexcept {
    const core::Result<nlohmann::json> document = read_json_file(path);
    if (!document) {
        return false;
    }
    out = T{};
    from_json(*document, out);
    return true;
}

} // namespace nexigen::runner
