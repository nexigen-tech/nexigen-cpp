// nexigen::logging — pre-allocated per-thread shard pool with lifecycle
// (change rework-logging-hot-path-engine, design D3/D9; spec
// logging/spsc-shard-pool).
//
// `SpscShardPool` pre-allocates `max_producer_threads` shard rings at
// initialization. Each producer thread claims a slot via `thread_local`
// registration (one relaxed fetch_add). Pool exhaustion counts
// `unregistered_thread_drops_` without blocking or allocating. Thread exit
// transitions the shard to `Draining`; the consumer drains it and returns
// it to the free-slot pool.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <nexigen/logging/detail/spsc_shard_ring.hpp>
#include <nexigen/logging/logger_options.hpp>

namespace nexigen::logging::detail {

/// Per-shard lifecycle state.
enum class ShardState : std::uint8_t {
    Active,   ///< Registered and accepting records.
    Draining, ///< Producer thread exited; consumer must drain then free.
    Free,     ///< Returned to the free-slot pool.
};

/// One shard slot: ring + state + registration.
struct alignas(64) ShardSlot {
    SpscShardRing<std::uint64_t, 256> ring{};  ///< placeholder T; real T set by pool
    std::atomic<ShardState> state{ShardState::Free};
    std::atomic<bool> registered{false};
    std::uint8_t pad_[64]{};
};

/// Pre-allocated shard pool with thread-local registration and lifecycle.
class SpscShardPool {
    std::vector<ShardSlot> slots_;
    std::atomic<std::uint32_t> next_slot_{0};
    std::atomic<std::uint32_t> active_count_{0};
    std::atomic<std::uint64_t> unregistered_thread_drops_{0};
    std::uint32_t max_threads_;

public:
    explicit SpscShardPool(std::uint32_t max_threads) noexcept
        : slots_(max_threads), max_threads_(max_threads) {}

    SpscShardPool(const SpscShardPool&) = delete;
    SpscShardPool& operator=(const SpscShardPool&) = delete;
    SpscShardPool(SpscShardPool&&) = delete;
    SpscShardPool& operator=(SpscShardPool&&) = delete;

    [[nodiscard]] std::uint32_t max_threads() const noexcept { return max_threads_; }
    [[nodiscard]] std::uint32_t active_count() const noexcept {
        return active_count_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t unregistered_thread_drops() const noexcept {
        return unregistered_thread_drops_.load(std::memory_order_relaxed);
    }

    /// Register the calling thread; returns slot index or -1 on exhaustion.
    [[nodiscard]] std::int32_t register_thread() noexcept {
        const std::uint32_t slot = next_slot_.fetch_add(1, std::memory_order_relaxed);
        if (slot >= max_threads_) {
            unregistered_thread_drops_.fetch_add(1, std::memory_order_relaxed);
            return -1;
        }
        slots_[slot].state.store(ShardState::Active, std::memory_order_release);
        slots_[slot].registered.store(true, std::memory_order_release);
        active_count_.fetch_add(1, std::memory_order_relaxed);
        return static_cast<std::int32_t>(slot);
    }

    /// Transition a shard to Draining (called by thread_local destructor).
    void mark_draining(std::uint32_t slot) noexcept {
        if (slot < max_threads_) {
            slots_[slot].state.store(ShardState::Draining, std::memory_order_release);
        }
    }

    /// Return a drained shard to the free pool (consumer side).
    void release_shard(std::uint32_t slot) noexcept {
        if (slot < max_threads_) {
            slots_[slot].state.store(ShardState::Free, std::memory_order_release);
            slots_[slot].registered.store(false, std::memory_order_release);
            active_count_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    /// Access a shard's ring by slot index.
    [[nodiscard]] ShardSlot& shard(std::uint32_t slot) noexcept { return slots_[slot]; }
    [[nodiscard]] const ShardSlot& shard(std::uint32_t slot) const noexcept { return slots_[slot]; }

    /// Iterate all shards; consumer calls this to drain.
    [[nodiscard]] ShardSlot* data() noexcept { return slots_.data(); }
    [[nodiscard]] const ShardSlot* data() const noexcept { return slots_.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return slots_.size(); }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "SpscShardPool(max_threads=" + std::to_string(max_threads_) +
               ", active=" + std::to_string(active_count()) +
               ", unregistered_drops=" + std::to_string(unregistered_thread_drops()) + ")";
    }
};

/// Free helper (mandate 9).
[[nodiscard]] inline std::string to_string(const SpscShardPool& pool) {
    return pool.to_string();
}

} // namespace nexigen::logging::detail
