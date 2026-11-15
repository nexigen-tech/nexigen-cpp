// nexigen::chrono — time-sync detection thresholds (change
// time-sync-supervision).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace nexigen::chrono {

/// @brief Thresholds used to classify realtime/monotonic offset transitions.
///
/// Defaults are conservative: a step above 1 ms, a slew above 500 ppm, and a
/// rate evaluation window of at least 100 ms.
struct TimeSyncPolicy {
    std::int64_t max_step_ns{1'000'000};     ///< Largest tolerated offset jump.
    std::int64_t max_rate_ppm{500};          ///< Largest tolerated slew rate.
    std::int64_t min_window_ns{100'000'000}; ///< Minimum elapsed time for the rate check.

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("TimeSyncPolicy(max_step_ns={}, max_rate_ppm={}, min_window_ns={})",
                           max_step_ns, max_rate_ppm, min_window_ns);
    }
};

/// @brief Free helper mirroring `TimeSyncPolicy::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const TimeSyncPolicy& policy) {
    return policy.to_string();
}

} // namespace nexigen::chrono

/// @brief `std::formatter` for the time-sync policy (project mandate 11).
template <>
struct std::formatter<nexigen::chrono::TimeSyncPolicy> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::TimeSyncPolicy& policy, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(policy.to_string(), ctx);
    }
};
