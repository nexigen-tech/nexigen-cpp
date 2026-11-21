// nexigen::core — the default diagnostics consumer (change
// add-telemetry-diagnostics; spec core/diagnostics).
//
// Satisfies concepts::DiagnosticLogger and does nothing, so tests and
// uninstrumented components can omit the channel without a null check.
#pragma once

#include <string>

#include <nexigen/core/concepts/diagnostic_logger_concept.hpp>
#include <nexigen/core/diagnostic_event.hpp>

namespace nexigen::core {

/// @brief No-op diagnostics consumer.
///
/// @note Spec `core/diagnostics`, requirement "Diagnostic binding and logger
///       concept".
struct NullDiagnosticLogger {
    /// @brief Discard one event.
    ///
    /// @param event Ignored.
    void diagnostic(const DiagnosticEvent& event) noexcept { static_cast<void>(event); }

    /// @brief Render the null consumer for cold-path diagnostics.
    ///
    /// @return The literal `"NullDiagnosticLogger"`.
    /// @note Mandate 9.
    [[nodiscard]] std::string to_string() const { return "NullDiagnosticLogger"; }
};

static_assert(concepts::DiagnosticLogger<NullDiagnosticLogger>);

} // namespace nexigen::core
