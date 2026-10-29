// nexigen::logging — wait-free SPSC shard ring with overflow policies
// (change rework-logging-hot-path-engine, design D2; spec
// logging/spsc-shard-ring).
//
// `SpscShardRing<T, Capacity>` is a single-producer/single-consumer ring
// buffer supporting two overflow policies:
//   - DropNewest: incoming record dropped when ring is full; counted.
//   - OverwriteOldest: oldest unread record overwritten atomically; counted.
//
// TSan-clean: producer writes slot under head release, consumer reads under
// tail release. `core::SpscRingBuffer` is NOT modified.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include <nexigen/logging/logger_options.hpp>

namespace nexigen::logging::detail {

/// Wait-free SPSC ring with overflow policy.
template <class T, std::size_t Capacity>
class SpscShardRing {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "shard ring capacity must be a power of two >= 2");
    static_assert(std::is_trivially_copyable_v<T>,
                  "shard ring element must be trivially copyable");

    T slots_[Capacity]{};
    std::atomic<std::uint64_t> head_{0};    ///< producer write cursor
    std::atomic<std::uint64_t> tail_{0};    ///< consumer read cursor
    OverflowPolicy overflow_{OverflowPolicy::DropNewest};
    std::atomic<std::uint64_t> dropped_count_{0};

public:
    explicit SpscShardRing(OverflowPolicy policy = OverflowPolicy::DropNewest) noexcept
        : overflow_(policy) {}

    SpscShardRing(const SpscShardRing&) = delete;
    SpscShardRing& operator=(const SpscShardRing&) = delete;
    SpscShardRing(SpscShardRing&&) = delete;
    SpscShardRing& operator=(SpscShardRing&&) = delete;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    /// Producer hot path: try to push. Returns false on DropNewest drop.
    /// OverwriteOldest always succeeds (overwrites oldest).
    [[nodiscard]] bool try_push(const T& value) noexcept {
        const std::uint64_t h = head_.load(std::memory_order_relaxed);
        const std::uint64_t t = tail_.load(std::memory_order_acquire);

        if (h - t >= Capacity) {
            if (overflow_ == OverflowPolicy::DropNewest) {
                dropped_count_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            // OverwriteOldest: advance tail to free the oldest slot; the
            // overwritten record counts as a drop.
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            tail_.store(t + 1, std::memory_order_release);
        }

        slots_[h & (Capacity - 1)] = value;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    /// Consumer hot path: try to pop. Returns false when empty.
    [[nodiscard]] bool try_pop(T& out) noexcept {
        const std::uint64_t t = tail_.load(std::memory_order_relaxed);
        const std::uint64_t h = head_.load(std::memory_order_acquire);
        if (t == h) {
            return false;
        }
        out = slots_[t & (Capacity - 1)];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool is_empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_full() const noexcept {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire) >= Capacity;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t dropped_count() const noexcept {
        return dropped_count_.load(std::memory_order_relaxed);
    }

    void set_overflow_policy(OverflowPolicy p) noexcept { overflow_ = p; }
    [[nodiscard]] OverflowPolicy overflow_policy() const noexcept { return overflow_; }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "SpscShardRing(capacity=" + std::to_string(Capacity) +
               ", size=" + std::to_string(size()) +
               ", dropped=" + std::to_string(dropped_count()) +
               ", overflow=" + std::string(nexigen::logging::to_string(overflow_)) + ")";
    }
};

/// Free helper (mandate 9).
template <class T, std::size_t Capacity>
[[nodiscard]] std::string to_string(const SpscShardRing<T, Capacity>& ring) {
    return ring.to_string();
}

} // namespace nexigen::logging::detail
