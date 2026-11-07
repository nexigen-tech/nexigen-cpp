// nexigen::core — shared per-thread sharded SPSC pool.
//
// Both the logging Producer and the tracing TracerProvider build the same
// multi-producer machinery: a fixed array of SPSC rings (one per thread, up to
// MaxThreads), per-shard accounting cells, a slot-registration cell with a
// thread_local claim cache, a pool-exhaustion counter, and a single-consumer
// round-robin drain. ShardPool is that machinery, factored out of the two
// hot paths so the wait-free invariants and memory orders live in exactly one
// place.
//
// WAIT-FREE CONTRACT: exactly MaxThreads distinct producer threads may call
// claim_slot()/ring()/state() on one pool; each owns one ring. The single
// consumer calls pop(). Threads beyond the pool are dropped and counted
// (slot_exhausted) — never blocked.
//
// MEMORY ORDERS: registration fetch_add relaxed + release high-water publish,
// SPSC release/acquire inside the rings, drop counters relaxed. The
// ShardPoolState cells and the registration/exhaustion cells each own whole
// cache lines (alignas(64)) to avoid false sharing.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>

#include <nexigen/core/result.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>

namespace nexigen::core {

/// Per-shard accounting + throttle window. The two counters commit
/// increments ONLY by the slot's owner via a single relaxed atomic
/// `fetch_add` (never `store(load+1)` — the consumer resets cells it has
/// reported, so each increment must survive as an atomic RMW); the window
/// fields are plain single-writer state. One cache line per shard.
struct alignas(64) ShardPoolState {
    std::atomic<std::uint64_t> rate_limited{}; ///< owner fetch_add, others load
    std::atomic<std::uint64_t> buffer_full{};  ///< owner fetch_add, others load
    std::uint64_t window_start_ns{};           ///< owner-only plain state
    std::uint64_t tokens_used{};               ///< owner-only plain state
    std::uint8_t pad_[32]{};
};
static_assert(sizeof(ShardPoolState) % 64 == 0, "shard pool state owns whole cache lines");

/// The shared shard pool (see file header for the full contract).
template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
class alignas(64) ShardPool {
    static_assert(ShardCapacity >= 1 && (ShardCapacity & (ShardCapacity - 1)) == 0,
                  "shard capacity must be a positive power of two");
    static_assert(MaxThreads >= 1, "shard pool must hold at least one slot");

public:
    ShardPool() = default;
    ShardPool(const ShardPool&) = delete;
    ShardPool& operator=(const ShardPool&) = delete;
    ShardPool(ShardPool&&) = delete;
    ShardPool& operator=(ShardPool&&) = delete;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept { return MaxThreads; }

    /// Producer hot path: resolve this thread's shard slot. thread_local
    /// cached; the first call per (thread, this) claims a slot with one relaxed
    /// fetch_add plus a release high-water publish, then caches it. A cached
    /// slot is re-validated against the high-water mark so a pool destroyed
    /// and re-created at the same address never hands out a slot the consumer
    /// cannot drain. Returns -1 when the pool is exhausted (already counted).
    [[nodiscard]] std::int32_t claim_slot() noexcept;

    /// Producer accessors (owner thread only).
    [[nodiscard]] core::SpscRingBuffer<Record, ShardCapacity>& ring(std::size_t slot) noexcept {
        return shards_[slot];
    }
    [[nodiscard]] const core::SpscRingBuffer<Record, ShardCapacity>&
    ring(std::size_t slot) const noexcept {
        return shards_[slot];
    }
    [[nodiscard]] ShardPoolState& state(std::size_t slot) noexcept { return state_[slot]; }

    /// Registered high-water mark (acquire): the consumer bounds a sweep by it.
    [[nodiscard]] std::uint32_t active() const noexcept {
        return registration_.active.load(std::memory_order_acquire);
    }

    /// Consumer end (single worker): round-robin with a 16-record slice per
    /// shard per sweep (one flooded shard cannot starve the others); a shard
    /// skipped at its cap is retried once before any "empty" verdict.
    /// BufferEmpty when every registered shard is momentarily empty.
    [[nodiscard]] core::Status pop(Record& out) noexcept;

    // -- third-thread relaxed snapshots (diagnostics, never synchronization)
    [[nodiscard]] std::size_t ring_size() const noexcept;
    [[nodiscard]] bool ring_empty() const noexcept;
    [[nodiscard]] std::uint64_t rate_limited_total() const noexcept;
    [[nodiscard]] std::uint64_t buffer_full_total() const noexcept;
    [[nodiscard]] std::uint64_t slot_exhausted() const noexcept {
        return exhausted_.slot_exhausted.load(std::memory_order_relaxed);
    }

    /// Cold-path test/diagnostic accessor for one shard (out-of-range → 0).
    [[nodiscard]] core::SpscRingBuffer<Record, ShardCapacity>& shard(std::size_t index) noexcept {
        return shards_[index < MaxThreads ? index : 0U];
    }

private:
    struct alignas(64) RegistrationCell {
        std::atomic<std::uint32_t> next_slot{}; ///< relaxed fetch_add
        std::atomic<std::uint32_t> active{};    ///< release fetch_max (CAS loop)
        std::uint8_t pad_[56]{};
    };
    struct alignas(64) ExhaustedCell {
        std::atomic<std::uint64_t> slot_exhausted{};
        std::uint8_t pad_[56]{};
    };

