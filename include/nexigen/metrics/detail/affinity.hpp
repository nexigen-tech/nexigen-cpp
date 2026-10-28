// nexigen::metrics — thread affinity helpers (design D2/D14).
//
// Mirrors nexigen::logging::detail::affinity: a uint64_t core mask (cores
// 0..63 — beyond that returns InvalidArgument). Callers run these ON THE
// THREAD TO BE PINNED: the harvester pins itself as the first act inside its
// body, because std::jthread creation is asynchronous and pinning from the
// spawning thread could pin the wrong schedule window.
//
// The module keeps its own copy instead of reaching into nexigen::logging's
// detail namespace: metrics must not depend on the logging module.
#pragma once

#include <cstdint>

#include <nexigen/core/result.hpp>

namespace nexigen::metrics::detail {

/// Pin the calling thread to the cores set in core_mask (bit i == CPU i).
/// core_mask == 0 or any bit above 63: InvalidArgument without touching the
/// affinity. pthread failure: SystemError.
[[nodiscard]] core::Status pin_self(std::uint64_t core_mask) noexcept;

/// Read the calling thread's current affinity into core_mask_out. Cores above
/// bit 63 are unrepresentable in the uint64_t mask and silently dropped.
/// On pthread failure: SystemError.
[[nodiscard]] core::Status self_affinity(std::uint64_t& core_mask_out) noexcept;

} // namespace nexigen::metrics::detail
