// nexigen::chrono — thread affinity helpers (change add-chrono-clock-service;
// design D4).
//
// Mirrors nexigen::logging/metrics/tracing::detail::affinity: a uint64_t core
// mask (cores 0..63). Callers run these ON THE THREAD TO BE PINNED: the
// snapshot worker pins itself as the first act inside its body, because
// std::jthread creation is asynchronous and pinning from the spawning thread
// could pin the wrong schedule window.
//
// The module keeps its own copy instead of reaching into another module's
// detail namespace: chrono must not depend on logging/metrics/tracing.
#pragma once

#include <cstdint>

#include <nexigen/core/result.hpp>

namespace nexigen::chrono::detail {

/// Pin the calling thread to the cores set in core_mask (bit i == CPU i).
/// core_mask == 0: InvalidArgument without touching the affinity. pthread
/// failure (or a mask naming cores beyond the allowed set): SystemError.
[[nodiscard]] core::Status pin_self(std::uint64_t core_mask) noexcept;

/// Read the calling thread's current affinity into core_mask_out. Cores above
/// bit 63 are unrepresentable in the uint64_t mask and silently dropped.
/// On pthread failure: SystemError.
[[nodiscard]] core::Status self_affinity(std::uint64_t& core_mask_out) noexcept;

} // namespace nexigen::chrono::detail
