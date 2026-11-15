// nexigen::phm — Linux watchdog device implementation (change
// close-phm-safety-loop).
#include <nexigen/phm/posix_watchdog_device.hpp>

#include <cerrno>
#include <string>

#include <fcntl.h>
#include <linux/watchdog.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace nexigen::phm {

namespace {

/// @brief One `write` retried on `EINTR`.
[[nodiscard]] core::Status write_all(int fd, const char* data, std::size_t size) noexcept {
    std::size_t written = 0;
    while (written < size) {
        const ssize_t chunk = ::write(fd, data + written, size - written);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(core::ErrorCode::SystemError);
        }
        written += static_cast<std::size_t>(chunk);
    }
    return {};
}

} // namespace

PosixWatchdogDevice::~PosixWatchdogDevice() {
    static_cast<void>(disarm());
}

core::Status PosixWatchdogDevice::open(std::string_view path) noexcept {
    if (fd_ >= 0) {
        return std::unexpected(core::ErrorCode::Conflict);
    }
    const std::string terminated(path);
    const int fd = ::open(terminated.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    fd_ = fd;
    disarmed_ = false;
    return {};
}

core::Result<std::uint32_t>
PosixWatchdogDevice::set_timeout(std::uint32_t requested_ms) noexcept {
    if (fd_ < 0) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    int value = static_cast<int>(requested_ms);
    if (::ioctl(fd_, WDIOC_SETTIMEOUT, &value) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    int granted = 0;
    if (::ioctl(fd_, WDIOC_GETTIMEOUT, &granted) != 0 || granted <= 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return static_cast<std::uint32_t>(granted);
}

core::Status PosixWatchdogDevice::feed() noexcept {
    if (fd_ < 0 || disarmed_) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    constexpr char ping = '*';
    return write_all(fd_, &ping, 1);
}

core::Status PosixWatchdogDevice::disarm() noexcept {
    if (fd_ < 0) {
        return {};
    }
    if (!disarmed_) {
        // The Linux watchdog API treats 'V' as the magic close; a kernel built
        // with `nowayout` ignores it and keeps counting down.
        constexpr char magic = 'V';
        static_cast<void>(write_all(fd_, &magic, 1));
        disarmed_ = true;
    }
    const int fd = fd_;
    fd_ = -1;
    if (::close(fd) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

} // namespace nexigen::phm
