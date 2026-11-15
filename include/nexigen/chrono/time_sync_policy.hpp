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
/// One policy drives both the pure classification (`TimeSyncMonitor::classify`)
/// and the clock-backed sampling (`TimeSyncMonitor::sample`). The values are
/// deliberately plain `std::int64_t` nanoseconds and parts per million, so the
/// policy is trivially copyable, needs no allocation, and can be constructed in
/// a real-time thread's start-up path.
///
/// Defaults are conservative: a step above 1 ms, a slew above 500 ppm, and a
/// rate evaluation window of at least 100 ms.
///
/// @note Spec `chrono/time-sync`, requirement "Threshold policy".
struct TimeSyncPolicy {
    std::int64_t max_step_ns{1'000'000};     ///< Largest tolerated offset jump, in nanoseconds.
    std::int64_t max_rate_ppm{500};          ///< Largest tolerated slew rate, in parts per million.
    std::int64_t min_window_ns{100'000'000}; ///< Minimum elapsed time for the rate check.

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text of the form
    ///         `TimeSyncPolicy(max_step_ns=<ns>, max_rate_ppm=<ppm>, min_window_ns=<ns>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `chrono/time-sync`, requirement "Threshold policy".
    [[nodiscard]] std::string to_string() const {
        return std::format("TimeSyncPolicy(max_step_ns={}, max_rate_ppm={}, min_window_ns={})",
                           max_step_ns, max_rate_ppm, min_window_ns);
    }
};

/// @brief Free helper mirroring `TimeSyncPolicy::to_string()` (mandate 9).
///
/// @param policy Policy to render.
/// @return The member diagnostic text.
/// @note Mandate 9; allocates, so cold path only. Spec `chrono/time-sync`,
///       requirement "Threshold policy".
[[nodiscard]] inline std::string to_string(const TimeSyncPolicy& policy) {
    return policy.to_string();
}

} // namespace nexigen::chrono

/// @brief `std::formatter` for the time-sync policy (project mandate 11).
///
/// Makes `std::format("{}", policy)` equal `to_string(policy)`.
///
/// @note Mandate 11 (universal `std::formatter`); spec `chrono/time-sync`,
///       requirement "Threshold policy".
template <>
struct std::formatter<nexigen::chrono::TimeSyncPolicy> : std::formatter<std::string_view> {
    /// @brief Write the policy diagnostic into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param policy Policy to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Mandate 11; spec `chrono/time-sync`, requirement "Threshold
    ///       policy".
    template <class Ctx>
    auto format(const nexigen::chrono::TimeSyncPolicy& policy, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(policy.to_string(), ctx);
    }
};
