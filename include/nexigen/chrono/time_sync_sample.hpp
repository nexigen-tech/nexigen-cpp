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
///
/// `TimeSyncMonitor::sample` fills one value per call: the current offset, its
/// change since the previous sample, the classification, and the measured slew
/// rate when it is meaningful. The first sample only establishes the baseline,
/// so `step_ns` and `rate_ppm` stay 0 and `status` is `Ok`.
///
/// All time values are signed nanoseconds; `rate_ppm` is parts per million.
///
/// @note Spec `chrono/time-sync`, requirement "Monitor diagnostics".
struct TimeSyncSample {
    TimeSyncStatus status{TimeSyncStatus::Ok}; ///< Classification of this transition.
    std::int64_t offset_ns{0};                 ///< Current `REALTIME - MONOTONIC_RAW`, in ns.
    std::int64_t step_ns{0};                   ///< Offset change since the previous sample, in ns.
    /// Step rate in ppm; 0 when not evaluated (first sample, short window, or a
    /// step outside `max_step_ns`).
    std::int64_t rate_ppm{0};

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text of the form
    ///         `TimeSyncSample(status=<name>, offset_ns=<ns>, step_ns=<ns>, rate_ppm=<ppm>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `chrono/time-sync`, requirement "Monitor diagnostics".
    [[nodiscard]] std::string to_string() const {
        return std::format("TimeSyncSample(status={}, offset_ns={}, step_ns={}, rate_ppm={})",
                           nexigen::chrono::to_string(status), offset_ns, step_ns, rate_ppm);
    }
};

/// @brief Free helper mirroring `TimeSyncSample::to_string()` (mandate 9).
///
/// @param sample Sample to render.
/// @return The member diagnostic text.
/// @note Mandate 9; allocates, so cold path only. Spec `chrono/time-sync`,
///       requirement "Monitor diagnostics".
[[nodiscard]] inline std::string to_string(const TimeSyncSample& sample) {
    return sample.to_string();
}

} // namespace nexigen::chrono

/// @brief `std::formatter` for a time-sync sample (project mandate 11).
///
/// Makes `std::format("{}", sample)` equal `to_string(sample)`.
///
/// @note Mandate 11 (universal `std::formatter`); spec `chrono/time-sync`,
///       requirement "Monitor diagnostics".
template <>
struct std::formatter<nexigen::chrono::TimeSyncSample> : std::formatter<std::string_view> {
    /// @brief Write the sample diagnostic into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param sample Sample to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Mandate 11; spec `chrono/time-sync`, requirement "Monitor
    ///       diagnostics".
    template <class Ctx>
    auto format(const nexigen::chrono::TimeSyncSample& sample, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(sample.to_string(), ctx);
    }
};
