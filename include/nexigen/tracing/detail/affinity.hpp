// nexigen::tracing — thread affinity (CHANGE dedup-affinity-core: the
// implementation moved to nexigen::core::affinity; this header is a
// forward-only shim so every `detail::pin_self`/`detail::self_affinity`
// call site keeps resolving).
//
// Calling convention (from the core header, kept here for convenience):
// callers run these ON THE THREAD TO BE PINNED — the snapshot worker
// pins itself as the first act inside its body, because std::jthread
// creation is asynchronous and pinning from the spawning thread could
// pin the wrong schedule window.
#pragma once

#include <nexigen/core/affinity.hpp>

namespace nexigen::tracing::detail {

/// @brief Re-export of `core::affinity::pin_self` for tracing call sites.
///
/// Pins the calling thread to the cores named by a 64-bit mask (bit `i` == CPU
/// `i`). Must be invoked on the thread to be pinned, typically as the first act
/// inside the daemon or provider worker body, because thread creation is
/// asynchronous and pinning from the spawning thread could pin the wrong
/// schedule window.
///
/// @param core_mask Non-zero mask of cores to pin to.
/// @return Success, `core::ErrorCode::InvalidArgument` for a zero mask (the
///         affinity is then untouched), or `core::ErrorCode::SystemError` when
///         the affinity syscall fails for a bit that names no online CPU.
/// @note Forward-only shim; spec `core/affinity`, requirement "Pinning the
///       calling thread by core mask" and requirement "Single implementation
///       behind module shims". Tracing applies it only at cold worker startup.
using nexigen::core::affinity::pin_self;

/// @brief Re-export of `core::affinity::self_affinity` for tracing call sites.
///
/// Reads the calling thread's affinity into a 64-bit mask. Must be invoked on
/// the thread being queried. Cores above bit 63 are unrepresentable and are
/// silently dropped.
///
/// @param core_mask_out Receives the current affinity mask on success.
/// @return Success, or `core::ErrorCode::SystemError` when the affinity query
///         syscall fails.
/// @note Forward-only shim; spec `core/affinity`, requirement "Querying the
///       calling thread's affinity" and requirement "Single implementation
///       behind module shims". Tracing queries affinity only on the diagnostic
///       path.
using nexigen::core::affinity::self_affinity;

} // namespace nexigen::tracing::detail
