// nexigen::phm — hardware watchdog with a supervision-derived feed policy
// (change close-phm-safety-loop).
#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/phm/watchdog_config.hpp>
#include <nexigen/phm/watchdog_device.hpp>
#include <nexigen/phm/watchdog_state.hpp>

namespace nexigen::phm {

/// @brief Daemon-owned watchdog with a feed policy tied to supervision.
///
/// `create` opens and arms the device (unless the manifest disables it); the
/// monitoring loop then calls `tick` once per cycle with the aggregate health.
/// Feeding stops while any supervised process is `Expired` and while the loop
/// missed its feed period, and resumes on the next healthy cycle. `disarm`
/// performs the magic close on shutdown.
///
/// All methods are cold path: one device write per feed period. The type is
/// move-only because it refers to a device it does not own.
///
/// @note Spec `phm/watchdog`, requirements "Watchdog ownership and lifecycle"
///       and "Feed policy".
class Watchdog {
public:
    /// @brief Clock used for feed-period accounting.
    using Clock = std::chrono::steady_clock;

    /// @brief Open and arm the configured device, or return a disabled watchdog.
    ///
    /// @param device Device boundary; must outlive the returned watchdog.
    /// @param config Manifest watchdog section.
    /// @return The watchdog, or the device error when it cannot be opened or
    ///         armed while enabled. A disabled config never touches the device.
    /// @note Spec `phm/watchdog`, requirement "Watchdog ownership and lifecycle".
    [[nodiscard]] static core::Result<Watchdog> create(WatchdogDevice& device,
                                                       const WatchdogConfig& config) noexcept;

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    Watchdog(Watchdog&&) noexcept = default;
    Watchdog& operator=(Watchdog&&) noexcept = default;

    /// @brief Feed when the system is healthy and the loop kept its period.
    ///
    /// @param now Monotonic cycle time.
    /// @param any_expired Whether any armed supervised process is `Expired`.
    ///        A true value stops feeding until a later healthy cycle.
    /// @note Spec `phm/watchdog`, requirement "Feed policy".
    void tick(Clock::time_point now, bool any_expired) noexcept;

    /// @brief Stop feeding without disarming, so the hardware timeout fires.
    ///
    /// @note Spec `phm/recovery`, requirement "SafeState escalation": the
    ///       watchdog reset is local actuation, not an EM request.
    void stop_feeding() noexcept;

    /// @brief Disarm the device (magic close) and mark the watchdog stopped.
    void disarm() noexcept;

    /// @brief Whether the manifest enabled the watchdog.
    [[nodiscard]] bool enabled() const noexcept { return config_.enabled; }

    /// @brief Whether feeding is currently active.
    [[nodiscard]] bool feeding() const noexcept { return feeding_; }

    /// @brief Current watchdog state.
    [[nodiscard]] WatchdogState state() const noexcept { return state_; }

    /// @brief Timeout granted by the driver, or 0 when disabled.
    [[nodiscard]] std::uint32_t timeout_ms() const noexcept { return granted_ms_; }

    /// @brief Number of successful feed writes.
    [[nodiscard]] std::uint64_t feeds() const noexcept { return feeds_; }

    /// @brief Effective device path (`/dev/watchdog` when the config is empty).
    [[nodiscard]] std::string device_path() const;

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text naming the device, granted timeout, feed count, and state.
    /// @note Mandate 9; allocates, so cold path only. Spec `phm/watchdog`,
    ///       requirement "Watchdog diagnostics".
    [[nodiscard]] std::string to_string() const;

private:
    Watchdog(WatchdogDevice& device, WatchdogConfig config, std::uint32_t granted_ms) noexcept;

    WatchdogDevice* device_;        ///< Borrowed device boundary.
    WatchdogConfig config_;         ///< Manifest section.
    WatchdogState state_;           ///< Observable state.
    std::uint32_t granted_ms_;      ///< Timeout granted by the driver.
    std::uint64_t feeds_{0};        ///< Successful feed writes.
    Clock::time_point last_tick_{}; ///< Previous cycle time.
    Clock::time_point last_feed_{}; ///< Previous feed time.
    bool have_tick_{false};         ///< `last_tick_` is valid.
    bool have_feed_{false};         ///< `last_feed_` is valid.
    bool feeding_{false};           ///< Feed policy is active.
};

/// @brief Free helper mirroring `Watchdog::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const Watchdog& watchdog) {
    return watchdog.to_string();
}

} // namespace nexigen::phm

/// @brief `std::formatter` for the watchdog (project mandate 11).
template <> struct std::formatter<nexigen::phm::Watchdog> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::Watchdog& watchdog, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(watchdog.to_string(), ctx);
    }
};
