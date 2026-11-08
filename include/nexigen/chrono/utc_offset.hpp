// nexigen::chrono — UTC-offset snapshot and consumer-side conversion
// (logging design D10; spec chrono/stamp-source "UTC offset snapshot
// calibration" / "Monotonic-to-UTC conversion").
//
// Cold path (initialize / rotation boundary / flush hook): sample_utc_offset
// reads (CLOCK_REALTIME − CLOCK_MONOTONIC_RAW) at one instant; the producer
// never calls CLOCK_REALTIME — conversion to UTC happens on the consumer
// thread through utc_from_mono_ns. The body of sample_utc_offset lives in
// src/chrono/utc_offset.cpp.
#pragma once

#include <cstdint>

#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// Snapshot the offset such that utc_ns ≈ monotonic_ns + offset (logging
/// design D10). Cold path (initialize / rotation boundary / flush hook): two
/// clock reads, no allocation. The producer never calls CLOCK_REALTIME —
/// conversion to UTC happens on the consumer thread through this offset
/// snapshot.
[[nodiscard]] core::Status sample_utc_offset(std::int64_t& offset_ns_out) noexcept;

/// Apply an offset snapshot: the consumer-side UTC time of a record stamp.
[[nodiscard]] inline std::int64_t utc_from_mono_ns(std::uint64_t mono_ns,
                                                   std::int64_t offset_ns) noexcept {
    return static_cast<std::int64_t>(mono_ns) + offset_ns;
}

} // namespace nexigen::chrono
