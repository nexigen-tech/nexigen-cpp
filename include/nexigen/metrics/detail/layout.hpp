// nexigen::metrics — fixed memory-layout constants (design D6).
//
// The registry pre-allocates one aligned block per instrument kind and hands
// out 64-byte-aligned slots from it. The per-kind budget is derived from a
// compile-time size bound; every instrument header static_asserts that its
// largest instantiation fits its bound, so the budget can never drift below
// the real object size.
#pragma once

#include <cstddef>

namespace nexigen::metrics::detail {

/// @brief Cache-line alignment every instrument slot starts at.
inline constexpr std::size_t instrument_alignment = 64;

/// @brief Worst-case byte size of one `Counter` instantiation.
inline constexpr std::size_t max_counter_size = 2560;
/// @brief Worst-case byte size of one `Gauge` instantiation.
inline constexpr std::size_t max_gauge_size = 2560;
/// @brief Worst-case byte size of one `StateGauge` instantiation.
inline constexpr std::size_t max_state_size = 256;
/// @brief Worst-case byte size of one `DescriptiveStatistics` instantiation.
inline constexpr std::size_t max_statistics_size = 8192;
/// @brief Worst-case byte size of one `ExplicitBucketHistogram` instantiation.
inline constexpr std::size_t max_explicit_histogram_size = 49152;
/// @brief Worst-case byte size of one `ExponentialHistogram` instantiation.
inline constexpr std::size_t max_exponential_histogram_size = 147456;

/// @brief Worst-case byte size of one `Meter` object.
inline constexpr std::size_t max_meter_size = 256;

/// @brief Round a byte count up to the instrument alignment.
///
/// @param bytes Byte count to round.
/// @return The smallest multiple of `instrument_alignment` that is at least
///         `bytes`. The alignment is a power of two, so the mask arithmetic is
///         exact.
/// @note Spec `metrics/metric-provider`, requirement "Pre-allocated aligned
///       metric registry".
[[nodiscard]] constexpr std::size_t align_up(std::size_t bytes) noexcept {
    return (bytes + (instrument_alignment - 1U)) & ~(instrument_alignment - 1U);
}

} // namespace nexigen::metrics::detail
