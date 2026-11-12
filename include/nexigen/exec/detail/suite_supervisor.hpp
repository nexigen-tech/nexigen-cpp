// nexigen::exec — suite supervisor entry point
// (change refactor-app-suite-layers; spec execution_manager/app-suite).
//
// COLD PATH ONLY. The control TU forks the detached supervisor and hands it
// the suite directory plus the start-report pipe; the supervisor TU owns the
// polling loop.
#pragma once

#include <cstdint>
#include <string>

#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec::detail {

/// @brief Suite supervisor poll cadence in milliseconds.
///
/// The supervisor sleeps this long between scans of child liveness, restart
/// timers, dependency readiness, and the stop-request file.
///
/// @note Spec `exec/app-suite`, requirement "Suite supervision is cold path
///       only".
inline constexpr std::uint64_t suite_poll_interval_ms = 100;

/// @brief Run the detached suite supervisor until it records a terminal
///        outcome.
///
/// Launches each application, polls for exits and restart timers, applies the
/// per-application impact policy and the shared suite restart budget, handles
/// dependency-ordered startup, and records `suite-state.json`/`suite-exit.json`.
/// Never called from an application path; the process exits from inside after
/// writing the final records.
///
/// @param manifest Parsed and validated suite manifest.
/// @param suite_dir Absolute suite directory containing the report files.
/// @param report_fd Write end of the start-report pipe; the supervisor writes
///        one `StartReport` and closes it.
/// @note Spec `exec/app-suite`, requirement "Suite supervisor loop uses an
///       explicit application state machine".
/// @note Spec `exec/app-suite`, requirement "Failure impact restarts the whole
///       suite".
/// @note Spec `exec/logging`, requirement "Suite supervisor records lifecycle
///       and policy events".
/// @warning Forks and signals processes; does not return to its caller.
void run_suite_supervisor(const SuiteManifest& manifest, const std::string& suite_dir,
                          int report_fd);

} // namespace nexigen::exec::detail
