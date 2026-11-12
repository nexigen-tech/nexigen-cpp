// nexigen::chrono — thread affinity (CHANGE dedup-affinity-core: the
// implementation moved to nexigen::core::affinity; this header is a
// forward-only shim so every `detail::pin_self`/`detail::self_affinity`
// call site keeps resolving).
//
// Calling convention (from the core header, kept here for convenience):
// callers run these ON THE THREAD TO BE PINNED — the snapshot worker pins
// itself as the first act inside its body, because std::jthread creation
// is asynchronous and pinning from the spawning thread could pin the
// wrong schedule window.
#pragma once

#include <nexigen/core/affinity.hpp>

namespace nexigen::chrono::detail {

/// @brief Pin the calling thread to a uint64 core mask; forwarded from core.
///
/// Bit `i` selects CPU `i`. A zero mask reports `InvalidArgument` without
/// touching affinity; a pthread or policy failure reports `SystemError`.
/// Callers must run this on the thread to be pinned — worker threads pin
/// themselves as their first act, because `std::jthread` creation is
/// asynchronous and pinning from the spawning thread can target the wrong
/// schedule window.
///
/// @note Spec `core/affinity`; cold path (worker/provider start only).
using nexigen::core::affinity::pin_self;
/// @brief Read the calling thread's affinity into a uint64 core mask; forwarded from core.
///
/// Cores above bit 63 are unrepresentable and silently dropped. A pthread
/// failure reports `SystemError`. As with `pin_self`, call it on the thread
/// whose affinity is being observed.
///
/// @note Spec `core/affinity`; cold path.
using nexigen::core::affinity::self_affinity;

} // namespace nexigen::chrono::detail
