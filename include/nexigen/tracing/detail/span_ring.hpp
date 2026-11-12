// nexigen::tracing — bounded flight-recorder ring for SpanFrame records
// (change add-trace-daemon; spec tracing/trace-daemon "The ring overwrites the
// oldest frames"; design D2).
//
// Pre-allocated at startup and touched by ONE thread (the daemon's drain/dump
// loop): no atomics, no memory orders, no allocations on push. When full, the
// oldest frame is overwritten and the overwrite counter advances.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <nexigen/tracing/span_frame.hpp>

namespace nexigen::tracing::detail {

/// @brief Fixed-capacity ring of `SpanFrame` records with drop-oldest overflow.
///
/// Pre-allocated at startup with exactly `ring_capacity_records` frame slots
/// and touched by ONE thread (the daemon's drain/dump loop): no atomics, no
/// memory orders, and no allocations on `push`. When full, the oldest frame is
/// overwritten and the cumulative overwrite counter advances; retained frames
/// are presented oldest-first.
///
/// @note Spec `tracing/trace-daemon`, requirement "The ring overwrites the
///       oldest frames"; design D2.
class SpanRing {
public:
    /// @brief Construct an empty, unallocated ring.
    ///
    /// A default-constructed ring has capacity zero and `push` is a no-op;
    /// use the capacity constructor for a usable ring.
    SpanRing() = default;

    /// @brief Allocate `capacity` frame slots.
    ///
    /// @param capacity Number of frame slots to own; `0` leaves the ring
    ///        unallocated. The caller (configuration validation) guarantees a
    ///        positive value.
    /// @note Spec `tracing/trace-daemon`, requirement "The ring overwrites the
    ///       oldest frames". Allocation happens here only, never on `push`.
    explicit SpanRing(std::size_t capacity)
        : slots_{capacity > 0U ? std::make_unique<SpanFrame[]>(capacity) : nullptr},
          capacity_{capacity} {}

    /// @brief Copy construction is deleted: the ring owns unique storage.
    SpanRing(const SpanRing&) = delete;
    /// @brief Copy assignment is deleted: the ring owns unique storage.
    SpanRing& operator=(const SpanRing&) = delete;
    /// @brief Move construction transfers the slot buffer and counters.
    SpanRing(SpanRing&&) noexcept = default;
    /// @brief Move assignment transfers the slot buffer and counters.
    SpanRing& operator=(SpanRing&&) noexcept = default;

    /// @brief Configured slot count.
    ///
    /// @return Number of frame slots owned by the ring.
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    /// @brief Retained frame count.
    ///
    /// @return Number of frames currently readable, at most `capacity()`.
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    /// @brief Total frames evicted by overflow since construction.
    ///
    /// @return Cumulative overwrite count; unaffected by `clear()`.
    [[nodiscard]] std::uint64_t overwritten() const noexcept { return overwritten_; }
    /// @brief Test whether the ring retains no frames.
    ///
    /// @return `true` when `count() == 0`.
    [[nodiscard]] bool empty() const noexcept { return count_ == 0U; }

    /// @brief Append one frame, discarding the oldest frame when full.
    ///
    /// Wait-free: a slot store, a modular head advance, and one counter
    /// increment. A zero-capacity ring ignores the call.
    ///
    /// @param frame Frame to copy into the ring.
    /// @note Spec `tracing/trace-daemon`, requirement "The ring overwrites the
    ///       oldest frames". Allocates nothing.
    void push(const SpanFrame& frame) noexcept {
        if (capacity_ == 0U) {
            return;
        }
        slots_[head_] = frame;
        head_ = (head_ + 1U) % capacity_;
        if (count_ < capacity_) {
            ++count_;
        } else {
            ++overwritten_;
        }
    }

    /// @brief Chronological access.
    ///
    /// @param index Position with `index < count()`; `0` is the oldest retained
    ///        frame.
    /// @return Reference to the retained frame at `index` (oldest-first order).
    /// @warning The caller must keep `index < count()`; no bounds check is
    ///          performed on this hot dump path.
    [[nodiscard]] const SpanFrame& at(std::size_t index) const noexcept {
        return slots_[physical(index)];
    }

    /// @brief Drop all retained frames.
    ///
    /// Resets the read window; the cumulative overwrite counter is kept so
    /// diagnostics still report historical eviction pressure.
    void clear() noexcept {
        head_ = 0U;
        count_ = 0U;
    }

private:
    /// @brief Map a chronological index onto a physical slot.
    ///
    /// @param index Chronological index in `[0, count())`.
    /// @return Physical slot index in `[0, capacity())`.
    /// @warning Requires `capacity() > 0`; only called from `at`.
    [[nodiscard]] std::size_t physical(std::size_t index) const noexcept {
        const std::size_t oldest = (head_ + capacity_ - count_) % capacity_;
        return (oldest + index) % capacity_;
    }

    std::unique_ptr<SpanFrame[]> slots_{}; ///< Owned slot storage; size == capacity.
    std::size_t capacity_{0U};             ///< Slot count; never changes after construction.
    std::size_t head_{0U};                 ///< Next write slot.
    std::size_t count_{0U};                ///< Retained frames, at most capacity.
    std::uint64_t overwritten_{0U};        ///< Cumulative frames evicted by overflow.
};

} // namespace nexigen::tracing::detail
