// nexigen::core — borrowed diagnostics binding (change
// add-telemetry-diagnostics; spec core/diagnostics).
//
// Same type-erasure pattern as core::SinkBinding: plain function pointers, no
// virtual dispatch, no RTTI, no ownership. A null callback is a successful
// no-op.
#pragma once

#include <string>

#include <nexigen/core/diagnostic_event.hpp>

namespace nexigen::core {

/// @brief Borrowed diagnostics consumer with function-pointer dispatch.
///
/// The pointed-to logger MUST outlive the binding; the binding never owns it.
/// Bind before the producing worker starts (or on a control thread for
/// worker-less clients) so the 16-byte value is not read while being written.
///
/// @note Spec `core/diagnostics`, requirement "Diagnostic binding and logger
///       concept". Cold path only.
struct DiagnosticBinding {
    void* self{nullptr}; ///< Borrowed logger instance.
    /// @brief Deliver one event; null means "no diagnostics".
    void (*on_event)(void*, const DiagnosticEvent&) noexcept {nullptr};

    /// @brief Render the binding state for cold-path diagnostics.
    ///
    /// @return `DiagnosticBinding(bound=true|false)`.
    /// @note Mandate 9; never called on a recording path.
    [[nodiscard]] std::string to_string() const {
        return std::string("DiagnosticBinding(bound=") + (on_event != nullptr ? "true" : "false") +
               ")";
    }
};

static_assert(sizeof(DiagnosticBinding) == 2U * sizeof(void*),
              "a diagnostics binding is one borrowed pointer plus one callback");

} // namespace nexigen::core
