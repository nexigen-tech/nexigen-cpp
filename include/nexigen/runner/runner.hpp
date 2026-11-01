// nexigen::runner — process supervisor (change nexigen-run;
// spec runner/process-supervision).
//
// COLD PATH ONLY. A `Runner` starts a detached supervisor that owns the
// application child, so `start` can return while the child keeps running.
// `stop` and `status` address an instance through its directory, never
// through in-process state.
#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nexigen/app/telemetry_layout.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/log_level.hpp>
#include <nexigen/runner/app_suite.hpp>
#include <nexigen/runner/instance_lifecycle.hpp>

namespace nexigen::runner {

/// Optional launch settings for one instance. Defaults preserve the original
/// single-instance behavior: streams in the instance directory, no extra
/// environment, no affinity restriction, no settings sources, no level override.
struct Start_options {
    std::string artifact_dir{};          ///< Empty = instance directory.
    std::string global_settings_dir{};   ///< Absolute directory or empty.
    std::string local_settings_dir{};    ///< Absolute directory or empty.
    std::optional<logging::LogLevel> log_level{}; ///< Empty = keep settings value.
    std::vector<std::pair<std::string, std::string>> env{}; ///< Child-only env.
    std::vector<std::uint32_t> cpus{};   ///< Empty = unrestricted.

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] inline std::string to_string(const Start_options& options) {
    return options.to_string();
}

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
    [[nodiscard]] core::Status start(
        const app::Run_identity& identity,
        const std::vector<std::string>& command,
        std::uint64_t timeout_s = 0);

    /// Options overload used by suite supervision. `artifact_dir` relocates
    /// runtime streams, `global_settings_dir`/`local_settings_dir` feed the
    /// settings merge, and `env`/`cpus` apply to the child before `exec`.
    [[nodiscard]] core::Status start(
        const app::Run_identity& identity,
        const std::vector<std::string>& command,
        std::uint64_t timeout_s,
        const Start_options& options);

    /// Start a detached suite supervisor and return once `suite.json` exists.
    [[nodiscard]] core::Status start_suite(const Suite_manifest& manifest,
                                           std::string& suite_dir_out);

    /// Send SIGTERM to the child process group, wait up to `grace_s`, then
    /// SIGKILL. Writes/patches `exit.json` so a graceful stop is attributed
    /// to the runner. Idempotent: an instance that already has an `exit.json`
    /// is left untouched.
    [[nodiscard]] core::Status stop(std::string_view instance_dir,
                                    std::uint64_t grace_s = 5);

    /// Report `running`, `stopped`, `exited`, or `unknown`, derived from
    /// `resource.json`, `/proc`, and `exit.json`.
    [[nodiscard]] core::Status status(std::string_view instance_dir,
                                      std::string& state) noexcept;
};

} // namespace nexigen::runner

template <>
struct std::formatter<nexigen::runner::Start_options>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::Start_options& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};
