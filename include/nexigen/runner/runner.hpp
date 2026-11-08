// nexigen::runner — process supervisor (change nexigen-run;
// spec runner/process-supervision).
//
// COLD PATH ONLY. A `Runner` starts a detached supervisor that owns the
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
#include <nexigen/runner/start_options.hpp>
#include <nexigen/runner/suite_manifest.hpp>

namespace nexigen::runner {

/// Supervisor control surface. Instances are identified by their directory,
/// which makes the class stateless across runner processes.
class Runner {
public:
    /// Create the instance layout, generate settings, and launch `command`
    /// in a new session. Returns once the child PID has been recorded in
    /// `resource.json`; the child itself keeps running in the background.
    ///
    /// `identity.instance_dir` is derived as
    /// `<run_dir>/<app_name>/<instance_id>` when it is empty. A non-zero
    /// `timeout_s` starts the same stop sequence as `stop` on expiry.
    [[nodiscard]] core::Status start(const app::RunIdentity& identity, std::string_view app_name,
                                     const std::vector<std::string>& command,
                                     std::uint64_t timeout_s = 0);

    /// Options overload used by suite supervision. `artifact_dir` relocates
    /// runtime streams, `global_settings_dir`/`local_settings_dir` feed the
    /// settings merge, and `env`/`cpus` apply to the child before `exec`.
    [[nodiscard]] core::Status start(const app::RunIdentity& identity, std::string_view app_name,
                                     const std::vector<std::string>& command,
                                     std::uint64_t timeout_s, const StartOptions& options);

    /// Start a detached suite supervisor and return once `suite.json` exists.
    [[nodiscard]] core::Status start_suite(const SuiteManifest& manifest,
                                           std::string& suite_dir_out);

    /// Send SIGTERM to the child process group, wait up to `grace_s`, then
    /// SIGKILL. Writes/patches `exit.json` so a graceful stop is attributed
    /// to the runner. Idempotent: an instance that already has an `exit.json`
    /// is left untouched.
    [[nodiscard]] core::Status stop(std::string_view instance_dir, std::uint64_t grace_s = 5);

    /// Report `running`, `stopped`, `exited`, or `unknown`, derived from
    /// `resource.json`, `/proc`, and `exit.json`.
    [[nodiscard]] core::Status status(std::string_view instance_dir, std::string& state) noexcept;
};

} // namespace nexigen::runner
