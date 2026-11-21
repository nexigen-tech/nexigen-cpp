// nexigen::phm — injectable watchdog device boundary (change
// close-phm-safety-loop).
//
// `Watchdog` owns the feed policy; a device owns the kernel interaction. The
// production device talks to `/dev/watchdog`, while tests drive an in-memory
// double so no CI machine can be reset.
#pragma once

#include <cstdint>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::phm {

/// @brief Kernel-facing watchdog operations.
///
/// Implementations are cold-path objects: `open` and `set_timeout` run once at
/// startup, `feed` runs once per feed period, and `disarm` runs on shutdown.
///
/// @note Spec `phm/watchdog`, requirement "Injectable device boundary".
class WatchdogDevice {
public:
    WatchdogDevice() = default;
    virtual ~WatchdogDevice() = default;

    WatchdogDevice(const WatchdogDevice&) = delete;
    WatchdogDevice& operator=(const WatchdogDevice&) = delete;

    /// @brief Open the device.
    ///
    /// @param path Device path; the caller resolves the default.
    /// @return Empty status on success, or the mapped OS error.
    [[nodiscard]] virtual core::Status open(std::string_view path) noexcept = 0;

    /// @brief Request a hardware timeout and report the granted value.
    ///
    /// @param requested_ms Requested timeout in milliseconds.
    /// @return The granted timeout, or the mapped OS error.
    [[nodiscard]] virtual core::Result<std::uint32_t>
    set_timeout(std::uint32_t requested_ms) noexcept = 0;

    /// @brief Feed the device once.
    /// @return Empty status on success, or the mapped OS error.
    [[nodiscard]] virtual core::Status feed() noexcept = 0;

    /// @brief Disarm the device (magic close) and release it.
    /// @return Empty status on success, or the mapped OS error.
    [[nodiscard]] virtual core::Status disarm() noexcept = 0;
};

} // namespace nexigen::phm
