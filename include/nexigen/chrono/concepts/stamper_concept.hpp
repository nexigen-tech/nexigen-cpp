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

/// A per-source monotonic stamp source: one noexcept sample().
template <class S>
concept Stamper = requires(S& s) {
    { s.sample() } noexcept -> std::same_as<Timestamp>;
};

} // namespace nexigen::chrono::concepts
