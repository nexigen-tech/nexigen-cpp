// nexigen::logging — thread affinity (CHANGE dedup-affinity-core: the
// implementation moved to nexigen::core::affinity; this header is a
// forward-only shim so every `detail::pin_self`/`detail::self_affinity`
// call site keeps resolving).
//
// Calling convention (from the core header, kept here for convenience):
// callers run these ON THE THREAD TO BE PINNED — the background worker
// pins itself as the first act inside its body, because std::jthread
// creation is asynchronous and pinning from the spawning thread could
// pin the wrong schedule window.
#pragma once

#include <nexigen/core/affinity.hpp>

namespace nexigen::logging::detail {

/// @brief Forwarding alias for `core::affinity::pin_self`: pin the calling
///        thread to a uint64 core mask.
///
/// Bit `i` selects CPU `i`; a zero mask yields `InvalidArgument` without an
/// affinity syscall, and an ungrantable CPU yields `SystemError`. The function
/// is `noexcept`, allocation-free, and cold-path only (worker start).
///
/// @note Spec `core/affinity`, requirement "Pinning the calling thread by core
///       mask"; the logging module keeps this forward-only shim under
///       requirement "Single implementation behind module shims".
/// @warning Call it on the thread to be pinned, as that thread's first act:
///          `std::jthread` creation is asynchronous, so pinning from the
///          spawning thread may apply to the wrong schedule window.
using nexigen::core::affinity::pin_self;

/// @brief Forwarding alias for `core::affinity::self_affinity`: read the
///        calling thread's affinity into a uint64 mask.
///
/// Cores above bit 63 are unrepresentable and dropped silently; a syscall
/// failure yields `SystemError`. Cold-path only and allocation-free.
///
/// @note Spec `core/affinity`, requirement "Querying the calling thread's
///       affinity"; forwarded here per requirement "Single implementation
///       behind module shims".
/// @warning Call it on the thread whose mask is requested.
using nexigen::core::affinity::self_affinity;

} // namespace nexigen::logging::detail
