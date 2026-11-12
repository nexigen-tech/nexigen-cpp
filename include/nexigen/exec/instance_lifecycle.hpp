/// @file nexigen::exec — instance-lifecycle umbrella (compatibility surface).
/// @brief Compatibility umbrella for the whole instance-lifecycle surface.
///
/// Includes every per-entity header of the instance vocabulary, so a consumer
/// that needs the complete surface (resource metadata, exit metadata, the
/// generated instance layout and settings, and the file I/O around them) gets
/// it with one include. Each included entity keeps its own header and its own
/// documentation; this file only forwards.
///
/// COLD PATH ONLY. Everything here runs before a child process starts or
/// after it has been reaped; none of it may be called from a tick, producer,
/// snapshot, or sink callback.
///
/// @note Spec `exec/instance-lifecycle` — the metadata and file contract of a
///       supervised instance.
///
/// @note Entity headers forwarded here:
///       - `exit_metadata.hpp` — how an instance terminated.
///       - `instance_file_io.hpp` — reading/writing the instance files.
///       - `instance_layout.hpp` — the generated per-instance directory layout.
///       - `instance_settings.hpp` — the generated per-instance settings.
///       - `resource_metadata.hpp` — the resource metadata of an instance.
///
/// @note Prefer including the specific entity header when a consumer needs
///       one entity only: the umbrella pulls the whole surface and therefore
///       the whole compile cost of the module.
#pragma once

#include <nexigen/exec/exit_metadata.hpp>
#include <nexigen/exec/instance_file_io.hpp>
#include <nexigen/exec/instance_layout.hpp>
#include <nexigen/exec/instance_settings.hpp>
#include <nexigen/exec/resource_metadata.hpp>
