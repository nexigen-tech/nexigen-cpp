// nexigen::logging — thread affinity helpers (design.md D15).
//
// Wraps pthread_setaffinity_np / pthread_getaffinity_np with a uint64_t core
// mask (cores 0..63 — beyond that returns InvalidArgument). Callers run these
// ON THE THREAD TO BE PINNED (the worker pins itself as the first act inside
// its body; tests probe via a dedicated thread): std::jthread creation is
// asynchronous, so setting affinity from the spawning thread could pin the
// wrong schedule window. Requires _GNU_SOURCE for cpu_set_t (provided by the
// nexigen_logging target).
#pragma once

#include <cstdint>

#include <nexigen/core/result.hpp>

namespace nexigen::logging::detail {

/// Pin the calling thread to the cores set in core_mask (bit i == CPU i).
/// core_mask == 0 or any bit above 63: InvalidArgument without touching the
/// affinity. pthread failure: SystemError.
[[nodiscard]] core::Status pin_self(std::uint64_t core_mask) noexcept;

/// Read the calling thread's current affinity into core_mask_out. Cores above
/// bit 63 are unrepresentable in the uint64_t mask and silently dropped. On
/// pthread failure: SystemError.
[[nodiscard]] core::Status self_affinity(std::uint64_t& core_mask_out) noexcept;

} // namespace nexigen::logging::detail
