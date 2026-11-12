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

/// Fixed-capacity ring of SpanFrame records with drop-oldest overflow.
class SpanRing {
public:
    SpanRing() = default;

    /// Allocate `capacity` frame slots (capacity > 0; validated by the caller).
    explicit SpanRing(std::size_t capacity)
        : slots_{capacity > 0U ? std::make_unique<SpanFrame[]>(capacity) : nullptr},
          capacity_{capacity} {}

    SpanRing(const SpanRing&) = delete;
    SpanRing& operator=(const SpanRing&) = delete;
    SpanRing(SpanRing&&) noexcept = default;
    SpanRing& operator=(SpanRing&&) noexcept = default;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] std::uint64_t overwritten() const noexcept { return overwritten_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0U; }

    /// Append one frame; the oldest frame is discarded when the ring is full.
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

    /// Chronological access: index 0 is the oldest retained frame.
    [[nodiscard]] const SpanFrame& at(std::size_t index) const noexcept {
        return slots_[physical(index)];
    }

    /// Drop all retained frames; the cumulative overwrite counter is kept.
    void clear() noexcept {
        head_ = 0U;
        count_ = 0U;
    }

private:
    [[nodiscard]] std::size_t physical(std::size_t index) const noexcept {
        const std::size_t oldest = (head_ + capacity_ - count_) % capacity_;
        return (oldest + index) % capacity_;
    }

    std::unique_ptr<SpanFrame[]> slots_{};
    std::size_t capacity_{0U};
    std::size_t head_{0U};  ///< next write slot
    std::size_t count_{0U}; ///< retained frames, ≤ capacity
    std::uint64_t overwritten_{0U};
};

} // namespace nexigen::tracing::detail
