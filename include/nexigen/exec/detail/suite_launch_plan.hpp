// nexigen::exec — resolved launch scenario for one suite application attempt
// (change refactor-app-suite-layers; spec execution_manager/app-suite).
//
// COLD PATH ONLY. A launch plan is built once per attempt and then consumed by
// the suite supervisor; it contains no process state.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/exec/app_spec.hpp>
#include <nexigen/exec/start_options.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec::detail {

/// Everything needed to start one application attempt from a valid manifest.
struct SuiteLaunchPlan {
    std::string artifact_dir{};
    StartOptions options{};
    app::RunIdentity identity{};
    std::vector<std::string> command{};
};

/// Resolve the effective launch settings for `spec` attempt `attempt`:
/// artifact directory, settings sources, log level, environment, CPU set,
/// argv, and run identity. `instance_dir` is the control directory owned by
/// the suite supervisor.
[[nodiscard]] SuiteLaunchPlan resolve_suite_launch(const SuiteManifest& manifest,
                                                   const AppSpec& spec, std::uint32_t attempt,
                                                   std::string_view instance_dir);

} // namespace nexigen::exec::detail
