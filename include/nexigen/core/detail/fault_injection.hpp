// nexigen::core — test-only cold-path fault injection.
//
// Enabled with NEXIGEN_ENABLE_FAULT_INJECTION=ON (CI Debug/coverage jobs).
// Production builds compile the checks to constant `false`, so there is no
// runtime or layout impact. Only control-plane syscalls consult these points;
// no producer, tick, snapshot, or sink hot path is instrumented.
#pragma once

#include <cstdint>

namespace nexigen::core::fault_injection {

/// @brief Control-plane syscall sites that the test-only hook may fail.
///
/// Each enumerator names one cold-path operation in the execution-manager or
/// thread-management control plane; no producer, tick, snapshot, or sink hot
/// path is instrumented. Only meaningful while
/// `NEXIGEN_ENABLE_FAULT_INJECTION` is defined.
enum class FaultPoint : std::uint8_t {
    ExecutionManager_pipe2,     ///< `pipe2` during execution-manager setup.
    ExecutionManager_fork,      ///< `fork` of a managed child process.
    ExecutionManager_open_null, ///< Opening the null device.
    Hub_socket,                 ///< Hub socket creation.
    Hub_bind,                   ///< Hub address binding.
    Hub_epoll,                  ///< Hub epoll instance creation.
    Hub_eventfd,                ///< Hub eventfd creation.
    Thread_set_name,            ///< Applying a thread name.
    Thread_set_affinity,        ///< Applying a CPU affinity.
    Thread_set_scheduler,       ///< Applying a scheduling policy/priority.
    Thread_snapshot,            ///< Snapshotting inherited thread attributes.
    Thread_restore_name,        ///< Rolling back a thread name.
    Thread_restore_affinity,    ///< Rolling back a CPU affinity.
    Thread_restore_scheduler,   ///< Rolling back a scheduling policy/priority.
};

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)

/// @brief Callback consulted before each instrumented syscall.
///
/// @param point Syscall site about to run.
/// @return `true` to fail the syscall artificially, `false` to run it normally.
using FaultHook = bool (*)(FaultPoint point) noexcept;

/// @brief Process-wide hook consulted by `fault`; `nullptr` disables injection.
inline FaultHook g_fault_hook = nullptr;

/// @brief Install the process-wide fault hook.
///
/// @param hook Callback to install, or `nullptr` to disable injection.
inline void set_fault_hook(FaultHook hook) noexcept {
    g_fault_hook = hook;
}

/// @brief Ask whether the instrumented syscall at `point` should fail.
///
/// @param point Syscall site about to run.
/// @return `true` when a hook is installed and reports failure for `point`.
[[nodiscard]] inline bool fault(FaultPoint point) noexcept {
    return g_fault_hook != nullptr && g_fault_hook(point);
}

#else

/// @brief Callback consulted before each instrumented syscall.
///
/// @param point Syscall site about to run.
/// @return `true` to fail the syscall artificially, `false` to run it normally.
using FaultHook = bool (*)(FaultPoint point) noexcept;

/// @brief Production stub: injection is compiled out, so no syscall fails.
///
/// The unnamed argument is an ignored syscall site.
///
/// @return Always `false`.
constexpr bool fault(FaultPoint) noexcept {
    return false;
}

/// @brief Production stub: accepts and ignores a hook (injection disabled).
///
/// The unnamed argument is an ignored callback; no hook is stored.
inline void set_fault_hook(FaultHook) noexcept {}

#endif

} // namespace nexigen::core::fault_injection
