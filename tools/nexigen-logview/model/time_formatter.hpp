// nexigen-logview — allocation-free timestamp and delta rendering.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "time_context.hpp"
#include "timestamp_mode.hpp"

namespace nexigen::logview {

/// @brief One rendered time cell.
struct FormattedTime {
    std::array<char, 48> text{}; ///< NUL-terminated display text.
    bool calibrated{false};      ///< Absolute came from the published time base.
    bool negative{false};        ///< The value is negative (delta or T0 offset).
    bool unknown{false}; ///< Absolute time is unavailable (attached base has no calibration).

    /// @brief Cold-path diagnostic rendering.
    /// @return The rendered text.
    [[nodiscard]] std::string to_string() const { return std::string(text.data()); }
};

/// @brief Render monotonic stamps and deltas into stack buffers.
class TimeFormatter {
public:
    /// @brief Render a record stamp in the selected timestamp mode.
    /// @param stamp_ns Frame monotonic timestamp.
    /// @param mode Display mode.
    /// @param context Time base, offset, and anchor.
    /// @return Rendered cell.
    [[nodiscard]] static FormattedTime format_timestamp(std::uint64_t stamp_ns, TimestampMode mode,
                                                        const TimeContext& context) noexcept;

    /// @brief Render a signed delta with adaptive units.
    /// @param delta_ns Signed nanosecond difference.
    /// @return Rendered cell.
    [[nodiscard]] static FormattedTime format_delta(std::int64_t delta_ns) noexcept;

    /// @brief Whether a delta crosses the configured anomaly threshold.
    /// @param delta_ns Signed nanosecond difference.
    /// @param threshold_ns Configured threshold.
    /// @return `true` when the magnitude exceeds the threshold.
    [[nodiscard]] static bool is_anomalous(std::int64_t delta_ns,
                                           std::int64_t threshold_ns) noexcept;

    /// @brief Magnitude of a signed value without overflow at `INT64_MIN`.
    /// @param value Signed value.
    /// @return Unsigned magnitude.
    [[nodiscard]] static std::uint64_t magnitude(std::int64_t value) noexcept;
};

/// @brief Free diagnostic rendering of a formatted cell.
/// @param time Cell to render.
/// @return The cell text.
[[nodiscard]] inline std::string to_string(const FormattedTime& time) {
    return time.to_string();
}

} // namespace nexigen::logview
