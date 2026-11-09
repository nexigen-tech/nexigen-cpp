// nexigen::exec — instance directory layout helpers
// (change refactor-execution_manager-structure; spec execution_manager/instance-lifecycle).
#pragma once

#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::exec {

/// Deterministic path `<instance_dir>/<file_name>`.
[[nodiscard]] std::string instance_file_path(std::string_view instance_dir,
                                             std::string_view file_name);

/// Create `<instance_dir>/` and `<instance_dir>/settings/`, including any
/// missing parents. `instance_dir` must be an absolute path of at most 511
/// bytes. Returns InvalidArgument for a bad path, SystemError on mkdir
/// failure (for example a permission error).
[[nodiscard]] core::Status create_instance_dir(std::string_view instance_dir) noexcept;

/// Create an absolute directory tree (mkdir -p) for runtime artifacts.
[[nodiscard]] core::Status create_artifact_dir(std::string_view artifact_dir) noexcept;

} // namespace nexigen::exec
