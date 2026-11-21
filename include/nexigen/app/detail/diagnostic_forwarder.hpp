// nexigen::app — policy-logger diagnostics adapter (change
// add-telemetry-diagnostics; design D9).
//
// Maps a DiagnosticEvent onto the policy logger's severities. Formatting is
// deferred by the logger, so the adapter allocates nothing on the emitting
// (cold) thread.
#pragma once

#include <string>

#include <nexigen/core/concepts/diagnostic_logger_concept.hpp>
#include <nexigen/core/diagnostic_event.hpp>

namespace nexigen::app::detail {

/// @brief Forwards telemetry diagnostics to the application logger.
///
/// @tparam Logger Policy logger type satisfying the logging concept.
/// @note Spec `app/application`, requirement "Initialization follows a fixed
///       order".
template <class Logger>
class DiagnosticForwarder {
public:
    /// @brief Store the borrowed logger.
    ///
    /// @param logger Policy logger; must outlive this forwarder and the
    ///        providers bound to it.
    explicit DiagnosticForwarder(Logger& logger) noexcept : logger_(&logger) {}

    /// @brief Map one event onto the logger.
    ///
    /// `Degraded` is a warning carrying the error code; other transitions are
    /// informational.
    ///
    /// @param event Event to forward.
    /// @note Cold path only; the logger defers formatting.
    void diagnostic(const core::DiagnosticEvent& event) noexcept {
        if (event.kind == core::DiagnosticKind::Degraded) {
            logger_->warning("{} {} {}", event.source, event.kind, event.error);
        } else {
            logger_->info("{} {}", event.source, event.kind);
        }
    }

    /// @brief Render the adapter for cold-path diagnostics.
    ///
    /// @return The literal `"DiagnosticForwarder"`.
    /// @note Mandate 9.
    [[nodiscard]] std::string to_string() const { return "DiagnosticForwarder"; }

private:
    Logger* logger_{nullptr}; ///< Borrowed policy logger.
};

/// @brief Concept check for the adapter over a concrete logger.
///
/// @tparam Logger Policy logger type.
template <class Logger>
concept ForwardingDiagnosticLogger =
    core::concepts::DiagnosticLogger<DiagnosticForwarder<Logger>>;

} // namespace nexigen::app::detail
