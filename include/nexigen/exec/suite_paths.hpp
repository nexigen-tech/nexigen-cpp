// nexigen::exec — deterministic suite/attempt path contract
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace nexigen::exec {

/// @brief Deterministic suite metadata directory `<run_dir>/.suite/<suite_id>`.
///
/// @param run_dir Absolute run directory.
/// @param suite_id Manifest suite id.
/// @return The suite directory path as a plain string.
/// @note Spec `exec/app-suite`, requirement "Suite metadata lives in a
///       deterministic suite directory".
[[nodiscard]] std::string suite_dir_path(std::string_view run_dir, std::string_view suite_id);

/// @brief Deterministic artifact directory of one launch attempt.
///
/// The attempt number is rendered zero-padded to four digits (`0001`, `0002`,
/// ...); a restart always allocates a new directory and never reuses or
/// truncates an earlier attempt.
///
/// @param artifacts_path Absolute artifact root from the manifest.
/// @param suite_id Manifest suite id.
/// @param app Application name.
/// @param instance Instance id.
/// @param attempt 1-based attempt number.
/// @return `<artifacts_path>/<suite_id>/<app>/<instance>/attempt-<NNNN>`.
/// @note Spec `exec/app-suite`, requirement "Each attempt writes to a new
///       artifact directory".
[[nodiscard]] std::string attempt_dir_path(std::string_view artifacts_path,
                                           std::string_view suite_id, std::string_view app,
                                           std::string_view instance, std::uint32_t attempt);

} // namespace nexigen::exec
