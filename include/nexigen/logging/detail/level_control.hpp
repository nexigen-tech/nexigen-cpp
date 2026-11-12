// nexigen::logging — shared atomic level-control cell (design.md D2).
//
// Every concrete logger composes exactly one LevelControl; this is where the
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

#include <nexigen/core/log_level.hpp>

namespace nexigen::logging::detail {

/// @brief Cache-line-padded runtime threshold shared by all concrete loggers.
///
/// Each concrete logger composes exactly one `LevelControl`; the atomic lives
/// alone on its own cache line, so throttle windows, drop counters, and ring
/// indices of the surrounding logger never dirty it while a producer polls
/// `level()`. Both edges are single relaxed-atomic operations: reading is the
/// whole filtered-call cost (no timestamp, ring access, TLS, or lock), and
/// publishing is one store. Non-copyable and non-movable because of the
/// `std::atomic` member; instances are embedded in long-lived loggers.
///
/// @note Spec `logging/log-level`, requirement "Runtime level control" and
///       requirement "Sub-threshold calls short-circuit".
class LevelControl {
    static_assert(std::atomic<core::LogLevel>::is_always_lock_free,
                  "core::LogLevel atomics must be lock-free");

    /// @brief Cache-line-aligned storage for the threshold plus padding.
    struct alignas(64) Cell {
        /// @brief Active threshold; relaxed loads and stores only.
        std::atomic<core::LogLevel> level{core::LogLevel::Info};
        /// @brief Tail padding, never written; keeps `Cell` one cache line.
        std::uint8_t pad_[63]{};
    } cell_{}; ///< The single padded cell holding the runtime threshold.

public:
    /// @brief Construct with the default `Info` threshold.
    constexpr LevelControl() noexcept = default;

    /// @brief Construct with a non-default initial threshold.
    ///
    /// @param initial Threshold published at construction time.
    explicit constexpr LevelControl(core::LogLevel initial) noexcept : cell_{.level = initial} {}

    /// @brief Copy construction is deleted: the cell owns an atomic.
    LevelControl(const LevelControl&) = delete;
    /// @brief Copy assignment is deleted: the cell owns an atomic.
    LevelControl& operator=(const LevelControl&) = delete;
    /// @brief Move construction is deleted: the cell owns an atomic.
    LevelControl(LevelControl&&) = delete;
    /// @brief Move assignment is deleted: the cell owns an atomic.
    LevelControl& operator=(LevelControl&&) = delete;

    /// @brief Producer/agent edge: publish a new threshold.
    ///
    /// @param new_level Threshold visible to subsequent `level()` reads.
    /// @note Spec `logging/log-level`, requirement "Runtime level control": one
    ///       relaxed store, no lock or ordering handshake.
    void set_level(core::LogLevel new_level) noexcept {
        cell_.level.store(new_level, std::memory_order_relaxed);
    }

    /// @brief Hot-path read: current threshold as last visible to this thread.
    ///
    /// @return The threshold this thread would filter against.
    /// @note Spec `logging/log-level`, requirement "Sub-threshold calls
    ///       short-circuit": one relaxed load plus the caller's compare.
    [[nodiscard]] core::LogLevel level() const noexcept {
        return cell_.level.load(std::memory_order_relaxed);
    }
};

static_assert(sizeof(LevelControl) == 64, "level control must own one cache line");

} // namespace nexigen::logging::detail
