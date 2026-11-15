// nexigen::exec — cgroup v2 resource-group budgets (change cpu-time-budgets).
//
// COLD PATH ONLY. Prepares one cgroup directory per budgeted resource group and
// writes the kernel limits; the child joins it (write 0 to cgroup.procs) before
// exec.
#pragma once

#include <string>

#include <nexigen/core/result.hpp>
#include <nexigen/exec/platform_controls.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec {

/// @brief Whether a group configures a cgroup CPU or memory budget.
[[nodiscard]] inline bool group_has_cgroup_budget(const ResourceGroupConfig& group) noexcept {
    return group.cpu_quota_us > 0U || group.memory_max_mb > 0U;
}

/// @brief Effective cgroup root (`/sys/fs/cgroup` when the manifest leaves it empty).
[[nodiscard]] inline std::string cgroup_root_of(const SuiteManifest& manifest) {
    return manifest.cgroup_root.empty() ? std::string("/sys/fs/cgroup") : manifest.cgroup_root;
}

/// @brief Directory of one group's cgroup (`<root>/nexigen/<suite>/<group>`).
[[nodiscard]] std::string resource_group_cgroup_path(const SuiteManifest& manifest,
                                                     const ResourceGroupConfig& group);

/// @brief Create and configure every budgeted group cgroup.
///
/// Groups without a budget are skipped and no filesystem path is touched. A
/// configured budget that cannot be written fails with `SystemError`, so the
/// suite start fails instead of silently running unbudgeted.
///
/// @param manifest Suite manifest.
/// @return Empty status on success or the first filesystem error.
/// @note Spec `exec/resource-groups`, requirement "CPU and memory budgets use
///       cgroup v2".
[[nodiscard]] core::Status prepare_resource_groups(const SuiteManifest& manifest) noexcept;

} // namespace nexigen::exec
