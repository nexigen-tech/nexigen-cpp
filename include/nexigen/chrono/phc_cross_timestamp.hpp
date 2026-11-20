// nexigen::chrono — PTP hardware clock cross-timestamp backend (change
// add-chrono-service-contract; spec chrono/time-base "Hardware domain access
// modes"; design D-F).
//
// The Linux PTP subsystem exposes `PTP_SYS_OFFSET_PRECISE` for clocks whose
// driver can capture the system and PHC time atomically. This class owns one
// `/dev/ptpN` descriptor and turns that ioctl into a `TimeBaseCorrelationPair`;
// it never converts ticks and never keeps a map, because the estimator owns the
// map.
//
// The class is move-only and allocates nothing on `read_pair`, so the daemon can
// call it once per publication interval.
#pragma once

#include <cstdint>
#include <string>

#include <nexigen/chrono/time_base_device.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// @brief One opened PTP hardware clock read through cross-timestamps.
///
/// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
class PhcCrossTimestampClock {
public:
    /// @brief Open a PTP clock device.
    ///
    /// @param path Device path such as `/dev/ptp2`.
    /// @return The opened clock, `NotFound` when the device is absent,
    ///         `InvalidArgument` for an empty path, or `SystemError` when the
    ///         descriptor cannot be opened. Never throws.
    /// @note Cold path only (daemon startup).
    [[nodiscard]] static core::Result<PhcCrossTimestampClock> open(std::string_view path) noexcept;

    /// @brief Move-construct, transferring the descriptor.
    /// @param other Clock whose descriptor is adopted; it becomes closed.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    PhcCrossTimestampClock(PhcCrossTimestampClock&& other) noexcept;
    /// @brief Move-assign, closing any descriptor already held.
    /// @param other Clock whose descriptor is adopted; it becomes closed.
    /// @return This clock.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    PhcCrossTimestampClock& operator=(PhcCrossTimestampClock&& other) noexcept;
    /// @brief Copying is deleted: the class owns a descriptor.
    PhcCrossTimestampClock(const PhcCrossTimestampClock&) = delete;
    /// @brief Copy assignment is deleted: the class owns a descriptor.
    PhcCrossTimestampClock& operator=(const PhcCrossTimestampClock&) = delete;

    /// @brief Close the descriptor.
    ~PhcCrossTimestampClock();

    /// @brief Whether a descriptor is held.
    /// @return `true` when the clock is open and readable.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }
    /// @brief The held descriptor, or `-1`.
    /// @return Native file descriptor owned by this clock.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] int descriptor() const noexcept { return fd_; }
    /// @brief The device path this clock was opened from.
    /// @return Borrowed path text, or empty for a moved-from clock.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] std::string_view path() const noexcept { return path_; }

    /// @brief Read one `PTP_SYS_OFFSET_PRECISE` cross-timestamp pair.
    ///
    /// @param out Destination pair; `latched` is `true` because the driver
    ///            captures both sides atomically.
    /// @return `false` when the descriptor is closed, the ioctl fails, or the
    ///         result cannot be represented.
    /// @note Hot-path safe: one ioctl, no allocation.
    [[nodiscard]] bool read_pair(TimeBaseCorrelationPair& out) noexcept;

    /// @brief Backend adapter over this clock.
    ///
    /// @param domain_id Domain id the backend publishes.
    /// @param epoch Device lifecycle epoch.
    /// @param name Borrowed device name.
    /// @return A callable backend for the daemon loop.
    [[nodiscard]] TimeBaseDeviceBackend backend(std::uint16_t domain_id, std::uint64_t epoch,
                                                std::string_view name) noexcept;

private:
    PhcCrossTimestampClock(int fd, std::string path) noexcept;

    static bool read_thunk(void* self, TimeBaseCorrelationPair& out) noexcept;

    int fd_{-1};
    std::string path_{};
};

} // namespace nexigen::chrono
