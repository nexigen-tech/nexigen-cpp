// nexigen::canopen — CiA 306 parser outcome.
#pragma once

#include <format>
#include <memory>
#include <string>
#include <vector>

#include <nexigen/canopen/eds_diagnostic.hpp>
#include <nexigen/canopen/eds_dictionary.hpp>

namespace nexigen::canopen {

/// @brief Parser outcome.
struct EdsParseResult {
    std::unique_ptr<EdsDictionary> dictionary{}; ///< Parsed dictionary, null on failure.
    std::vector<EdsDiagnostic> diagnostics{};    ///< Line diagnostics.

    /// @brief Whether a dictionary was produced.
    /// @return `true` when parsing succeeded.
    [[nodiscard]] bool ok() const noexcept { return dictionary != nullptr; }

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line parse summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("EdsParseResult(ok={}, diagnostics={})", ok(), diagnostics.size());
    }
};

/// @brief Free helper mirroring `EdsParseResult::to_string`.
[[nodiscard]] inline std::string to_string(const EdsParseResult& result) {
    return result.to_string();
}

} // namespace nexigen::canopen
