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
#include <string>
#include <type_traits>
#include <utility>

#include <nexigen/core/result.hpp>

namespace nexigen::core {

/// @brief Wait-free SPSC ring buffer with compile-time fixed, power-of-two
///        capacity.
///
/// Exactly one producer thread calls `try_push` and exactly one consumer thread
/// calls `try_pop` through the same object; any other topology is outside the
/// contract. Storage is a fixed inline array (no heap), the index atomics are
/// lock-free, and each endpoint keeps a private mirror of the peer index so the
/// common path never touches the other thread's cache line. The snapshot
/// accessors may be called from third threads but are not linearizable:
/// diagnostics only.
///
/// @tparam T Element type; `static_assert` requires
///         `std::is_default_constructible_v<T>` and
///         `std::is_move_assignable_v<T>`.
/// @tparam Capacity Number of slots; `static_assert` requires a positive power
///         of two.
/// @note Spec `core/spsc-ring-buffer`, requirements "Fixed-capacity queue
///       declared at compile time" and "Single-producer single-consumer
///       contract".
template <class T, std::size_t Capacity> class alignas(64) SpscRingBuffer {
    static_assert(Capacity >= 1 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a positive power of two");
    static_assert(std::atomic<std::size_t>::is_always_lock_free, "index atomics must be lock-free");
    static_assert(std::is_default_constructible_v<T> && std::is_move_assignable_v<T>,
                  "T must be default-constructible and move-assignable");

    /// @brief Mask mapping monotonic indices onto the fixed slot array.
    static constexpr std::size_t mask_ = Capacity - 1;

    /// @brief Producer-owned cache line.
    ///
    /// The producer thread alone writes `tail`; `head_cache` is its private,
    /// plain-data mirror of the consumer's head.
    struct alignas(64) ProducerCell {
        std::atomic<std::size_t> tail{0}; ///< One past the newest stored element.
        std::size_t head_cache{0};        ///< Producer's cached view of consumer head.
    } producer_{};

    /// @brief Consumer-owned cache line.
    ///
    /// The consumer thread alone writes `head`; `tail_cache` is its private,
    /// plain-data mirror of the producer's tail.
    struct alignas(64) ConsumerCell {
        std::atomic<std::size_t> head{0}; ///< Oldest not-yet-consumed element.
        std::size_t tail_cache{0};        ///< Consumer's cached view of producer tail.
    } consumer_{};

    /// @brief Fixed payload storage.
    ///
    /// Written by the producer and read by the consumer, ordered by the index
    /// release/acquire pairs. A popped-from slot holds a valid moved-from value
    /// until the next overwrite.
    alignas(64) std::array<T, Capacity> slots_{};

    // Layout self-verification: if a refactor ever collapses the two index
    // cells onto one cache line, fail compilation instead of silently
    // reintroducing false sharing.
    static_assert(alignof(ProducerCell) == 64, "producer cell must own a cache line");
    static_assert(alignof(ConsumerCell) == 64, "consumer cell must own a cache line");
    static_assert(sizeof(ProducerCell) % 64 == 0, "producer cell must be a whole number of lines");
    static_assert(sizeof(ConsumerCell) % 64 == 0, "consumer cell must be a whole number of lines");

public:
    /// @brief Construct an empty queue with zeroed indices and slots.
    SpscRingBuffer() = default;
    /// @brief Destroy the queue; no allocation is released.
    ~SpscRingBuffer() = default;

    /// @brief Copy construction is deleted: endpoints hold unique ownership.
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    /// @brief Copy assignment is deleted: endpoints hold unique ownership.
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;
    /// @brief Move construction is deleted: the atomics are address-bound.
    SpscRingBuffer(SpscRingBuffer&&) = delete;
    /// @brief Move assignment is deleted: the atomics are address-bound.
    SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;

    /// @brief Compile-time power-of-two predicate.
    ///
    /// @param value Candidate capacity.
    /// @return `true` when `value` is at least 1 and has exactly one bit set.
    /// @note Also used by the `static_assert` above and the spec's
    ///       "Rejecting a non-power-of-two capacity" scenario.
    [[nodiscard]] static constexpr bool is_power_of_two(std::size_t value) noexcept {
        return value >= 1 && (value & (value - 1)) == 0;
    }

    /// @brief Fixed capacity, known at compile time.
    ///
    /// @return The `Capacity` template argument.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    /// @brief Insert a copy at the back (producer end only).
    ///
    /// Wait-free: the cached peer index keeps the common path on the producer's
    /// own cache line; an apparent full state refreshes once with an acquire
    /// load and re-checks, so there is no spurious failure.
    ///
    /// @param value Element to copy into the queue.
    /// @return `Status{}` when the element was enqueued; `BufferFull` when all
    ///         `Capacity` slots are occupied (the element is not taken into the
    ///         queue).
    /// @note Spec `core/spsc-ring-buffer`, requirement "Non-blocking push and
    ///       pop with classified outcomes".
    /// @warning Only the single producer thread may call this.
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

    /// @brief Move-insert an element at the back (producer end only).
    ///
    /// Same semantics and wait-free behaviour as the copying overload.
    ///
    /// @param value Element to move into the queue.
    /// @return `Status{}` when the element was enqueued; `BufferFull` when all
    ///         `Capacity` slots are occupied (the element is not taken into the
    ///         queue).
    /// @note Spec `core/spsc-ring-buffer`, requirement "Non-blocking push and
    ///       pop with classified outcomes".
    /// @warning Only the single producer thread may call this.
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

    /// @brief Remove the front element into `out` (consumer end only).
    ///
    /// The acquire reload orders these reads after the producer's release store
    /// of `tail`, making the pushed element fully visible here.
    ///
    /// @param out Destination assigned only on success; left untouched when the
    ///        queue is empty.
    /// @return `Status{}` when an element was moved out; `BufferEmpty` when no
    ///         element is available.
    /// @note Spec `core/spsc-ring-buffer`, requirement "Cross-thread visibility
    ///       of transferred elements".
    /// @warning Only the single consumer thread may call this.
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

    /// @brief Snapshot count of stored elements.
    ///
    /// @return `tail - head` read with relaxed loads; not linearizable and valid
    ///         from any thread, for diagnostics only.
    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t t = producer_.tail.load(std::memory_order_relaxed);
        const std::size_t h = consumer_.head.load(std::memory_order_relaxed);
        return t - h;
    }

    /// @brief Snapshot emptiness check (diagnostics only).
    ///
    /// @return `true` when the snapshot count is zero.
    [[nodiscard]] bool is_empty() const noexcept { return size() == 0; }

    /// @brief Snapshot fullness check (diagnostics only).
    ///
    /// @return `true` when the snapshot count equals `Capacity`.
    [[nodiscard]] bool is_full() const noexcept { return size() == Capacity; }

    /// @brief Cold-path diagnostic snapshot: capacity, occupancy, state.
    ///
    /// Reads the same relaxed counters as `size()`, so a third thread may call
    /// it; the returned `std::string` allocates, which is exactly why it is a
    /// diagnostics helper and never a hot-path primitive.
    ///
    /// @return Text such as `SpscRingBuffer(capacity=8, size=3, partial)`.
    /// @note Spec `core/spsc-ring-buffer`, requirement "Ring buffer diagnostic
    ///       string"; mandate 9.
    [[nodiscard]] std::string to_string() const {
        const std::size_t n = size();
        std::string out = "SpscRingBuffer(capacity=";
        out += std::to_string(Capacity);
        out += ", size=";
        out += std::to_string(n);
        out += ", ";
        out += (n == 0U ? "empty" : (n == Capacity ? "full" : "partial"));
        out += ")";
        return out;
    }
};

/// @brief Free mirror of the member `to_string` (mandate 9).
///
/// @tparam T Element type of the queue.
/// @tparam Capacity Slot count of the queue.
/// @param ring Queue to render.
/// @return The same text as `ring.to_string()`.
/// @note Spec `core/spsc-ring-buffer`, requirement "Ring buffer diagnostic
///       string".
template <class T, std::size_t Capacity>
[[nodiscard]] std::string to_string(const SpscRingBuffer<T, Capacity>& ring) {
    return ring.to_string();
}

} // namespace nexigen::core
