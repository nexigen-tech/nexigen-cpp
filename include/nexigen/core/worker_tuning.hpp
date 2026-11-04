// nexigen::core — idle-behaviour knobs shared by every background consumer
// thread (logging AsyncWorker, tracing TracerProvider).
//
// A consumer that polls empty rings burns a pinned core with context
// switches. When many processes pin their loggers/tracers to one or two
// shared cores, the idle wake-up rate is what saturates them, so all
// consumers use the same policy:
//   * only a few failed sweeps (spin_budget) before the first sleep;
//   * the sleep grows geometrically (100 us, 200 us, ...) up to
//     poll_interval_us, so a burst right after activity is still picked up
//     within ~100 us while a long-idle worker wakes once per interval;
//   * the worker widens its OWN timer slack to half of the interval (never
//     below the kernel default of 50 us), letting the kernel coalesce idle
//     wake-ups of co-located consumers into shared bursts.
// Producers never wait on any of this; the shard rings must absorb bursts
// for up to poll_interval_us (ShardCapacity >= peak records/s * interval).
#pragma once

#include <cstdint>

#if defined(__linux__)
#include <sys/prctl.h>
#endif

namespace nexigen::core {

struct WorkerTuning {
    static constexpr std::uint32_t default_poll_interval_us = 1000U;
    static constexpr std::uint32_t min_poll_interval_us = 50U;
    static constexpr std::uint32_t max_poll_interval_us = 100'000U;
    static constexpr std::uint32_t default_spin_budget = 4U;
    static constexpr std::uint64_t backoff_floor_us = 100U; ///< first idle sleep

    /// Ceiling of the idle sleep; clamped to [min, max] by clamp_poll_us().
    std::uint32_t poll_interval_us = default_poll_interval_us;
    /// Failed sweeps before the first sleep (0 = sleep right away).
    std::uint32_t spin_budget = default_spin_budget;

    [[nodiscard]] static constexpr std::uint64_t clamp_poll_us(std::uint32_t requested) noexcept {
        if (requested < min_poll_interval_us) {
            return min_poll_interval_us;
        }
        if (requested > max_poll_interval_us) {
            return max_poll_interval_us;
        }
        return requested;
    }

    /// Sleep length after `failed_sleeps` consecutive empty wake-ups
    /// (1 = first): floor, doubling each time, capped at `poll_max_us`.
    [[nodiscard]] static constexpr std::uint64_t backoff_us(std::uint64_t failed_sleeps,
                                                            std::uint64_t poll_max_us) noexcept {
        const std::uint64_t floor_us =
            backoff_floor_us < poll_max_us ? backoff_floor_us : poll_max_us;
        const std::uint64_t shift = failed_sleeps - 1U < 16U ? failed_sleeps - 1U : 16U;
        const std::uint64_t us = floor_us << shift;
        return us < poll_max_us ? us : poll_max_us;
    }

    /// Call ON the worker thread: widen its timer slack to poll_max_us / 2 so
    /// idle sleeps of co-located consumers coalesce. Failure is harmless.
    static void apply_timer_slack(std::uint64_t poll_max_us) noexcept {
#if defined(__linux__) && defined(PR_SET_TIMERSLACK)
        const std::uint64_t slack_ns = poll_max_us * 500U;
        if (slack_ns > 50'000U) { // never below the kernel default (50 us)
            static_cast<void>(
                ::prctl(PR_SET_TIMERSLACK, static_cast<unsigned long>(slack_ns), 0, 0, 0));
        }
#else
        static_cast<void>(poll_max_us);
#endif
    }
};

} // namespace nexigen::core
