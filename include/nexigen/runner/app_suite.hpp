// nexigen::runner — application suite manifest and supervision
// (change nexigen-run-app-suite; spec runner/app-suite).
//
// COLD PATH ONLY. Manifest parsing, validation, suite metadata, and the
// supervisor loop run outside every application hot path.
//
// The suite vocabulary is split one entity per header; this header is the
// compatibility umbrella for consumers that need the whole suite surface.
#pragma once

#include <nexigen/runner/app_spec.hpp>
#include <nexigen/runner/suite_app_metadata.hpp>
#include <nexigen/runner/suite_app_status.hpp>
#include <nexigen/runner/suite_control.hpp>
#include <nexigen/runner/suite_exit.hpp>
#include <nexigen/runner/suite_manifest.hpp>
#include <nexigen/runner/suite_metadata.hpp>
#include <nexigen/runner/suite_paths.hpp>
#include <nexigen/runner/suite_status.hpp>
