// nexigen::canopen — one EDS parser diagnostic.
#pragma once

#include <cstddef>
#include <string>

namespace nexigen::canopen {

/// @brief One parser diagnostic tied to a source line.
struct EdsDiagnostic {
    std::size_t line{0};   ///< 1-based line, or zero for file-level errors.
    std::string message{}; ///< Human-readable reason.

    /// @brief Cold-path rendering.
    /// @return `line N: message`.
    [[nodiscard]] std::string to_string() const;
};

} // namespace nexigen::canopen
