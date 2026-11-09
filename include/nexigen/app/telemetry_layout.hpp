// nexigen::app — run/instance identity and telemetry path resolution
// (change add-telemetry-output-contract; spec app/telemetry-layout).
//
// COLD PATH ONLY. `load_run_identity` reads the NEXIGEN_* environment contract,
// `validate_run_identity` checks it, and `resolve_telemetry_paths` maps it to
// the four JSONL stream locations. None of this may be called from a tick,
// producer, snapshot, or sink path.
//
// The contract is intentionally language-neutral: the same variables and field
// names are used by the C# and Python implementations.
#pragma once

#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/app/app_settings.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/app/run_identity.hpp>
#include <nexigen/app/telemetry_paths.hpp>

namespace nexigen::app {


/// Read NEXIGEN_RUN_ID, NEXIGEN_EXEC_DIR,
/// NEXIGEN_INSTANCE_ID, and NEXIGEN_INSTANCE_DIR. The application name is not
/// part of run identity and is supplied separately. Never consults CWD,
/// hostname, or PID.
[[nodiscard]] RunIdentity load_run_identity() noexcept;

/// Derive the instance directory as `<run_dir>/<app_name>/<instance_id>` from a
/// separately supplied application name.
[[nodiscard]] std::string derive_instance_dir(std::string_view run_dir, std::string_view app_name,
                                              std::string_view instance_id);

/// Validate present ids (1..128 bytes, [A-Za-z0-9._-], no "."/".."/"..")
/// and present directories (POSIX absolute). A completely empty identity is
/// valid. Returns InvalidArgument on any violation.
[[nodiscard]] core::Status validate_run_identity(const RunIdentity& identity) noexcept;

/// Resolve each stream independently: explicit non-empty path, otherwise
/// `<instance_dir>/<metrics|traces|chrono>.jsonl`, otherwise empty. The
/// resolver performs no I/O and leaves `out` untouched on invalid identity.
[[nodiscard]] core::Status resolve_telemetry_paths(const RunIdentity& identity,
                                                   std::string_view explicit_metrics_path,
                                                   std::string_view explicit_traces_path,
                                                   std::string_view explicit_chrono_path,
                                                   TelemetryPaths& out) noexcept;

/// Settings convenience overload: reads `sink.path` from each subsystem.
[[nodiscard]] inline core::Status resolve_telemetry_paths(const RunIdentity& identity,
                                                          const metrics::Settings& metrics,
                                                          const tracing::Settings& tracing,
                                                          const chrono::Settings& chrono,
                                                          TelemetryPaths& out) noexcept {
    return resolve_telemetry_paths(identity, metrics.sink.path, tracing.sink.path, chrono.sink.path,
                                   out);
}


} // namespace nexigen::app
