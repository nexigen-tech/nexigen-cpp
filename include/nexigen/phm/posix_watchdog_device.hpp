// nexigen::phm — Linux watchdog device (change close-phm-safety-loop).
#pragma once

#include <cstdint>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/phm/watchdog_device.hpp>

namespace nexigen::phm {

/// @brief `/dev/watchdog` implementation of `WatchdogDevice`.
///
/// Opens the device, negotiates the timeout with `WDIOC_SETTIMEOUT`, feeds it
/// with a one-byte write, and disarms with the magic-close sequence (`'V'`
/// followed by `close`). The destructor disarms best-effort, so a clean
/// shutdown cannot reset the machine; a kernel configured with `nowayout`
/// ignores the magic close by design.
///
/// @note Spec `phm/watchdog`, requirement "Watchdog ownership and lifecycle".
class PosixWatchdogDevice final : public WatchdogDevice {
public:
    PosixWatchdogDevice() noexcept = default;

    /// @brief Disarm and close the device when still open.
    ~PosixWatchdogDevice() override;

    PosixWatchdogDevice(const PosixWatchdogDevice&) = delete;
    PosixWatchdogDevice& operator=(const PosixWatchdogDevice&) = delete;

    /// @brief Open the device read-write with close-on-exec.
    [[nodiscard]] core::Status open(std::string_view path) noexcept override;

    /// @brief Set `WDIOC_SETTIMEOUT` and read back `WDIOC_GETTIMEOUT`.
    [[nodiscard]] core::Result<std::uint32_t>
    set_timeout(std::uint32_t requested_ms) noexcept override;

    /// @brief Write one byte to keep the timer alive.
    [[nodiscard]] core::Status feed() noexcept override;

    /// @brief Magic-close the device and release the descriptor.
    [[nodiscard]] core::Status disarm() noexcept override;

    /// @brief Whether a descriptor is currently open.
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

private:
    int fd_{-1};              ///< Open descriptor, or -1.
    bool disarmed_{false};    ///< Magic close already written.
};

} // namespace nexigen::phm
