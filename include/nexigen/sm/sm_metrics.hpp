// nexigen::sm — performance metrics (change add-sm; spec sm/telemetry
// "Performance metrics").
//
// Relaxed atomic counters updated by the arbitration thread and read by the
// background flusher; cache-line separated from the arbitration hot data.
#pragma once

#include <atomic>
#include <cstdint>
#include <format>
#include <string>

namespace nexigen::sm {

/// @brief Arbitration performance counters.
///
/// Every field is a relaxed atomic: the arbitration thread is the only writer,
/// the background flusher periodically loads all five. Relaxed ordering is
/// enough because the counters are independent observations, not a consistent
/// snapshot. The structure is `alignas(64)` so counter traffic does not share a
/// cache line with the arbitration hot data.
///
/// @note Spec `sm/telemetry`, requirement "Performance metrics".
struct alignas(64) SmMetrics {
    std::atomic<std::uint64_t> last_transition_duration_us{0}; ///< Last transition duration.
    std::atomic<std::uint64_t> arbitration_latency_us{0};      ///< Request-to-decision latency.
    std::atomic<std::uint64_t> rejected_requests_total{0};     ///< Requests rejected by policy.
    std::atomic<std::uint64_t> emergency_transitions_total{0}; ///< Forced recovery/emergency moves.
    std::atomic<std::uint64_t> transition_timeouts_total{0};   ///< Watchdog expiries observed.

    /// @brief Render all counters as a diagnostic string.
    ///
    /// Loads each counter with relaxed ordering; the values may be observed at
    /// slightly different instants.
    ///
    /// @return Text naming every counter and its current value; allocates.
    /// @note Spec `sm/telemetry`, requirement "Telemetry diagnostics".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("SmMetrics(last_transition_duration_us={}, arbitration_latency_us={}, "
                           "rejected_requests_total={}, emergency_transitions_total={}, "
                           "transition_timeouts_total={})",
                           last_transition_duration_us.load(std::memory_order_relaxed),
                           arbitration_latency_us.load(std::memory_order_relaxed),
                           rejected_requests_total.load(std::memory_order_relaxed),
                           emergency_transitions_total.load(std::memory_order_relaxed),
                           transition_timeouts_total.load(std::memory_order_relaxed));
    }
};

/// @brief Free diagnostic helper mirroring `SmMetrics::to_string`.
///
/// @param metrics Counters to render.
/// @return The member `to_string()` output.
/// @note Spec `sm/telemetry`, requirement "Telemetry diagnostics".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const SmMetrics& metrics) {
    return metrics.to_string();
}

} // namespace nexigen::sm

/// @brief `std::formatter` for `SmMetrics` (project mandate 11).
///
/// Delegates to `std::formatter<std::string_view>` after rendering the member
/// diagnostic, so `std::format("{}", metrics)` equals `to_string()`.
///
/// @note Spec `sm/telemetry`, requirement "Telemetry diagnostics".
template <> struct std::formatter<nexigen::sm::SmMetrics> : std::formatter<std::string_view> {
    /// @brief Format the counters as their diagnostic string.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param metrics Counters to render.
    /// @param ctx Output iterator context receiving the text.
    /// @return Iterator past the written characters.
    template <class Ctx> auto format(const nexigen::sm::SmMetrics& metrics, Ctx& ctx) const {
        const std::string text = metrics.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
