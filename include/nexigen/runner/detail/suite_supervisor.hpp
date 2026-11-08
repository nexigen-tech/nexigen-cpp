// nexigen::runner — suite supervisor entry point
// (change refactor-app-suite-layers; spec runner/app-suite).
//
// COLD PATH ONLY. The control TU forks the detached supervisor and hands it
// the suite directory plus the start-report pipe; the supervisor TU owns the
// polling loop.
#pragma once

#include <cstdint>
#include <string>

#include <nexigen/runner/suite_manifest.hpp>

namespace nexigen::runner::detail {

/// Suite supervisor poll cadence in milliseconds.
inline constexpr std::uint64_t suite_poll_interval_ms = 100;

/// Run the detached suite supervisor until it records a terminal outcome.
/// Never called from an application path; the process exits from inside.
void run_suite_supervisor(const SuiteManifest& manifest, const std::string& suite_dir,
                          int report_fd);

} // namespace nexigen::runner::detail
