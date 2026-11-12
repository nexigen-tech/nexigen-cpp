// nexigen::logging — per-shard anti-spam state (owner-written, consumer-read
// relaxed; spec logging/spam-protection).
#pragma once

#include <atomic>
#include <cstdint>

namespace nexigen::logging::detail {

/// @brief Per-shard anti-spam state: owner-written, consumer-read relaxed.
///
/// One instance per producer shard, `alignas(64)` so hot-path updates never
/// share a cache line with neighbouring shards. The owning producer thread is
/// the only writer of the plain fields; the consumer reads them while draining
/// and synthesizing drop summaries, and is the only thread that resets the
/// reported counters.
///
/// @note Spec `logging/spam-protection`, requirement "Repeat suppression
///       collapses identical messages" for the repeat fields and requirement
///       "Per-shard token-bucket rate limiter" for the token fields.
struct alignas(64) SpamShardState {
    /// @brief Hash of the previous message identity (location + format + args).
    std::uint64_t last_hash{0};
    /// @brief Consecutive identical messages in the current suppression run.
    std::uint32_t repeat_count{0};
    /// @brief Monotonic nanoseconds at which the suppression window started.
    std::uint64_t repeat_window_start{0};
    /// @brief Messages admitted in the current one-second rate window.
    std::uint64_t tokens_used{0};
    /// @brief Monotonic nanoseconds at which the rate window started.
    std::uint64_t window_start_ns{0};
    /// @brief Rate-limited drops, incremented by the owner and read/reset by
    ///        the consumer.
    ///
    /// @note Spec `logging/spam-protection`, requirement "Per-shard
    ///       token-bucket rate limiter"; reset only on the consumer side per
    ///       `logging/multi-producer`, requirement "Reported drop cells reset
    ///       after synthesis", scenario "Consumer is the only resetter of
    ///       reported cells".
    std::atomic<std::uint64_t> throttled_count{0};
    /// @brief Explicit tail padding; never written, keeps the state one line.
    std::uint8_t pad_[16]{};
};

} // namespace nexigen::logging::detail
