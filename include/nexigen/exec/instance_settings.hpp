// nexigen::exec — generated instance settings files
// (change refactor-execution_manager-structure; spec execution_manager/instance-lifecycle).
#pragma once

#include <string_view>

#include <nexigen/app/telemetry_layout.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::exec {

/// @brief Write the four generated settings files with the instance directory
///        as the stream root.
///
/// Convenience overload for a single-instance start: `stream_root` is
/// `instance_dir`, so every sink and `log_root_directory` point there.
///
/// @param instance_dir Control instance directory containing `settings/`.
/// @param resolved Telemetry paths to honour when they lie inside the root.
/// @return Success, `InvalidArgument` for a bad path, or `SystemError` when a
///         settings file cannot be written.
/// @note Spec `exec/instance-lifecycle`, requirement "Generated settings
///       contain absolute sink paths".
[[nodiscard]] core::Status generate_settings(std::string_view instance_dir,
                                             const app::TelemetryPaths& resolved) noexcept;

/// @brief Write the four generated settings files under
///        `<instance_dir>/settings/`.
///
/// `stream_root` is the directory that receives `app.jsonl`, `metrics.jsonl`,
/// `traces.jsonl`, `chrono.jsonl`, and `stdout.log`/`stderr.log`. Each
/// `sink.path` is an absolute path inside `stream_root`. A path in `resolved`
/// is used only when it is absolute and lies inside `stream_root`; otherwise
/// the deterministic `<stream_root>/<stream>.jsonl` default is written.
/// `log_root_directory` is exactly `stream_root`.
///
/// @param instance_dir Control instance directory containing `settings/`.
/// @param stream_root Attempt artifact directory (or the instance directory).
/// @param resolved Telemetry paths from the identity contract and settings.
/// @return Success, `InvalidArgument` for a bad path, or `SystemError` when a
///         settings file cannot be written.
/// @note Spec `exec/app-suite`, requirement "Generated settings point at the
///       attempt artifacts".
/// @note Spec `exec/instance-lifecycle`, requirement "Generated settings
///       contain absolute sink paths".
[[nodiscard]] core::Status generate_settings(std::string_view instance_dir,
                                             std::string_view stream_root,
                                             const app::TelemetryPaths& resolved) noexcept;

} // namespace nexigen::exec
