// nexigen::logging — per-shard anti-spam state (owner-written, consumer-read
// relaxed; spec logging/spam-protection).
#pragma once

#include <atomic>
#include <cstdint>

namespace nexigen::logging::detail {

/// Per-shard anti-spam state (owner-written, consumer-read relaxed).
struct alignas(64) SpamShardState {
    std::uint64_t last_hash{0};                    ///< hash of previous message identity
    std::uint32_t repeat_count{0};                 ///< consecutive identical messages
    std::uint64_t repeat_window_start{0};          ///< monotonic ns of window start
    std::uint64_t tokens_used{0};                  ///< messages in current second
    std::uint64_t window_start_ns{0};              ///< rate-limit window start
    std::atomic<std::uint64_t> throttled_count{0}; ///< rate-limited drops
    std::uint8_t pad_[16]{};
};

} // namespace nexigen::logging::detail
