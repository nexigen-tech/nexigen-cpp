// nexigen::core — test-only cold-path fault injection.
//
// Enabled with NEXIGEN_ENABLE_FAULT_INJECTION=ON (CI Debug/coverage jobs).
// Production builds compile the checks to constant `false`, so there is no
// runtime or layout impact. Only control-plane syscalls consult these points;
// no producer, tick, snapshot, or sink hot path is instrumented.
#pragma once

#include <cstdint>

namespace nexigen::core::fault_injection {

enum class FaultPoint : std::uint8_t {
    Runner_pipe2,
    Runner_fork,
    Runner_open_null,
    Hub_socket,
    Hub_bind,
    Hub_epoll,
    Hub_eventfd,
    Thread_set_name,
    Thread_set_affinity,
    Thread_set_scheduler,
    Thread_snapshot,
    Thread_restore_name,
    Thread_restore_affinity,
    Thread_restore_scheduler,
};

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)

using FaultHook = bool (*)(FaultPoint point) noexcept;

inline FaultHook g_fault_hook = nullptr;

inline void set_fault_hook(FaultHook hook) noexcept {
    g_fault_hook = hook;
}

[[nodiscard]] inline bool fault(FaultPoint point) noexcept {
    return g_fault_hook != nullptr && g_fault_hook(point);
}

#else

using FaultHook = bool (*)(FaultPoint point) noexcept;

constexpr bool fault(FaultPoint) noexcept {
    return false;
}

inline void set_fault_hook(FaultHook) noexcept {}

#endif

} // namespace nexigen::core::fault_injection
