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

/// @brief Sample the signed `realtime` minus `monotonic_raw` clock offset.
///
/// Cold-path operation for initialize / rotation boundaries / flush hooks: two
/// clock reads at one instant, no heap allocation. The producer never calls
/// `CLOCK_REALTIME`; consumers apply this offset through `utc_from_mono_ns`.
/// The body lives in `src/chrono/utc_offset.cpp`.
///
/// @param offset_ns_out Destination for the signed offset in nanoseconds;
///        assigned only on success.
/// @return Success with `offset_ns_out` set, or `core::ErrorCode::SystemError`
///         when either clock read fails (the destination keeps its old value).
/// @note Spec `chrono/stamp-source`, requirement "UTC offset snapshot
///       calibration".
[[nodiscard]] core::Status sample_utc_offset(std::int64_t& offset_ns_out) noexcept;

/// @brief Convert a monotonic stamp to UTC using a previously sampled offset.
///
/// Pure consumer-side arithmetic: no clock access, no allocation, no shared
/// state, so any thread may call it. A monotonic event is re-bound to wall
/// time as `mono_ns + offset_ns`.
///
/// @param mono_ns Monotonic-raw nanoseconds of the event or snapshot.
/// @param offset_ns Signed offset produced by `sample_utc_offset`.
/// @return UTC nanoseconds since the Unix epoch.
/// @note Spec `chrono/stamp-source`, requirement "Monotonic-to-UTC conversion".
/// @warning Precision is bounded by the offset's two-read latency.
[[nodiscard]] inline std::int64_t utc_from_mono_ns(std::uint64_t mono_ns,
                                                   std::int64_t offset_ns) noexcept {
    return static_cast<std::int64_t>(mono_ns) + offset_ns;
}

} // namespace nexigen::chrono
