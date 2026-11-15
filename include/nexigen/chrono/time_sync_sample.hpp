// nexigen::chrono — one time-sync classification result (change
// time-sync-supervision).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/chrono/time_sync_status.hpp>

namespace nexigen::chrono {

/// @brief Result of one offset comparison.
struct TimeSyncSample {
    TimeSyncStatus status{TimeSyncStatus::Ok}; ///< Classification.
    std::int64_t offset_ns{0};                 ///< Current `REALTIME - MONOTONIC_RAW`.
    std::int64_t step_ns{0};                   ///< Offset change since the previous sample.
    std::int64_t rate_ppm{0};                  ///< Step rate in ppm; 0 when not evaluated.

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("TimeSyncSample(status={}, offset_ns={}, step_ns={}, rate_ppm={})",
                           nexigen::chrono::to_string(status), offset_ns, step_ns, rate_ppm);
    }
};

/// @brief Free helper mirroring `TimeSyncSample::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const TimeSyncSample& sample) {
    return sample.to_string();
}

} // namespace nexigen::chrono

/// @brief `std::formatter` for a time-sync sample (project mandate 11).
template <>
struct std::formatter<nexigen::chrono::TimeSyncSample> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::TimeSyncSample& sample, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(sample.to_string(), ctx);
    }
};
