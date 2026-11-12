// nexigen::exec — detached suite lifecycle control
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec {

/// @brief Start a detached suite supervisor.
///
/// Validates the manifest, preflights the thread specs, checks for an
/// already-running suite, creates `<run_dir>/.suite/<suite_id>`, forks and
/// detaches the supervisor (`setsid`, redirect to `/dev/null`), and waits for
/// its start report. Returns once the supervisor owns the suite and wrote
/// `suite.json`. Per-application launch failures are recorded in suite state
/// and handled by policy, not by this status.
///
/// @param manifest Parsed and validated suite manifest.
/// @param suite_dir_out Receives the absolute suite directory on success.
/// @return Success, `InvalidArgument` for an invalid manifest, `Conflict` when
///         the suite is already running, or `SystemError` when the supervisor
///         cannot be created or reports a failed start.
/// @note Spec `exec/app-suite`, requirement "Suite launch is detached".
/// @note Spec `exec/app-suite`, requirement "`start_suite` is split into
///       validation, conflict check, and supervisor launch".
/// @note Spec `exec/app-suite`, requirement "Manifest launch settings reach
///       each child".
/// @warning Cold path only; forks and performs file I/O.
[[nodiscard]] core::Status start_suite(const SuiteManifest& manifest, std::string& suite_dir_out);

/// @brief Request a final suite stop and wait for the recorded outcome.
///
/// Writes the stop request, computes the effective grace period, and waits for
/// `suite-exit.json`. When the supervisor does not finalize within the
/// deadline, a fallback path verifies the supervisor identity anchor and
/// signals the supervisor plus each recorded instance. Idempotent: a suite
/// that already recorded a terminal outcome is left unchanged.
///
/// @param suite_dir Absolute `<run_dir>/.suite/<suite_id>` directory.
/// @param grace_override Optional grace period overriding each application's
///        `stop_grace_s`.
/// @return Success when the terminal record exists or was written, or an error
///         when the suite directory is unreadable or signalling fails.
/// @note Spec `exec/app-suite`, requirement "Suite stop is final and
///       idempotent".
/// @note Spec `exec/app-suite`, requirement "`stop_suite` is split into
///       request, wait, and finalize phases".
/// @warning Cold path only; blocks while waiting and sends signals.
[[nodiscard]] core::Status
stop_suite(std::string_view suite_dir,
           std::optional<std::uint64_t> grace_override = std::nullopt) noexcept;

} // namespace nexigen::exec
