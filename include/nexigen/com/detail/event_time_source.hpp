// nexigen::com — event-system host clock and epoch helpers.
#pragma once

#include <atomic>
#include <cstdint>
#include <ctime>

#include <unistd.h>

#include <nexigen/com/error.hpp>

namespace nexigen::com::detail {

/// @brief Read host `CLOCK_MONOTONIC_RAW` nanoseconds.
/// @return Current nanoseconds, or `Error::Unknown` when the clock read fails.
[[nodiscard]] inline Result<std::uint64_t> event_now_ns() noexcept {
    ::timespec ts{};
    if (::clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        return std::unexpected(Error::Unknown);
    }
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

/// @brief Generate a nonzero process-unique producer epoch.
/// @return Epoch value suitable for `EventHeader::producer_epoch`.
[[nodiscard]] inline std::uint64_t make_event_epoch() noexcept {
    static std::atomic<std::uint64_t> counter{0};
    ::timespec ts{};
    std::uint64_t realtime = 0;
    if (::clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        realtime = static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
                   static_cast<std::uint64_t>(ts.tv_nsec);
    }
    const auto sequence = counter.fetch_add(1U, std::memory_order_relaxed);
    const auto pid = static_cast<std::uint64_t>(::getpid());
    const std::uint64_t epoch = (realtime ^ (pid << 32U) ^ sequence) | 1ULL;
    return epoch;
}

/// @brief Allocate a process-unique event-consumer-set identifier.
/// @return Monotonic identifier used in the private wake service name.
[[nodiscard]] inline std::uint64_t next_event_set_id() noexcept {
    static std::atomic<std::uint64_t> counter{0};
    return counter.fetch_add(1U, std::memory_order_relaxed);
}

} // namespace nexigen::com::detail
