// nexigen::core — diagnostics consumer concept (change
// add-telemetry-diagnostics; spec core/diagnostics).
//
// Deliberately narrow: a diagnostics logger is not the application Logger and
// carries no format wrapper, so chrono (L1) and the telemetry providers can
// accept it without depending on the logging module.
#pragma once

#include <concepts>

#include <nexigen/core/diagnostic_event.hpp>

namespace nexigen::core::concepts {

/// @brief A type that accepts one diagnostic event without throwing.
///
/// @tparam T Candidate consumer type.
/// @note Spec `core/diagnostics`, requirement "Diagnostic binding and logger
///       concept".
template <class T>
concept DiagnosticLogger = requires(T& logger, const DiagnosticEvent& event) {
    { logger.diagnostic(event) } noexcept;
};

} // namespace nexigen::core::concepts
