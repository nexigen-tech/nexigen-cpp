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

/// @brief Read the run identity from the fixed `NEXIGEN_*` environment contract.
///
/// Reads `NEXIGEN_RUN_ID`, `NEXIGEN_EXEC_DIR` (the run directory),
/// `NEXIGEN_INSTANCE_ID` and `NEXIGEN_INSTANCE_DIR`; unset or empty variables
/// leave the corresponding field empty. The application name is not part of run
/// identity and is supplied separately. Never consults the working directory,
/// hostname, or process id.
///
/// @return The observed identity; missing variables are not an error.
/// @note Spec `app/telemetry-layout`, requirement "Run identity is read from a
///       fixed environment contract".
/// @note Cold path only: environment access and allocation happen before
///       workers start.
[[nodiscard]] RunIdentity load_run_identity() noexcept;

/// @brief Derive the instance directory from the run and instance identity.
///
/// Joins `<run_dir>/<app_name>/<instance_id>`, trimming trailing separators
/// from `run_dir` so no double slash is produced. The inputs are not validated
/// here: call `validate_run_identity` on the result before resolving paths.
///
/// @param run_dir Run directory, normally `RunIdentity::run_dir`.
/// @param app_name Application name, supplied separately from run identity.
/// @param instance_id Instance id within the run.
/// @return The derived absolute path, or a path with empty segments when an
///         input is empty.
/// @note Spec `app/telemetry-layout`, requirement "Instance directory is
///       derived when not supplied".
/// @note Cold path only; allocates the result string.
[[nodiscard]] std::string derive_instance_dir(std::string_view run_dir, std::string_view app_name,
                                              std::string_view instance_id);

/// @brief Validate every present identity field.
///
/// Present ids (`run_id`, `instance_id`) must be 1..128 bytes from
/// `[A-Za-z0-9._-]` and must not equal `"."` or `".."` nor contain `".."`.
/// Present directories (`run_dir`, `instance_dir`) must be POSIX absolute
/// paths. A completely empty identity is valid, because observed applications
/// may not use the contract.
///
/// @param identity Identity to check; empty fields are skipped.
/// @return OK when every present field is safe, otherwise `InvalidArgument`.
/// @note Spec `app/telemetry-layout`, requirement "Identity validation rejects
///       unsafe values".
/// @note Cold path only; never applies the identity or touches the filesystem.
[[nodiscard]] core::Status validate_run_identity(const RunIdentity& identity) noexcept;

/// @brief Resolve the three stream paths from explicit settings then identity.
///
/// Each stream resolves independently: a non-empty explicit path wins,
/// otherwise `<instance_dir>/<metrics|traces|chrono>.jsonl` when the identity
/// carries an instance directory, otherwise the path stays empty (the helper
/// does not configure that sink). The resolver performs no I/O and creates
/// nothing.
///
/// @param identity Identity supplying the instance-directory fallback.
/// @param explicit_metrics_path Configured metrics path ("" = none).
/// @param explicit_traces_path Configured traces path ("" = none).
/// @param explicit_chrono_path Configured chrono path ("" = none).
/// @param out Destination, left untouched when `identity` is invalid.
/// @return OK with `out` assigned; `InvalidArgument` when the identity does not
///         validate.
/// @note Spec `app/telemetry-layout`, requirement "Sink paths resolve from
///       settings, then instance identity".
/// @note Spec `app/telemetry-layout`, requirement "Invalid identity fails path
///       resolution".
/// @note Cold path only; allocates path strings and performs no I/O.
[[nodiscard]] core::Status resolve_telemetry_paths(const RunIdentity& identity,
                                                   std::string_view explicit_metrics_path,
                                                   std::string_view explicit_traces_path,
                                                   std::string_view explicit_chrono_path,
                                                   TelemetryPaths& out) noexcept;

/// @brief Settings overload: reads `sink.path` from each subsystem.
///
/// @param identity Identity supplying the instance-directory fallback.
/// @param metrics Metrics settings whose `sink.path` is the explicit override.
/// @param tracing Tracing settings whose `sink.path` is the explicit override.
/// @param chrono Chrono settings whose `sink.path` is the explicit override.
/// @param out Destination, left untouched when `identity` is invalid.
/// @return OK with `out` assigned; `InvalidArgument` when the identity does not
///         validate.
/// @note Spec `app/telemetry-layout`, requirement "Sink paths resolve from
///       settings, then instance identity".
/// @note Cold path only: no I/O, no directory creation.
[[nodiscard]] inline core::Status resolve_telemetry_paths(const RunIdentity& identity,
                                                          const metrics::Settings& metrics,
                                                          const tracing::Settings& tracing,
                                                          const chrono::Settings& chrono,
                                                          TelemetryPaths& out) noexcept {
    return resolve_telemetry_paths(identity, metrics.sink.path, tracing.sink.path, chrono.sink.path,
                                   out);
}

} // namespace nexigen::app
