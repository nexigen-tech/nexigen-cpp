// nexigen::core — test-only cold-path fault injection.
//
// Enabled with NEXIGEN_ENABLE_FAULT_INJECTION=ON (CI Debug/coverage jobs).
// Production builds compile the checks to constant `false`, so there is no
// runtime or layout impact. Only control-plane syscalls consult these points;
// no producer, tick, snapshot, or sink hot path is instrumented.
#pragma once

#include <cstdint>

namespace nexigen::core::detail {

enum class Fault_point : std::uint8_t {
    Runner_pipe2,
    Runner_fork,
    Runner_open_null,
    Hub_socket,
    Hub_bind,
    Hub_epoll,
    Hub_eventfd,
};

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)

using Fault_hook = bool (*)(Fault_point point) noexcept;

inline Fault_hook g_fault_hook = nullptr;

inline void set_fault_hook(Fault_hook hook) noexcept {
    g_fault_hook = hook;
}

[[nodiscard]] inline bool fault(Fault_point point) noexcept {
    return g_fault_hook != nullptr && g_fault_hook(point);
}

#else

using Fault_hook = bool (*)(Fault_point point) noexcept;

constexpr bool fault(Fault_point) noexcept {
    return false;
}

inline void set_fault_hook(Fault_hook) noexcept {}

#endif

} // namespace nexigen::core::detail
