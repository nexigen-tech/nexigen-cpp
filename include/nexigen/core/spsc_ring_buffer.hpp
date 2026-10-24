// nexigen::core — wait-free single-producer / single-consumer bounded queue
// (design.md D3).
//
// CONTRACT — exactly one producer thread and one consumer thread call through
// the same SpscRingBuffer object, each exclusively from one end (try_push from
// the producer, try_pop from the consumer). Any other thread topology (two
// producers, two consumers, the same thread on both ends across calls with
// interleaving other threads) is outside the contract, not a supported mode.
// size()/is_empty()/is_full() may be called by third threads but return
// SNAPSHOTS: an observation, not a linearizable reading — control-plane and
// diagnostics use only, never hot-path synchronization.
//
// WAIT-FREE — every try_push/try_pop completes in a bounded number of steps
// determined by the operation alone (1-2 loads plus 1 store, no CAS loop):
// neither endpoint can be stalled, spin-waited, or blocked by the other.
//
// HOT PATH — no locks, no blocking primitives, no heap allocation: storage is
// a fixed inline std::array sized at compile time. The producer's tail and
// the consumer's head are monotonically increasing size_t indices; full and
// empty are distinguished by their difference, so all Capacity slots are
// usable. Each index cell owns its own cache line (alignas(64)); the private
// peer-index mirrors (head_cache/tail_cache) mean the common path never
// touches the other thread's cache line at all.
//
// The class is non-copyable and non-movable (atomics + per-thread caches):
// instances live in static/global storage or inside long-lived objects.
//
// Usage rule (see result.hpp): never call value() on the returned Status;
// branch on has_value() and read the failure via .error().
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>

#include <nexigen/core/result.hpp>

namespace nexigen::core {

/// Wait-free SPSC ring buffer with compile-time fixed, power-of-two capacity.
/// One producer thread (try_push), one consumer thread (try_pop). See the
/// file header for the full thread-safety contract.
template <class T, std::size_t Capacity>
class alignas(64) SpscRingBuffer {
    static_assert(Capacity >= 1 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a positive power of two");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "index atomics must be lock-free");
    static_assert(std::is_default_constructible_v<T> && std::is_move_assignable_v<T>,
                  "T must be default-constructible and move-assignable");

    /// Mask for mapping monotonic indices onto the fixed slot array.
    static constexpr std::size_t mask_ = Capacity - 1;

    /// Producer-owned cache line: tail is written only by the producer thread;
    /// head_cache is its private mirror of the consumer's head (plain data).
    struct alignas(64) Producer_cell {
        std::atomic<std::size_t> tail{0}; ///< One past the newest stored element.
        std::size_t head_cache{0};        ///< Producer's cached view of consumer head.
    } producer_{};

    /// Consumer-owned cache line: head is written only by the consumer thread;
    /// tail_cache is its private mirror of the producer's tail (plain data).
    struct alignas(64) Consumer_cell {
        std::atomic<std::size_t> head{0}; ///< Oldest not-yet-consumed element.
        std::size_t tail_cache{0};        ///< Consumer's cached view of producer tail.
    } consumer_{};

    /// Fixed payload storage; written by the producer, read by the consumer,
    /// ordered by the index release/acquire pairs below. A popped-from slot
    /// holds a valid moved-from value until the next overwrite.
    alignas(64) std::array<T, Capacity> slots_{};

    // Layout self-verification: if a refactor ever collapses the two index
    // cells onto one cache line, fail compilation instead of silently
    // reintroducing false sharing.
    static_assert(alignof(Producer_cell) == 64, "producer cell must own a cache line");
    static_assert(alignof(Consumer_cell) == 64, "consumer cell must own a cache line");
    static_assert(sizeof(Producer_cell) % 64 == 0, "producer cell must be a whole number of lines");
    static_assert(sizeof(Consumer_cell) % 64 == 0, "consumer cell must be a whole number of lines");

public:
    SpscRingBuffer() = default;
    ~SpscRingBuffer() = default;

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;
    SpscRingBuffer(SpscRingBuffer&&) = delete;
    SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;

    /// Compile-time power-of-two predicate (also used by the static_assert above).
    [[nodiscard]] static constexpr bool is_power_of_two(std::size_t value) noexcept {
        return value >= 1 && (value & (value - 1)) == 0;
    }

    /// Fixed capacity, known at compile time.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    /// Producer end only. Inserts at the back; BufferFull when all Capacity
    /// slots are occupied (the pushed element is not taken into the queue).
    /// Wait-free: the cached peer index keeps the common path on the
    /// producer's own cache line; an apparent full refreshes once (acquire)
    /// and re-checks, so there is no spurious failure.
    [[nodiscard]] Status try_push(const T& value) noexcept {
        const std::size_t t = producer_.tail.load(std::memory_order_relaxed);
        if (t - producer_.head_cache >= Capacity) {
            producer_.head_cache = consumer_.head.load(std::memory_order_acquire);
            if (t - producer_.head_cache >= Capacity) {
                return std::unexpected(ErrorCode::BufferFull);
            }
        }
        slots_[t & mask_] = value;
        producer_.tail.store(t + 1, std::memory_order_release); // publishes the slot write
        return {};
    }

    /// Producer end only. Move-inserting overload; same semantics as above.
    [[nodiscard]] Status try_push(T&& value) noexcept {
        const std::size_t t = producer_.tail.load(std::memory_order_relaxed);
        if (t - producer_.head_cache >= Capacity) {
            producer_.head_cache = consumer_.head.load(std::memory_order_acquire);
            if (t - producer_.head_cache >= Capacity) {
                return std::unexpected(ErrorCode::BufferFull);
            }
        }
        slots_[t & mask_] = std::move(value);
        producer_.tail.store(t + 1, std::memory_order_release); // publishes the slot write
        return {};
    }

    /// Consumer end only. Removes from the front into `out` (moved out);
    /// BufferEmpty when no element is available (`out` is left untouched).
    /// The acquire reload orders these reads after the producer's release
    /// store of tail, making the pushed element fully visible here.
    [[nodiscard]] Status try_pop(T& out) noexcept {
        const std::size_t h = consumer_.head.load(std::memory_order_relaxed);
        if (h == consumer_.tail_cache) {
            consumer_.tail_cache = producer_.tail.load(std::memory_order_acquire);
            if (h == consumer_.tail_cache) {
                return std::unexpected(ErrorCode::BufferEmpty);
            }
        }
        out = std::move(slots_[h & mask_]);
        consumer_.head.store(h + 1, std::memory_order_release); // releases the slot for reuse
        return {};
    }

    /// Snapshot count of stored elements (diagnostics only, not linearizable).
    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t t = producer_.tail.load(std::memory_order_relaxed);
        const std::size_t h = consumer_.head.load(std::memory_order_relaxed);
        return t - h;
    }

    /// Snapshot emptiness check (diagnostics only).
    [[nodiscard]] bool is_empty() const noexcept { return size() == 0; }

    /// Snapshot fullness check (diagnostics only).
    [[nodiscard]] bool is_full() const noexcept { return size() == Capacity; }
};

} // namespace nexigen::core
