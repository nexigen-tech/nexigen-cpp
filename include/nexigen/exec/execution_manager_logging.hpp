// nexigen::exec — execution_manager-owned logging and CPU affinity
// (change nexigen-exec-logging; spec execution_manager/logging).
//
// COLD PATH ONLY. All functions are control-plane helpers used by the CLI and
// the detached suite supervisor. The asynchronous FileLogger worker is
// suspended and joined before every fork performed after initialization.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/settings.hpp>

namespace nexigen::exec {

/// @brief Load `<global_settings_path>/logging.json` through the logging
///        settings pipeline.
///
/// An empty path leaves defaults (no file loaded). A missing file keeps
/// defaults; an unreadable or invalid file produces a warning and does not
/// fail the command.
///
/// @param global_settings_path Absolute suite settings directory or empty.
/// @param out Receives the normalized settings, including `initial_log_level`,
///        logger worker affinity, maximum file size, and throttling.
/// @return Success or a `core::Status` describing why the file could not be
///         applied.
/// @note Spec `exec/logging`, requirement "Global logging settings supply
///       operational fields".
[[nodiscard]] core::Status
load_execution_manager_logging_settings(std::string_view global_settings_path,
                                        logging::Settings& out) noexcept;

/// @brief Initialize the process-local execution_manager logger.
///
/// `level` is the already-resolved effective level; an explicit
/// `logger_thread` overrides the file's worker affinity. The primary directory
/// is tried first, then `<cwd>/.nexigen-exec`. The file name is always
/// `execution_manager.jsonl`, scheduled rotation stays disabled, and the
/// application name comes from `execution_manager_name` (default
/// `nexigen-exec`).
///
/// @param primary_dir Preferred log directory, normally
///        `<artifacts_path>/<suite_id>`.
/// @param settings Logging settings supplying size limits and throttling.
/// @param level Effective execution_manager level.
/// @param logger_thread Logger worker placement; empty fields mean no request.
/// @param execution_manager_name Application name recorded in every line.
/// @return Success, or `SystemError` when neither location can be opened
///         (logging stays inactive and diagnostics continue on stderr).
/// @note Spec `exec/logging`, requirement "ExecutionManager diagnostics use the
///       project file logger".
/// @note Spec `exec/logging`, requirement "Log location follows the manifest
///       and suite metadata".
/// @note Spec `exec/logging`, requirement "Logging is best-effort".
[[nodiscard]] core::Status
init_execution_manager_logging(std::string_view primary_dir, const logging::Settings& settings,
                               core::LogLevel level, const core::ThreadSpec& logger_thread = {},
                               std::string_view execution_manager_name = {}) noexcept;

/// @brief Flush and destroy the logger so no worker thread survives a fork.
///
/// The stored directory, settings, level, and thread spec are retained for
/// `resume_execution_manager_logging`.
///
/// @note Spec `exec/logging`, requirement "Logger lifecycle is fork-safe".
void suspend_execution_manager_logging() noexcept;

/// @brief Recreate the logger with the stored directory/settings/level/thread
///        spec.
///
/// Called in the parent after every fork while logging was active.
///
/// @return Success when the logger is active again, or `SystemError` when it
///         cannot be recreated; supervision continues either way.
/// @note Spec `exec/logging`, requirement "Logger lifecycle is fork-safe".
/// @note Spec `exec/logging`, requirement "Logging is best-effort".
[[nodiscard]] core::Status resume_execution_manager_logging() noexcept;

/// @brief Flush accepted records.
///
/// @return Success when logging is inactive, or the logger flush result.
/// @note Spec `exec/logging`, requirement "Logger lifecycle is fork-safe".
[[nodiscard]] core::Status flush_execution_manager_logging() noexcept;

/// @brief Emit one execution_manager record.
///
/// Formatting and enqueue happen on the calling control thread only; the call
/// is a no-op when logging is inactive.
///
/// @param level Record level; records below the effective level are dropped.
/// @param message Message text, copied before the call returns.
/// @note Spec `exec/logging`, requirement "ExecutionManager logging is cold
///       path only".
void log_execution_manager(core::LogLevel level, std::string_view message) noexcept;

/// @brief Report a warning through the execution_manager warning channel.
///
/// Writes a warning-level record when logging is active, otherwise one
/// `nexigen-exec:` line on standard error. Used before returning any JSON
/// state-write error.
///
/// @param message Warning text.
/// @note Spec `exec/logging`, requirement "State write failures are reported".
void warn_execution_manager(std::string_view message) noexcept;

/// @brief Whether the execution_manager logger is currently initialized.
///
/// @return `true` when records would be written to a file.
/// @note Spec `exec/logging`, requirement "Logging is best-effort".
[[nodiscard]] bool execution_manager_logging_active() noexcept;

/// @brief Path of the active execution_manager log.
///
/// @return `<base_dir>/execution_manager.jsonl` when active, otherwise empty.
/// @note Spec `exec/logging`, requirement "ExecutionManager diagnostics use the
///       project file logger".
[[nodiscard]] std::string execution_manager_logging_path();

/// @brief Post-pin worker CPU mask of the active logger.
///
/// @return The worker mask when active, otherwise 0.
/// @note Spec `exec/logging`, requirement "Global logging settings supply
///       operational fields".
[[nodiscard]] std::uint64_t execution_manager_logging_writer_mask() noexcept;

/// @brief Flush and destroy the logger; idempotent.
///
/// Called before `_exit` so accepted records reach the file.
///
/// @note Spec `exec/logging`, requirement "Logger lifecycle is fork-safe".
void shutdown_execution_manager_logging() noexcept;

/// @brief Apply a full thread spec on the calling thread before its control
///        loop.
///
/// Applies name, affinity, and scheduler attributes as requested and exposes
/// the per-attribute outcome for diagnostics. Under the default `BestEffort`
/// mode a rejected attribute produces a warning and does not abort; a
/// `Required` attribute that cannot be applied returns `SystemError`.
///
/// @param spec Thread placement to apply; empty fields are not requested.
/// @param report Receives the per-attribute application outcome.
/// @param context Diagnostic context appended to warnings, such as
///        `control thread`.
/// @return Success (including best-effort partial application), or
///         `SystemError` when a required attribute fails.
/// @note Spec `exec/logging`, requirement "Manifest configures
///       execution_manager thread affinity".
/// @note Spec `exec/logging`, requirement "Affinity failures are non-fatal
///       warnings".
[[nodiscard]] core::Status
apply_execution_manager_thread_spec(const core::ThreadSpec& spec, core::ThreadApplyReport& report,
                                    std::string_view context = {}) noexcept;

/// @brief Apply a full thread spec on the calling thread before its control
///        loop.
///
/// Convenience overload that discards the per-attribute report.
///
/// @param spec Thread placement to apply; empty fields are not requested.
/// @param context Diagnostic context appended to warnings.
/// @return Success (including best-effort partial application), or
///         `SystemError` when a required attribute fails.
/// @note Spec `exec/logging`, requirement "Manifest configures
///       execution_manager thread affinity".
[[nodiscard]] core::Status
apply_execution_manager_thread_spec(const core::ThreadSpec& spec,
                                    std::string_view context = {}) noexcept;

/// @brief Human-readable per-attribute rendering of a thread apply report.
///
/// Names which of name, affinity, and scheduler were requested, applied, and
/// failed, plus the first error, without claiming the thread is unpinned when
/// some attributes applied.
///
/// @param report Report produced by `apply_execution_manager_thread_spec`.
/// @return Operator-facing warning text.
/// @note Spec `exec/logging`, requirement "Affinity failures are non-fatal
///       warnings".
[[nodiscard]] std::string describe_thread_apply_report(const core::ThreadApplyReport& report);

/// @brief Legacy CPU-list adapter for control-thread affinity.
///
/// An empty list means "leave untouched".
///
/// @param cpus CPU indices to apply; duplicates and out-of-range entries are
///        reported as warnings under best-effort.
/// @param context Diagnostic context appended to warnings.
/// @return Success, or `SystemError` when a required application fails.
/// @note Spec `exec/logging`, requirement "Affinity failures are non-fatal
///       warnings".
[[nodiscard]] core::Status apply_execution_manager_affinity(std::span<const std::uint32_t> cpus,
                                                            std::string_view context = {}) noexcept;

/// @brief Reset the calling thread to all online CPUs.
///
/// Used in forked children so an execution_manager control pin never leaks
/// into application processes.
///
/// @return Success or the `core::Status` from the affinity call.
/// @note Spec `exec/logging`, scenario "Applications keep their own affinity".
/// @note Spec `exec/process-supervision`, scenario "Empty CPU set retains existing reset behavior".
[[nodiscard]] core::Status reset_execution_manager_affinity() noexcept;

} // namespace nexigen::exec
