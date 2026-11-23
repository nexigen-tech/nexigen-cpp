// nexigen-logview — process-wide time base and anchor state.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace nexigen::logview {

/// @brief Inputs that turn monotonic frame stamps into displayed time.
struct TimeContext {
    bool time_base_attached{false};                ///< Whether `chrono::TimeBaseClock` is bound.
    std::int64_t utc_offset_ns{0};                 ///< Sampled UTC minus monotonic-raw offset.
    std::optional<std::int64_t> anchor_ns{};       ///< Single T0 anchor, if set.
    std::int64_t anomaly_threshold_ns{50'000'000}; ///< Delta highlight threshold (50 ms).
    bool mixed_boot{false}; ///< Imported records span more than one boot session.

    /// @brief Set or replace the anchor.
    /// @param stamp_ns Anchor timestamp in the monotonic domain.
    void set_anchor(std::int64_t stamp_ns) noexcept { anchor_ns = stamp_ns; }
    /// @brief Clear the anchor.
    void clear_anchor() noexcept { anchor_ns.reset(); }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with attachment, offset, anchor, threshold, and boot state.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic mirror for `TimeContext`.
/// @param context Context to render.
/// @return Text with attachment and anchor state.
[[nodiscard]] std::string to_string(const TimeContext& context);

} // namespace nexigen::logview
