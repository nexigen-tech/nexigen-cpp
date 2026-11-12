// nexigen::exec — online CPU-set validation shared by every execution_manager surface
// (change refactor-execution_manager-structure).
#pragma once

#include <cstdint>

#include <sched.h>
#include <unistd.h>

#include <nexigen/core/thread_management.hpp>

namespace nexigen::exec::detail {

/// @brief Number of online CPUs, falling back to the native `CPU_SETSIZE`.
///
/// Mirrors the previous per-file probes so every execution_manager surface
/// validates against the same limit.
///
/// @return `sysconf(_SC_NPROCESSORS_ONLN)` when it reports a positive count,
///         otherwise `CPU_SETSIZE`.
/// @note Spec `exec/app-suite`, scenario "CPU index outside the online set is rejected".
[[nodiscard]] inline long online_cpu_count() noexcept {
    const long count = ::sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? count : static_cast<long>(CPU_SETSIZE);
}

/// @brief True when every requested CPU is below the online and native limits.
///
/// An empty set is valid and means "no affinity request".
///
/// @param cpus CPU set to validate.
/// @return `true` when every contained index is below both the online count
///         and `CPU_SETSIZE`; `false` otherwise.
/// @note Spec `exec/process-supervision`, scenario "Invalid CPU index is rejected before fork".
/// @note Spec `exec/app-suite`, scenario "CPU above 63 is not rejected merely for its width".
[[nodiscard]] inline bool cpus_within_online(const core::CpuSet& cpus) noexcept {
    if (cpus.empty()) {
        return true;
    }
    const auto online = static_cast<std::uint32_t>(online_cpu_count());
    for (std::uint32_t cpu = 0; cpu <= core::CpuSet::max_cpu_id; ++cpu) {
        if (cpus.contains(cpu) &&
            (cpu >= online || cpu >= static_cast<std::uint32_t>(CPU_SETSIZE))) {
            return false;
        }
    }
    return true;
}

} // namespace nexigen::exec::detail
