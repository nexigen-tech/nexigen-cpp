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

/// @brief Per-shard accounting plus throttle window.
///
/// The slot's owner commits increments of the two counters with a single
/// relaxed atomic `fetch_add` (never `store(load + 1)`: the consumer resets
/// cells it has reported, so each increment must survive as an atomic RMW). The
/// window fields are plain single-writer state. One cache line per shard.
///
/// @note The composition follows Spec `core/spsc-ring-buffer`, requirement
///       "Single-producer single-consumer contract": each cell belongs to one
///       producer slot.
struct alignas(64) ShardPoolState {
    std::atomic<std::uint64_t> rate_limited{}; ///< owner fetch_add, others load
    std::atomic<std::uint64_t> buffer_full{};  ///< owner fetch_add, others load
    std::uint64_t window_start_ns{};           ///< owner-only plain state
    std::uint64_t tokens_used{};               ///< owner-only plain state
    std::uint8_t pad_[32]{};                   ///< Padding so each cell owns whole cache lines.
};
static_assert(sizeof(ShardPoolState) % 64 == 0, "shard pool state owns whole cache lines");

/// @brief Shared shard pool (see the file header for the full contract).
///
/// A fixed array of SPSC rings (one per registered thread), per-shard accounting
/// cells, a slot-registration cell with a thread-local claim cache, a
/// pool-exhaustion counter, and a single-consumer round-robin drain. At most
/// `MaxThreads` distinct producer threads call `claim_slot()`/`ring()`/
/// `state()`, each owning one ring; the single consumer calls `pop()`. Threads
/// beyond the pool are dropped and counted, never blocked.
///
/// @tparam Record Element type stored in each shard ring.
/// @tparam ShardCapacity Slots per ring; `static_assert` requires a positive
///         power of two.
/// @tparam MaxThreads Maximum number of producer slots; `static_assert`
///         requires at least one.
/// @note Spec `core/spsc-ring-buffer`, requirement "Single-producer
///       single-consumer contract" applies per shard; the pool adds the
///       multi-producer registration layer on top.
template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
class alignas(64) ShardPool {
    static_assert(ShardCapacity >= 1 && (ShardCapacity & (ShardCapacity - 1)) == 0,
                  "shard capacity must be a positive power of two");
    static_assert(MaxThreads >= 1, "shard pool must hold at least one slot");

public:
    /// @brief Default-construct an empty pool with no registered slots.
    ShardPool() = default;
    /// @brief Copy construction is deleted: shards hold unique ownership.
    ShardPool(const ShardPool&) = delete;
    /// @brief Copy assignment is deleted: shards hold unique ownership.
    ShardPool& operator=(const ShardPool&) = delete;
    /// @brief Move construction is deleted: atomics are address-bound.
    ShardPool(ShardPool&&) = delete;
    /// @brief Move assignment is deleted: atomics are address-bound.
    ShardPool& operator=(ShardPool&&) = delete;

    /// @brief Compile-time capacity of each shard ring.
    ///
    /// @return The `ShardCapacity` template argument.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    /// @brief Compile-time number of producer slots.
    ///
    /// @return The `MaxThreads` template argument.
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept { return MaxThreads; }

    /// @brief Resolve this thread's shard slot (producer hot path).
    ///
    /// `thread_local` cached: the first call per (thread, pool) claims a slot
    /// with one relaxed `fetch_add` plus a release high-water publish, then
    /// caches it. A cached slot is re-validated against the high-water mark so a
    /// pool destroyed and re-created at the same address never hands out a slot
    /// the consumer cannot drain.
    ///
    /// @return Slot index in `[0, MaxThreads)`; `-1` when the pool (or this
    ///         thread's TLS cache) is exhausted, already counted in
    ///         `slot_exhausted()`.
    /// @note Spec `core/spsc-ring-buffer`, requirement "Single-producer
    ///       single-consumer contract"; each returned slot is drained by exactly
    ///       one consumer.
    /// @warning Never blocks: exhaustion is a counted drop.
    [[nodiscard]] std::int32_t claim_slot() noexcept;

