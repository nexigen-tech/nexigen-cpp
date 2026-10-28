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

/// Every instrument object starts on its own cache line.
inline constexpr std::size_t instrument_alignment = 64;

/// Upper bounds (bytes) for one instrument object, worst-case instantiation.
inline constexpr std::size_t max_counter_size = 2560;
inline constexpr std::size_t max_gauge_size = 2560;
inline constexpr std::size_t max_statistics_size = 8192;
inline constexpr std::size_t max_explicit_histogram_size = 49152;
inline constexpr std::size_t max_exponential_histogram_size = 147456;

/// Upper bound for one meter object.
inline constexpr std::size_t max_meter_size = 256;

/// Round a byte count up to the instrument alignment.
[[nodiscard]] constexpr std::size_t align_up(std::size_t bytes) noexcept {
    return (bytes + (instrument_alignment - 1U)) & ~(instrument_alignment - 1U);
}

} // namespace nexigen::metrics::detail
