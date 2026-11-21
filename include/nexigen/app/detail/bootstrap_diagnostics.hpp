// nexigen::app — synchronous startup diagnostics (change
// add-telemetry-diagnostics; design D12).
//
// Used between process start and the logger becoming available, most notably
// for time-base attach failures. Fixed buffer, write(2), monotonic prefix, no
// rings, no worker, no time base, no allocation, never throws.
#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

#include <unistd.h>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/concepts/diagnostic_logger_concept.hpp>
#include <nexigen/core/diagnostic_event.hpp>

namespace nexigen::app::detail {

/// @brief Synchronous stderr diagnostics for the pre-logger window.
///
/// @note Spec `app/application`, requirement "Initialization follows a fixed
///       order"; design D12.
class BootstrapDiagnostics {
public:
    /// @brief Render one event to stderr without allocating.
    ///
    /// Formats a monotonic timestamp plus source, kind, and error names into a
    /// fixed 256-byte stack buffer and writes it with `write(2)`. Write failures
    /// are ignored: bootstrap diagnostics never abort startup.
    ///
    /// @param event Transition to report.
    /// @note Cold path only; called at most a handful of times per process.
    void diagnostic(const core::DiagnosticEvent& event) noexcept {
        const auto now = chrono::Timestamp::now();
        const std::uint64_t mono_ns = now ? now->nanoseconds() : 0U;
        char buffer[256];
        const auto written = std::format_to_n(
            buffer, sizeof buffer - 1U, "nexigen-bootstrap mono_ns={} source={} kind={} error={}\n",
            mono_ns, core::to_string(event.source), core::to_string(event.kind),
            core::to_string(event.error));
        const std::size_t written_size = static_cast<std::size_t>(written.size);
        const std::size_t count =
            written_size < sizeof buffer - 1U ? written_size : sizeof buffer - 1U;
        std::size_t offset = 0;
        while (offset < count) {
            const ssize_t n = ::write(STDERR_FILENO, buffer + offset, count - offset);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) {
                    continue;
                }
                return;
            }
            offset += static_cast<std::size_t>(n);
        }
    }

    /// @brief Render the writer for cold-path diagnostics.
    ///
    /// @return The literal `"BootstrapDiagnostics"`.
    /// @note Mandate 9.
    [[nodiscard]] std::string to_string() const { return "BootstrapDiagnostics"; }
};

static_assert(core::concepts::DiagnosticLogger<BootstrapDiagnostics>);

} // namespace nexigen::app::detail
