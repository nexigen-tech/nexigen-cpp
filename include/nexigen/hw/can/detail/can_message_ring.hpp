#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/hw/can/can_message_batch.hpp>

namespace nexigen::hw::can::detail {

/// @brief Fixed-capacity SPSC ring.
///
/// One producer calls `try_push`; one consumer calls `try_pop`. Indices use
/// release/acquire ordering and sit on separate cache lines.
///
/// @tparam T Element type.
/// @tparam Capacity Number of slots.
template <class T, std::size_t Capacity> class SpscRing {
    static_assert(Capacity > 0U, "SpscRing needs at least one slot");

public:
    /// @brief Push one message when a slot is free.
    /// @param message Message to copy into the ring.
    /// @return `true` on success, `false` when full.
    [[nodiscard]] bool try_push(const T& message) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1U) % Capacity;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        buffer_[head] = message;
        head_.store(next, std::memory_order_release);
        return true;
    }

    /// @brief Pop one message when available.
    /// @param message Destination.
    /// @return `true` on success, `false` when empty.
    [[nodiscard]] bool try_pop(T& message) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        message = buffer_[tail];
        tail_.store((tail + 1U) % Capacity, std::memory_order_release);
        return true;
    }

    /// @brief Approximate number of queued messages.
    /// @return Head minus tail modulo capacity.
    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return (head + Capacity - tail) % Capacity;
    }

private:
    std::array<T, Capacity> buffer_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

/// @brief SPSC ring specialized for CAN messages.
/// @tparam Capacity Number of message slots.
template <std::size_t Capacity> using CanMessageRing = SpscRing<CanMessage, Capacity>;

/// @brief SPSC ring specialized for CAN message batches.
/// @tparam Capacity Number of packet slots.
template <std::size_t Capacity> using CanBatchRing = SpscRing<CanMessageBatch, Capacity>;

} // namespace nexigen::hw::can::detail
