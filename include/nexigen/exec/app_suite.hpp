/// @file nexigen::exec — application-suite umbrella (compatibility surface).
/// @brief Compatibility umbrella for the whole application-suite surface.
///
/// Includes every per-entity header of the suite vocabulary, so a consumer
/// that needs the complete surface (suite manifest, app specs, control,
/// metadata, status, paths, exits and child processes) gets it with one
/// include. Each included entity keeps its own header and its own
/// documentation; this file only forwards.
///
/// COLD PATH ONLY. Manifest parsing, validation, suite metadata and the
/// supervisor loop run outside every application hot path.
///
/// @note Spec `exec/app-suite` — the suite manifest vocabulary, the control
///       surface and the supervision contract.
///
/// @note Entity headers forwarded here:
///       - `app_spec.hpp` — one application entry of a suite.
///       - `child_process.hpp` — the spawned child process handle.
///       - `suite_app_metadata.hpp` — per-application suite metadata.
///       - `suite_app_status.hpp` — per-application status snapshot.
///       - `suite_control.hpp` — start/stop/supervision control surface.
///       - `suite_exit.hpp` — how a suite terminated.
///       - `suite_manifest.hpp` — the suite manifest itself.
///       - `suite_metadata.hpp` — suite-level metadata.
///       - `suite_paths.hpp` — suite directory layout.
///       - `suite_status.hpp` — suite-level status snapshot.
///
/// @note Prefer including the specific entity header when a consumer needs
///       one entity only: the umbrella pulls the whole surface and therefore
///       the whole compile cost of the module.
#pragma once

#include <nexigen/exec/app_spec.hpp>
#include <nexigen/exec/child_process.hpp>
#include <nexigen/exec/suite_app_metadata.hpp>
#include <nexigen/exec/suite_app_status.hpp>
#include <nexigen/exec/suite_control.hpp>
#include <nexigen/exec/suite_exit.hpp>
#include <nexigen/exec/suite_manifest.hpp>
#include <nexigen/exec/suite_metadata.hpp>
#include <nexigen/exec/suite_paths.hpp>
#include <nexigen/exec/suite_status.hpp>
