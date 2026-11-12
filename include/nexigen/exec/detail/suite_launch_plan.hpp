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

/// @brief Everything needed to start one application attempt from a valid
///        manifest.
///
/// The resolved, immutable result of merging the suite manifest and the
/// application entry: the attempt artifact directory, `StartOptions`, the run
/// identity, and the exact argv. Built once per attempt and consumed by the
/// supervisor; it holds no process state.
///
/// @note Spec `exec/app-suite`, requirement "Manifest launch settings reach
///       each child".
/// @note Spec `exec/app-suite`, requirement "Settings sources merge per
///       application".
struct SuiteLaunchPlan {
    std::string artifact_dir{};         ///< Attempt artifact directory; stream root.
    StartOptions options{};             ///< Environment, CPU set, settings sources, level.
    app::RunIdentity identity{};        ///< Identity recorded in `resource.json` and env.
    std::vector<std::string> command{}; ///< Resolved argv, executable first.
};

/// @brief Resolve the effective launch settings for one attempt.
///
/// Merges the suite manifest with `spec`: artifact directory from
/// `attempt_dir_path`, settings sources (suite global replaced by the
/// application global, then the manifest-relative local path), effective log
/// level (application over suite), environment additions, CPU set, argv, and
/// run identity. `instance_dir` is the control directory owned by the suite
/// supervisor and stays fixed across attempts.
///
/// @param manifest Suite manifest supplying shared roots and defaults.
/// @param spec Application entry for this attempt.
/// @param attempt 1-based attempt number used for the artifact directory.
/// @param instance_dir Control instance directory.
/// @return The resolved launch plan.
/// @note Spec `exec/app-suite`, requirement "Each attempt writes to a new
///       artifact directory".
/// @note Spec `exec/app-suite`, requirement "Effective log level applies to
///       each application".
/// @note Spec `exec/app-suite`, scenario "Application global path replaces the suite path".
[[nodiscard]] SuiteLaunchPlan resolve_suite_launch(const SuiteManifest& manifest,
                                                   const AppSpec& spec, std::uint32_t attempt,
                                                   std::string_view instance_dir);

} // namespace nexigen::exec::detail
