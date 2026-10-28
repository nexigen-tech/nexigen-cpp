// nexigen::metrics — per-thread slot registration (design D7).
//
// Multi-threaded instruments shard their state per recording thread: slot 0..31
// are exclusive to the first 32 threads that record a given KIND of instrument,
// slot 32 is the shared fallback cell for every further thread. The slot index
// is a thread-local constant, acquired once per (thread, kind) from a relaxed
// atomic counter — no lock, no allocation, no coordination on the hot path.
//
// The index is per KIND, not per instrument: every counter shares one index
// space, every statistics instrument another. Each instrument therefore keeps
// its own 32 exclusive slots, and the thread that owns index N for a kind owns
// slot N of every instrument of that kind (spec: "up to 32 threads hold
// exclusive thread_local-registered slots").
//
// Slots are never recycled: real-time platforms keep a stable thread set, and
// recycling would need a handshake on the recording path.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics::detail {

/// Number of exclusive per-thread slots.
inline constexpr std::uint8_t max_thread_slots = 32;

/// Slot index handed to threads beyond the exclusive pool.
inline constexpr std::uint8_t fallback_slot = max_thread_slots;

/// Total number of cells a multi-threaded instrument keeps (32 + fallback).
inline constexpr std::size_t shard_count = static_cast<std::size_t>(max_thread_slots) + 1U;

/// The slot-index space of one instrument kind.
inline constexpr std::size_t slot_kind_count = 5;

/// Which slot-index space an instrument kind uses.
[[nodiscard]] constexpr std::size_t slot_kind_index(MetricKind kind) noexcept {
    switch (kind) {
        case MetricKind::Counter:               return 0;
        case MetricKind::Gauge:                 return 1;
        case MetricKind::DescriptiveStatistics: return 2;
        case MetricKind::ExplicitHistogram:     return 3;
        case MetricKind::ExponentialHistogram:  return 4;
    }
    return 0;
}

/// This thread's slot for the given kind: an exclusive index below
/// max_thread_slots, otherwise fallback_slot. The thread-local state is
/// initialized once per kind on first use.
[[nodiscard]] inline std::uint8_t thread_slot(MetricKind kind) noexcept {
    static std::array<std::atomic<std::uint32_t>, slot_kind_count> counters{};
    static thread_local std::array<std::uint8_t, slot_kind_count> slots{};
    static thread_local std::array<bool, slot_kind_count> assigned{};
    const std::size_t index = slot_kind_index(kind);
    if (!assigned[index]) {
        const std::uint32_t raw = counters[index].fetch_add(1U, std::memory_order_relaxed);
        slots[index] = raw < max_thread_slots ? static_cast<std::uint8_t>(raw) : fallback_slot;
        assigned[index] = true;
    }
    return slots[index];
}

} // namespace nexigen::metrics::detail
