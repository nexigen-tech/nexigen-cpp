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

/// Arbitration performance counters (single writer: the arbitration thread).
struct alignas(64) SmMetrics {
    std::atomic<std::uint64_t> last_transition_duration_us{0};
    std::atomic<std::uint64_t> arbitration_latency_us{0};
    std::atomic<std::uint64_t> rejected_requests_total{0};
    std::atomic<std::uint64_t> emergency_transitions_total{0};
    std::atomic<std::uint64_t> transition_timeouts_total{0};

    /// Cold-path diagnostic (mandate 9).
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

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const SmMetrics& metrics) {
    return metrics.to_string();
}

} // namespace nexigen::sm

/// The metrics format as their diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::sm::SmMetrics> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::SmMetrics& metrics, Ctx& ctx) const {
        const std::string text = metrics.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
