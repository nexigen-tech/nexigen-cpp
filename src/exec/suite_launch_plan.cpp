// nexigen::exec — suite scenario resolution: AppSpec -> launch plan
// (change refactor-app-suite-layers; spec execution_manager/app-suite).
//
// COLD PATH ONLY. Resolves the settings sources, log level, environment,
// affinity, argv, and identity for one attempt; it never creates a process.
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nexigen/exec/app_spec.hpp>
#include <nexigen/exec/cgroup_budget.hpp>
#include <nexigen/exec/detail/path_utils.hpp>
#include <nexigen/exec/detail/suite_launch_plan.hpp>
#include <nexigen/exec/suite_manifest.hpp>
#include <nexigen/exec/suite_paths.hpp>

namespace nexigen::exec::detail {

namespace {

[[nodiscard]] std::vector<std::string> command_vector(const AppSpec& spec,
                                                      const StateDependentStartupConfig& config) {
    std::vector<std::string> command;
    command.reserve(config.args.size() + 1);
    command.push_back(spec.command);
    for (const std::string& argument : config.args) {
        command.push_back(argument);
    }
    return command;
}

} // namespace

SuiteLaunchPlan resolve_suite_launch(const SuiteManifest& manifest, const AppSpec& spec,
                                     const StateDependentStartupConfig& config,
                                     std::uint32_t attempt, std::string_view instance_dir) {
    SuiteLaunchPlan plan{};
    plan.artifact_dir = attempt_dir_path(manifest.artifacts_path, manifest.suite_id, spec.app,
                                         spec.instance, attempt);

    plan.options.artifact_dir = plan.artifact_dir;
    plan.options.global_settings_dir = spec.global_settings_path.empty()
                                           ? manifest.global_settings_path
                                           : spec.global_settings_path;
    if (!spec.local_settings_path.empty()) {
        plan.options.local_settings_dir =
            join_path(directory_of(manifest.manifest_path), spec.local_settings_path);
    }
    plan.options.log_level = spec.log_level.has_value() ? spec.log_level : manifest.log_level;
    plan.options.env = config.env;
    plan.options.cpus = spec.cpus;
    plan.options.expected_executable_digest = spec.executable_digest;
    plan.options.uid = spec.uid;
    plan.options.gid = spec.gid;
    plan.options.require_non_root = manifest.trusted_platform.require_non_root;

    const ResourceGroupConfig* group = nullptr;
    for (const ResourceGroupConfig& candidate : manifest.resource_groups) {
        if (candidate.name == spec.resource_group) {
            group = &candidate;
            break;
        }
    }
    if (group != nullptr) {
        constexpr std::uint64_t bytes_per_mib = 1024ULL * 1024ULL;
        plan.options.memory_limit_bytes = group->memory_limit_mb > (UINT64_MAX / bytes_per_mib)
                                              ? UINT64_MAX
                                              : group->memory_limit_mb * bytes_per_mib;
        if (plan.options.cpus.empty()) {
            plan.options.cpus = group->cpus;
        }
        plan.options.working_dir = group->working_dir;
        if (group_has_cgroup_budget(*group)) {
            plan.options.cgroup_path = resource_group_cgroup_path(manifest, *group);
        }
    }

    plan.identity = app::RunIdentity{manifest.run_id, manifest.run_dir, spec.instance,
                                     std::string(instance_dir)};
    plan.command = command_vector(spec, config);
    return plan;
}

} // namespace nexigen::exec::detail