    /// @brief Ring belonging to `slot` (producer accessor).
    ///
    /// @param slot Slot index previously returned by `claim_slot()`.
    /// @return Reference to that slot's ring; the owner thread is its only
    ///         producer.
    /// @note Spec `core/spsc-ring-buffer`, requirement "Single-producer
    ///       single-consumer contract".
    /// @warning Owner thread only; no bounds check is performed.
    [[nodiscard]] core::SpscRingBuffer<Record, ShardCapacity>& ring(std::size_t slot) noexcept {
        return shards_[slot];
    }
    /// @brief Const ring access for diagnostics.
    ///
    /// @param slot Slot index previously returned by `claim_slot()`.
    /// @return Const reference to that slot's ring.
    /// @warning Cold-path diagnostics only; no bounds check is performed.
    [[nodiscard]] const core::SpscRingBuffer<Record, ShardCapacity>&
    ring(std::size_t slot) const noexcept {
        return shards_[slot];
    }
    /// @brief Accounting cell belonging to `slot` (owner thread only).
    ///
    /// @param slot Slot index previously returned by `claim_slot()`.
    /// @return Reference to that slot's accounting cell.
    /// @warning Owner thread writes it; other threads may only load the
    ///          counters.
    [[nodiscard]] ShardPoolState& state(std::size_t slot) noexcept { return state_[slot]; }

    /// @brief Registered high-water mark of producer slots.
    ///
    /// Read with acquire ordering; the consumer bounds its sweep by it.
    ///
    /// @return Number of slots that may currently hold records.
    [[nodiscard]] std::uint32_t active() const noexcept {
        return registration_.active.load(std::memory_order_acquire);
    }

    /// @brief Pop one record round-robin across registered shards (consumer).
    ///
    /// Gives each shard a 16-record slice per sweep so one flooded shard cannot
    /// starve the others; a shard skipped at its cap is retried once before any
    /// "empty" verdict.
    ///
    /// @param out Destination assigned only on success.
    /// @return `Status{}` with one record moved into `out`; `BufferEmpty` when
    ///         every registered shard is momentarily empty.
    /// @note Spec `core/spsc-ring-buffer`, requirement "First-in/first-out
    ///       ordering and element integrity" is preserved per shard.
    /// @warning Only the single consumer thread may call this.
    [[nodiscard]] core::Status pop(Record& out) noexcept;

    // -- third-thread relaxed snapshots (diagnostics, never synchronization)
    /// @brief Total records currently pending across every shard.
    ///
    /// @return Sum of the per-shard snapshot sizes; relaxed, not linearizable.
    [[nodiscard]] std::size_t ring_size() const noexcept;
    /// @brief Whether every shard snapshot is empty.
    ///
    /// @return `true` when no shard reports a pending record.
    [[nodiscard]] bool ring_empty() const noexcept;
    /// @brief Sum of the per-shard rate-limited drop counters.
    ///
    /// @return Counted rate-limit rejections across all shards.
    [[nodiscard]] std::uint64_t rate_limited_total() const noexcept;
    /// @brief Sum of the per-shard buffer-full drop counters.
    ///
    /// @return Counted full-ring rejections across all shards.
    [[nodiscard]] std::uint64_t buffer_full_total() const noexcept;
    /// @brief Records dropped because no producer slot was available.
    ///
    /// @return Counted slot-exhaustion drops across the pool's lifetime.
    [[nodiscard]] std::uint64_t slot_exhausted() const noexcept {
        return exhausted_.slot_exhausted.load(std::memory_order_relaxed);
    }

    /// @brief Cold-path test/diagnostic accessor for one shard.
    ///
    /// @param index Shard index; out-of-range values fall back to shard 0.
    /// @return Reference to the selected shard ring.
    [[nodiscard]] core::SpscRingBuffer<Record, ShardCapacity>& shard(std::size_t index) noexcept {
        return shards_[index < MaxThreads ? index : 0U];
    }

private:
    /// @brief Registration state: next slot to hand out and the active mark.
    struct alignas(64) RegistrationCell {
        std::atomic<std::uint32_t> next_slot{}; ///< Relaxed `fetch_add` counter.
        std::atomic<std::uint32_t> active{};    ///< Release `fetch_max` (CAS loop).
        std::uint8_t pad_[56]{};                ///< Padding to one cache line.
    };
    /// @brief Pool-exhaustion counter, isolated on its own cache line.
    struct alignas(64) ExhaustedCell {
        std::atomic<std::uint64_t> slot_exhausted{}; ///< Relaxed counted drops.
        std::uint8_t pad_[56]{};                     ///< Padding to one cache line.
    };

