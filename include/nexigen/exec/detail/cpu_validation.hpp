// nexigen::exec — online CPU-set validation shared by every execution_manager surface
// (change refactor-execution_manager-structure).
#pragma once

#include <cstdint>

#include <sched.h>
#include <unistd.h>

#include <nexigen/core/thread_management.hpp>

namespace nexigen::exec::detail {

/// Number of online CPUs, falling back to the native `CPU_SETSIZE` when the
/// kernel query fails. Mirrors the previous per-file probes.
[[nodiscard]] inline long online_cpu_count() noexcept {
    const long count = ::sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? count : static_cast<long>(CPU_SETSIZE);
}

/// True when every requested CPU is below both the online count and the
/// native `CPU_SETSIZE` limit. An empty set is valid.
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
