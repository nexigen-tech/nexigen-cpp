// nexigen::chrono — the per-source stamper contract (change
// add-clock-authority-core; spec chrono/provider "Stamper concept and
// issuance").
//
// A stamper is the single producer-owned source of hot-path monotonic stamps:
// sample() is noexcept and allocation-free, so producers call it directly in
// their enqueue path without touching the provider.
#pragma once

#include <concepts>

#include <nexigen/chrono/timestamp.hpp>

namespace nexigen::chrono::concepts {

/// @brief A per-source monotonic stamp source: exactly one noexcept `sample()`.
///
/// The single producer-owned instrument producers call directly in their
/// enqueue path. `sample()` must be allocation-free, lock-free, and free of
/// wall-clock reads; the returned series is non-decreasing per source object.
///
/// @tparam S Candidate stamp source type; it must expose
///         `sample() noexcept -> Timestamp`.
/// @note Spec `chrono/provider`, requirement "Stamper concept and issuance";
///       spec `chrono/stamp-source`, requirement "Calibrated monotonic stamp
///       source".
template <class S>
concept Stamper = requires(S& s) {
    { s.sample() } noexcept -> std::same_as<Timestamp>;
};

} // namespace nexigen::chrono::concepts