    alignas(64) std::array<core::SpscRingBuffer<Record, ShardCapacity>,
                           MaxThreads> shards_{}; ///< Per-slot SPSC rings.
    ShardPoolState state_[MaxThreads]{};          ///< Per-slot accounting cells.
    RegistrationCell registration_{};             ///< Slot registration state.
    ExhaustedCell exhausted_{};                   ///< Pool-exhaustion counter.

    /// @brief Consumer-only round-robin cursor (the sole `pop()` caller).
    std::uint32_t drain_shard_{};
    /// @brief Records drained from `drain_shard_` in the current sweep.
    std::uint32_t drain_shard_pops_{};

    static_assert(sizeof(RegistrationCell) == 64);
    static_assert(sizeof(ExhaustedCell) == 64);
};

// --- out-of-line definitions ------------------------------------------------

/// @brief Resolve this thread's shard slot (out-of-line definition).
///
/// @tparam Record Element type stored in each shard ring.
/// @tparam ShardCapacity Slots per ring.
/// @tparam MaxThreads Maximum number of producer slots.
/// @return Slot index in `[0, MaxThreads)`, or `-1` on exhaustion (counted).
/// @note Steady path touches TLS only: no shared atomics after the first claim.
template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
std::int32_t ShardPool<Record, ShardCapacity, MaxThreads>::claim_slot() noexcept {
    /// @brief One thread-local (pool, slot) cache entry.
    struct Entry {
        const void* owner{nullptr}; ///< Pool identity owning the slot.
        std::int32_t slot{-1};      ///< Cached slot, -1 when the entry is free.
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

/// @brief Out-of-line definition of `ShardPool::pop`; see the declaration.
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

/// @brief Sum of the per-shard snapshot sizes (definition).
///
/// @tparam Record Element type stored in each shard ring.
/// @tparam ShardCapacity Slots per ring.
/// @tparam MaxThreads Maximum number of producer slots.
/// @return Records currently pending across every shard.
template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
std::size_t ShardPool<Record, ShardCapacity, MaxThreads>::ring_size() const noexcept {
    std::size_t pending = 0;
    for (const auto& shard : shards_) {
        pending += shard.size();
    }
    return pending;
}

/// @brief Whether every shard snapshot is empty (definition).
///
/// @tparam Record Element type stored in each shard ring.
/// @tparam ShardCapacity Slots per ring.
/// @tparam MaxThreads Maximum number of producer slots.
/// @return `true` when no shard reports a pending record.
template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
bool ShardPool<Record, ShardCapacity, MaxThreads>::ring_empty() const noexcept {
    for (const auto& shard : shards_) {
        if (!shard.is_empty()) {
            return false;
        }
    }
    return true;
}

/// @brief Sum of the per-shard rate-limited counters (definition).
///
/// @tparam Record Element type stored in each shard ring.
/// @tparam ShardCapacity Slots per ring.
/// @tparam MaxThreads Maximum number of producer slots.
/// @return Counted rate-limit rejections across all shards.
template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
std::uint64_t ShardPool<Record, ShardCapacity, MaxThreads>::rate_limited_total() const noexcept {
    std::uint64_t total = 0;
    for (const ShardPoolState& state : state_) {
        total += state.rate_limited.load(std::memory_order_relaxed);
    }
    return total;
}

/// @brief Sum of the per-shard buffer-full counters (definition).
///
/// @tparam Record Element type stored in each shard ring.
/// @tparam ShardCapacity Slots per ring.
/// @tparam MaxThreads Maximum number of producer slots.
/// @return Counted full-ring rejections across all shards.
template <class Record, std::size_t ShardCapacity, std::size_t MaxThreads>
std::uint64_t ShardPool<Record, ShardCapacity, MaxThreads>::buffer_full_total() const noexcept {
    std::uint64_t total = 0;
    for (const ShardPoolState& state : state_) {
        total += state.buffer_full.load(std::memory_order_relaxed);
    }
    return total;
}

} // namespace nexigen::core
