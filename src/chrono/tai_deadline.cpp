// nexigen::chrono — absolute TAI deadline implementation.
#include <cerrno>
#include <ctime>

#include <nexigen/chrono/tai_deadline.hpp>

namespace nexigen::chrono {

core::Status sleep_until_tai(std::int64_t tai_ns) noexcept {
    const std::optional<timespec> deadline = tai_deadline(tai_ns);
    if (!deadline.has_value()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
#if defined(CLOCK_TAI)
    int result = 0;
    do {
        result = ::clock_nanosleep(CLOCK_TAI, TIMER_ABSTIME, &*deadline, nullptr);
    } while (result == EINTR);
    if (result != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
#else
    return std::unexpected(core::ErrorCode::UnsupportedFormat);
#endif
}

} // namespace nexigen::chrono
