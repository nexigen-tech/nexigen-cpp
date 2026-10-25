// nexigen::logging — shared atomic level-control cell (design.md D2).
//
// Every concrete logger composes exactly one Level_control; this is where the
// "dynamic runtime control" requirement lives. Both edges are single
// relaxed-atomic operations:
//   - level()  : one relaxed load + caller's compare — the whole filtered-call
//                cost (~1 ns): no timestamp, no ring access, no TLS, no lock.
//   - set_level: one relaxed store. Relaxed is deliberate — level values are
//                independent scalars and each LogRecord is self-contained, so
//                there is no other data whose visibility must be ordered by a
//                level store. A thread acting on a stale level for a few
//                instructions simply filtered/admitted as the old threshold
//                did: benign, and the only ordering a hot path can afford.
//
// False sharing: the atomic lives alone on its own cache line (padded Cell),
// so throttle windows / drop counters / ring indices of the surrounding
// logger never dirty this line while the producer hammers level().
//
// The class is non-copyable/non-movable (std::atomic member), like
// nexigen::core::SpscRingBuffer: instances are embedded in long-lived objects.
#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging::detail {

/// Cache-line-padded runtime threshold shared by all concrete loggers.
class Level_control {
    static_assert(std::atomic<LogLevel>::is_always_lock_free,
                  "LogLevel atomics must be lock-free");

    struct alignas(64) Cell {
        std::atomic<LogLevel> level{LogLevel::Info}; ///< Relaxed load/store only.
        std::uint8_t pad_[63]{};                     ///< Padding, never written.
    } cell_{};

public:
    constexpr Level_control() noexcept = default;

    /// Start at a non-default threshold (config-driven construction time).
    explicit constexpr Level_control(LogLevel initial) noexcept
        : cell_{.level = initial} {}

    Level_control(const Level_control&) = delete;
    Level_control& operator=(const Level_control&) = delete;
    Level_control(Level_control&&) = delete;
    Level_control& operator=(Level_control&&) = delete;

    /// Producer/agent edge: publish a new threshold. Relaxed store — the new
    /// value becomes visible with no ordering obligations (see header).
    void set_level(LogLevel new_level) noexcept {
        cell_.level.store(new_level, std::memory_order_relaxed);
    }

    /// Hot-path read: current threshold as last visible to this thread.
    [[nodiscard]] LogLevel level() const noexcept {
        return cell_.level.load(std::memory_order_relaxed);
    }
};

static_assert(sizeof(Level_control) == 64, "level control must own one cache line");

} // namespace nexigen::logging::detail
