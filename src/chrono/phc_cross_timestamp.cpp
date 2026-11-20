// nexigen::chrono — PTP hardware clock cross-timestamp backend (change
// add-chrono-service-contract; design D-F).
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include <nexigen/chrono/phc_cross_timestamp.hpp>
#include <nexigen/core/detail/fault_injection.hpp>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/ptp_clock.h>

#include <chrono>
#endif

namespace nexigen::chrono {
namespace {

#if defined(__linux__)
/// @brief Convert a `ptp_clock_time` to nanoseconds.
///
/// @param time Clock time split into seconds and nanoseconds.
/// @return Nanoseconds, or zero when the nanoseconds field is out of range.
[[nodiscard]] std::uint64_t clock_time_ns(const ptp_clock_time& time) noexcept {
    if (time.sec < 0 || time.nsec >= 1'000'000'000U) {
        return 0U;
    }
    return static_cast<std::uint64_t>(time.sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(time.nsec);
}
#endif

} // namespace

PhcCrossTimestampClock::PhcCrossTimestampClock(int fd, std::string path) noexcept
    : fd_(fd), path_(std::move(path)) {}

PhcCrossTimestampClock::PhcCrossTimestampClock(PhcCrossTimestampClock&& other) noexcept
    : fd_(other.fd_), path_(std::move(other.path_)) {
    other.fd_ = -1;
}

PhcCrossTimestampClock& PhcCrossTimestampClock::operator=(PhcCrossTimestampClock&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_));
        }
        fd_ = other.fd_;
        path_ = std::move(other.path_);
        other.fd_ = -1;
    }
    return *this;
}

PhcCrossTimestampClock::~PhcCrossTimestampClock() {
    if (fd_ >= 0) {
        static_cast<void>(::close(fd_));
    }
}

core::Result<PhcCrossTimestampClock> PhcCrossTimestampClock::open(std::string_view path) noexcept {
    if (path.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
#if defined(__linux__)
    const std::string copy{path};
    const int fd = ::open(copy.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(errno == ENOENT ? core::ErrorCode::NotFound
                                               : core::ErrorCode::SystemError);
    }
    return PhcCrossTimestampClock{fd, copy};
#else
    return std::unexpected(core::ErrorCode::UnsupportedFormat);
#endif
}

bool PhcCrossTimestampClock::read_pair(TimeBaseCorrelationPair& out) noexcept {
#if defined(__linux__)
    if (fd_ < 0) {
        return false;
    }
    std::uint64_t device_ns = 0U;
    std::uint64_t raw_before = 0U;
    if (nexigen::core::fault_injection::fault(
            nexigen::core::fault_injection::FaultPoint::Chrono_phc_cross_timestamp)) {
        // Test-only seam: synthesize the pair the driver would return so the
        // conversion and publication path is exercised without a PTP device.
        ::timespec ts{};
        if (::clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
            return false;
        }
        raw_before = static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
                     static_cast<std::uint64_t>(ts.tv_nsec);
        device_ns = raw_before + 1'000U;
    } else {
        ptp_sys_offset_precise precise{};
        if (::ioctl(fd_, PTP_SYS_OFFSET_PRECISE, &precise) != 0) {
            return false;
        }
        device_ns = clock_time_ns(precise.device);
        raw_before = clock_time_ns(precise.sys_monoraw);
        if (device_ns == 0U || raw_before == 0U) {
            return false;
        }
    }
    out.native_ticks = device_ns;
    out.canonical_ns = static_cast<std::int64_t>(raw_before);
    out.read_latency_ns = 0U;
    // A precise cross-timestamp contributes only the driver's capture jitter.
    out.uncertainty_ns = 1U;
    out.latched = true;
    out.ptm_capable = false;
    return true;
#else
    static_cast<void>(out);
    return false;
#endif
}

bool PhcCrossTimestampClock::read_thunk(void* self, TimeBaseCorrelationPair& out) noexcept {
    return static_cast<PhcCrossTimestampClock*>(self)->read_pair(out);
}

TimeBaseDeviceBackend PhcCrossTimestampClock::backend(std::uint16_t domain_id, std::uint64_t epoch,
                                                      std::string_view name) noexcept {
    return TimeBaseDeviceBackend{this, &PhcCrossTimestampClock::read_thunk, domain_id, epoch,
                                 name, DeviceAccessMode::PhcCrossTimestamp};
}

} // namespace nexigen::chrono
