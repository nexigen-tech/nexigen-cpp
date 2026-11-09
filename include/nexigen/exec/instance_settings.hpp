// nexigen::exec — generated instance settings files
// (change refactor-execution_manager-structure; spec execution_manager/instance-lifecycle).
#pragma once

#include <string_view>

#include <nexigen/app/telemetry_layout.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::exec {

/// Write the four generated settings files under `<instance_dir>/settings/`
/// with the instance directory as the stream root.
[[nodiscard]] core::Status generate_settings(std::string_view instance_dir,
                                             const app::TelemetryPaths& resolved) noexcept;

/// Write the four generated settings files under `<instance_dir>/settings/`.
///
/// `stream_root` is the directory that receives `app.jsonl`, `metrics.jsonl`,
/// `traces.jsonl`, `chrono.jsonl`, and `stdout.log`/`stderr.log`. Each
/// `sink.path` is an absolute path inside `stream_root`. A path in `resolved`
/// is used only when it is absolute and lies inside `stream_root`; otherwise
/// the deterministic `<stream_root>/<stream>.jsonl` default is written.
/// `log_root_directory` is exactly `stream_root`.
[[nodiscard]] core::Status generate_settings(std::string_view instance_dir,
                                             std::string_view stream_root,
                                             const app::TelemetryPaths& resolved) noexcept;

} // namespace nexigen::exec
