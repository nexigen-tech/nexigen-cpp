// nexigen::runner — instance lifecycle metadata (resource.json / exit.json)
// (change nexigen-run; spec runner/instance-lifecycle).
//
// COLD PATH ONLY. All functions here run before a child process starts or
// after it has been reaped. None of them may be called from a tick, producer,
// snapshot, or sink callback.
//
// The instance vocabulary is split one entity per header; this header is the
// compatibility umbrella for consumers that need the whole lifecycle surface.
#pragma once

#include <nexigen/runner/exit_metadata.hpp>
#include <nexigen/runner/instance_file_io.hpp>
#include <nexigen/runner/instance_layout.hpp>
#include <nexigen/runner/instance_settings.hpp>
#include <nexigen/runner/resource_metadata.hpp>
