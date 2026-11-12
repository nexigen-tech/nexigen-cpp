// nexigen::exec — process supervisor (change nexigen-exec;
// spec execution_manager/process-supervision).
//
// COLD PATH ONLY. A `ExecutionManager` starts a detached supervisor that owns the
// application child, so `start` can return while the child keeps running.
// `stop` and `status` address an instance through its directory, never
// through in-process state.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/start_options.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec {

/// @brief Supervisor control surface for one application instance.
///
/// `start` forks a detached supervisor which creates the instance layout,
/// generates settings, calls `setsid`/`execvp` for the child in a new session
/// and process group, and records `resource.json`; `stop` and `status` address
/// an instance through its directory, never through in-process state, which
/// makes the class stateless across execution_manager processes. All methods
/// are cold path: control-thread fork/wait/kill and JSON file I/O only, never
/// an application tick, producer, snapshot, or sink callback.
///
/// @note Spec `exec/process-supervision`, requirement "Child is launched under
///       a new process group".
/// @note Spec `exec/process-supervision`, requirement "Process supervision is
///       I/O-only on cold paths".
/// @warning Instances carry no state; concurrent operations on the same
///          instance directory are coordinated only by the on-disk files.
class ExecutionManager {
public:
    /// @brief Create the instance layout, generate settings, and launch
    ///        `command` in a new session.
    ///
    /// Returns once the child pid has been recorded in `resource.json`; the
    /// child itself keeps running in the background. `identity.instance_dir` is
    /// derived as `<run_dir>/<app_name>/<instance_id>` when it is empty. A
    /// non-zero `timeout_s` starts the same stop sequence as `stop` on expiry.
    ///
    /// @param identity Run identity; supplies the instance directory and the
    ///        `NEXIGEN_*` variables.
    /// @param app_name Application name passed through `--app-name`.
    /// @param command Executable followed by its arguments; must be non-empty.
    /// @param timeout_s Idle timeout in seconds; 0 = no timeout.
    /// @return Success, `InvalidArgument` for an empty command or invalid CPU
    ///         set, or `SystemError` when layout creation, the fork, or the
    ///         exec handshake fails. No `resource.json` is written on failure.
    /// @note Spec `exec/process-supervision`, requirement "`ExecutionManager::start`
    ///       is split into validation, layout, and launch phases".
    /// @note Spec `exec/process-supervision`, requirement "A failed exec fails
    ///       the start visibly".
    /// @note Spec `exec/instance-lifecycle`, requirement "Instance directory
    ///       layout is deterministic".
    [[nodiscard]] core::Status start(const app::RunIdentity& identity, std::string_view app_name,
                                     const std::vector<std::string>& command,
                                     std::uint64_t timeout_s = 0);

    /// @brief Options overload used by suite supervision.
    ///
    /// In addition to the base overload, `artifact_dir` relocates runtime
    /// streams (new per attempt, earlier attempts untouched),
    /// `global_settings_dir`/`local_settings_dir` feed the settings merge, and
    /// `env`/`cpus` apply to the child after the inherited environment and
    /// affinity reset and before `exec`. Identity variables take precedence
    /// over caller-supplied `env`.
    ///
    /// @param identity Run identity; supplies the instance directory and the
    ///        `NEXIGEN_*` variables.
    /// @param app_name Application name passed through `--app-name`.
    /// @param command Executable followed by its arguments; must be non-empty.
    /// @param timeout_s Idle timeout in seconds; 0 = no timeout.
    /// @param options Stream root, settings sources, log level, environment,
    ///        and CPU set.
    /// @return Success, `InvalidArgument` for an empty command, an invalid
    ///         environment entry, or an out-of-range CPU set, or `SystemError`
    ///         for layout, fork, or exec-handshake failures.
    /// @note Spec `exec/process-supervision`, requirement "Child environment and
    ///       CPU affinity are configurable".
    /// @note Spec `exec/app-suite`, requirement "Manifest launch settings reach
    ///       each child".
    /// @note Spec `exec/app-suite`, requirement "Settings sources merge per
    ///       application".
    [[nodiscard]] core::Status start(const app::RunIdentity& identity, std::string_view app_name,
                                     const std::vector<std::string>& command,
                                     std::uint64_t timeout_s, const StartOptions& options);

    /// @brief Start a detached suite supervisor and return once `suite.json`
    ///        exists.
    ///
    /// @param manifest Parsed and validated suite manifest.
    /// @param suite_dir_out Receives the absolute suite directory on success.
    /// @return Success, `InvalidArgument`, `Conflict` when the suite is already
    ///         running, or `SystemError` when the supervisor cannot be created.
    /// @note Spec `exec/app-suite`, requirement "Suite launch is detached".
    [[nodiscard]] core::Status start_suite(const SuiteManifest& manifest,
                                           std::string& suite_dir_out);

    /// @brief Send SIGTERM, wait, then SIGKILL, and finalize `exit.json`.
    ///
    /// Sends SIGTERM to the child process group, waits up to `grace_s`, then
    /// SIGKILL. Writes/patches `exit.json` so a graceful stop is attributed to
    /// the execution_manager. The identity anchor (`proc_start_ticks`) is
    /// verified before any signal: a recycled pid is not signalled and the
    /// instance is finalized with `terminated_by: "unknown"`. Idempotent: an
    /// instance that already has an `exit.json` is left untouched.
    ///
    /// @param instance_dir Control instance directory.
    /// @param grace_s Seconds between SIGTERM and SIGKILL.
    /// @return Success, `InvalidArgument` for a bad directory, or `SystemError`
    ///         when metadata cannot be read or written.
    /// @note Spec `exec/process-supervision`, requirement "Stop delivers SIGTERM
    ///       then SIGKILL on timeout".
    /// @note Spec `exec/process-supervision`, requirement "Identity is verified
    ///       before signals are sent".
    /// @note Spec `exec/app-suite`, requirement "Suite stop is final and
    ///       idempotent".
    [[nodiscard]] core::Status stop(std::string_view instance_dir, std::uint64_t grace_s = 5);

    /// @brief Report the lifecycle state of one instance.
    ///
    /// `running` means the child pid is alive and its live start-ticks match
    /// the recorded anchor; `stopped` means the execution_manager stopped the
    /// process deliberately; `exited` means the process terminated on its own;
    /// `unknown` means there is no record of this instance. Derived from
    /// `resource.json`, `/proc`, and `exit.json`.
    ///
    /// @param instance_dir Control instance directory.
    /// @param state Receives one of `running`, `stopped`, `exited`, or
    ///        `unknown`.
    /// @return Success, or `SystemError` when the recorded metadata cannot be
    ///         read.
    /// @note Spec `exec/process-supervision`, requirement "Status reports
    ///       lifecycle state".
    /// @note Spec `exec/process-supervision`, requirement "Identity is verified
    ///       before signals are sent" (same match classifies `running`).
    [[nodiscard]] core::Status status(std::string_view instance_dir, std::string& state) noexcept;
};

} // namespace nexigen::exec
