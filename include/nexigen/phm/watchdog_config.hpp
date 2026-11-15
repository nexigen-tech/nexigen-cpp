// nexigen::phm — watchdog configuration (change close-phm-safety-loop).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::phm {

/// @brief Watchdog section of the health manifest.
///
/// Disabled by default, so an existing manifest never opens a hardware device.
/// When enabled, `device` empty means `/dev/watchdog`, and `feed_period_ms`
/// must be positive and at most half of `timeout_ms` (validated when the
/// manifest is loaded). `timeout_ms` is a request: the driver may grant a
/// different value, which `Watchdog` reports.
///
/// @note Spec `phm/manifest`, requirement "Watchdog configuration".
struct WatchdogConfig {
    bool enabled{false};                  ///< Open and feed a device.
    std::string device{};                 ///< Device path; empty = `/dev/watchdog`.
    std::uint32_t timeout_ms{1000};       ///< Requested hardware timeout.
    std::uint32_t feed_period_ms{200};    ///< Feed at most once per period.

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text naming the enabled flag, device, timeout, and feed period.
    /// @note Mandate 9; allocates, so cold path only.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `WatchdogConfig::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const WatchdogConfig& config) {
    return config.to_string();
}

/// @brief ADL JSON writer for the watchdog section.
void to_json(nlohmann::json& j, const WatchdogConfig& config);

/// @brief ADL JSON reader for the watchdog section (non-throwing).
void from_json(const nlohmann::json& j, WatchdogConfig& config) noexcept;

} // namespace nexigen::phm

/// @brief `std::formatter` for the watchdog section (project mandate 11).
template <> struct std::formatter<nexigen::phm::WatchdogConfig> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::WatchdogConfig& config, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(config.to_string(), ctx);
    }
};
