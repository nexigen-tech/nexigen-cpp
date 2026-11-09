// nexigen::exec — instance lifecycle metadata (resource.json / exit.json)
// (change nexigen-exec; spec execution_manager/instance-lifecycle).
//
// COLD PATH ONLY. All functions here run before a child process starts or
// after it has been reaped. None of them may be called from a tick, producer,
// snapshot, or sink callback.
//
// The instance vocabulary is split one entity per header; this header is the
// compatibility umbrella for consumers that need the whole lifecycle surface.
#pragma once

#include <nexigen/exec/exit_metadata.hpp>
#include <nexigen/exec/instance_file_io.hpp>
#include <nexigen/exec/instance_layout.hpp>
#include <nexigen/exec/instance_settings.hpp>
#include <nexigen/exec/resource_metadata.hpp>
