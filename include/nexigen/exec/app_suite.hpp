// nexigen::exec — application suite manifest and supervision
// (change nexigen-exec-app-suite; spec execution_manager/app-suite).
//
// COLD PATH ONLY. Manifest parsing, validation, suite metadata, and the
// supervisor loop run outside every application hot path.
//
// The suite vocabulary is split one entity per header; this header is the
// compatibility umbrella for consumers that need the whole suite surface.
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