    alignas(64) std::array<core::SpscRingBuffer<Record, ShardCapacity>, MaxThreads> shards_{};
    ShardPoolState state_[MaxThreads]{};
    RegistrationCell registration_{};
    ExhaustedCell exhausted_{};

    /// Consumer-only round-robin cursor (the worker is the sole pop() caller).
    std::uint32_t drain_shard_{};
    std::uint32_t drain_shard_pops_{};

    static_assert(sizeof(RegistrationCell) == 64);
    static_assert(sizeof(ExhaustedCell) == 64);
};

// --- out-of-line definitions ------------------------------------------------

template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
std::int32_t ShardPool<Record, ShardCapacity, MaxThreads>::claim_slot() noexcept {
    struct Entry {
        const void* owner{nullptr};
        std::int32_t slot{-1};
    };
    // Per-thread (pool → slot) cache. 8 distinct pools per thread: the
    // realistic population is logging · metrics · tracing · chrono plus
    // headroom overflow — analysis #10 showed the old size-4 map pushed 5+-
    // pool threads into a per-record global-slot burn.
    thread_local Entry tls[8]{}; // 8 distinct pools per thread
    for (Entry& entry : tls) {
        if (entry.owner == this) {
            // A pool destroyed and re-created at the same address leaves a
            // stale TLS entry; its new registration has not published any slot
            // yet, so treat the entry as stale and re-register instead of
            // returning a slot the worker cannot drain.
            if (entry.slot >= 0 && static_cast<std::uint32_t>(entry.slot) <
                                       registration_.active.load(std::memory_order_acquire)) {
                return entry.slot; // steady path: TLS only, no shared atomics
            }
            entry.owner = nullptr;
            entry.slot = -1;
            break;
        }
    }
    // Overflow guard (spec logging/multi-producer "Thread-local slot
    // registration", TLS-cache-exhaustion scenario): a thread logged into
    // more distinct pools than its cache holds must NOT register per record
    // — the old path burned one global slot number per emit until the pool
    // was exhausted and every record dropped silently. The counted
    // slot_exhausted drop is the honest bounded behavior; previously cached
    // pools on this thread keep their wait-free steady path.
    bool cache_free = false;
    for (const Entry& entry : tls) {
        if (entry.owner == nullptr) {
            cache_free = true;
            break;
        }
    }
    if (!cache_free) {
        exhausted_.slot_exhausted.fetch_add(1U, std::memory_order_relaxed);
        return -1; // TLS-cache overflow: counted drop (never blocks)
    }
    const std::uint32_t claimed = registration_.next_slot.fetch_add(1U, std::memory_order_relaxed);
    if (claimed >= MaxThreads) {
        exhausted_.slot_exhausted.fetch_add(1U, std::memory_order_relaxed);
        return -1; // dropped: no slot (never blocks)
    }
    // Publish the high-water mark (release): the consumer's acquire-load of
    // active() must not order *before* the slot it will drain exists.
    std::uint32_t current = registration_.active.load(std::memory_order_relaxed);
    while (claimed + 1U > current &&
           !registration_.active.compare_exchange_weak(
               current, claimed + 1U, std::memory_order_release, std::memory_order_relaxed)) {
        // bounded: current strictly increases per retry, ≤ MaxThreads contenders
    }
    for (Entry& entry : tls) {
        if (entry.owner == nullptr) {
            entry.owner = this;
            entry.slot = static_cast<std::int32_t>(claimed);
            break;
        }
    }
    // Cache slot is guaranteed free here (checked above before touching the
    // shared registration).
    return static_cast<std::int32_t>(claimed);
}

template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
core::Status ShardPool<Record, ShardCapacity, MaxThreads>::pop(Record& out) noexcept {
    const std::uint32_t active = registration_.active.load(std::memory_order_acquire);
    bool deferred = false; // a shard was skipped at its 16-record slice
    for (std::uint32_t tried = 0U; tried < active; ++tried) {
        const std::uint32_t shard = drain_shard_;
        if (drain_shard_pops_ >= 16U) {
            deferred = true;
        } else if (shards_[shard].try_pop(out)) {
            ++drain_shard_pops_;
            return {};
        }
        drain_shard_ = active == 0U ? 0U : (shard + 1U) % active;
        drain_shard_pops_ = 0U;
    }
    if (deferred) {
        return pop(out); // one retry: budgets reset, real records surface
    }
    return std::unexpected(core::ErrorCode::BufferEmpty);
}

template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
std::size_t ShardPool<Record, ShardCapacity, MaxThreads>::ring_size() const noexcept {
    std::size_t pending = 0;
    for (const auto& shard : shards_) {
        pending += shard.size();
    }
    return pending;
}

template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
bool ShardPool<Record, ShardCapacity, MaxThreads>::ring_empty() const noexcept {
    for (const auto& shard : shards_) {
        if (!shard.is_empty()) {
            return false;
        }
    }
    return true;
}

template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
std::uint64_t ShardPool<Record, ShardCapacity, MaxThreads>::rate_limited_total() const noexcept {
    std::uint64_t total = 0;
    for (const ShardPoolState& state : state_) {
        total += state.rate_limited.load(std::memory_order_relaxed);
    }
    return total;
}

template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
std::uint64_t ShardPool<Record, ShardCapacity, MaxThreads>::buffer_full_total() const noexcept {
    std::uint64_t total = 0;
    for (const ShardPoolState& state : state_) {
        total += state.buffer_full.load(std::memory_order_relaxed);
    }
    return total;
}

} // namespace nexigen::core
