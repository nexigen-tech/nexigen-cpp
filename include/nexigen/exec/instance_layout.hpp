// nexigen::exec — instance directory layout helpers
// (change refactor-execution_manager-structure; spec execution_manager/instance-lifecycle).
#pragma once

#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::exec {

/// @brief Deterministic path `<instance_dir>/<file_name>`.
///
/// @param instance_dir Control instance directory.
/// @param file_name File name to append; the caller owns identifier safety.
/// @return The joined path as a plain string.
/// @note Spec `exec/instance-lifecycle`, requirement "Instance directory layout
///       is deterministic".
[[nodiscard]] std::string instance_file_path(std::string_view instance_dir,
                                             std::string_view file_name);

/// @brief Create `<instance_dir>/` and `<instance_dir>/settings/`.
///
/// Missing parents are created as well. `resource.json`, `exit.json`, and the
/// four generated settings files remain in this control directory even when
/// runtime streams are relocated to an attempt artifact directory.
///
/// @param instance_dir Absolute path of at most 511 bytes.
/// @return Success, `InvalidArgument` for a bad path, or `SystemError` on
///         `mkdir` failure (for example a permission error).
/// @note Spec `exec/instance-lifecycle`, requirement "Instance directory layout
///       is deterministic".
/// @warning Cold path only; performs file-system I/O before the child starts.
[[nodiscard]] core::Status create_instance_dir(std::string_view instance_dir) noexcept;

/// @brief Create an absolute directory tree (`mkdir -p`) for runtime artifacts.
///
/// Used for the per-attempt artifact directory supplied by a suite launch.
///
/// @param artifact_dir Absolute artifact directory.
/// @return Success, `InvalidArgument` for a bad path, or `SystemError` when the
///         tree cannot be created.
/// @note Spec `exec/app-suite`, requirement "Each attempt writes to a new
///       artifact directory".
[[nodiscard]] core::Status create_artifact_dir(std::string_view artifact_dir) noexcept;

} // namespace nexigen::exec
