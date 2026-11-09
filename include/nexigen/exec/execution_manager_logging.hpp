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

/// Load `<global_settings_path>/logging.json` through the logging-settings
/// pipeline. An empty path leaves defaults (no file loaded).
[[nodiscard]] core::Status load_execution_manager_logging_settings(std::string_view global_settings_path,
                                                        logging::Settings& out) noexcept;

/// Initialize the process-local execution_manager logger. `level` is the already-resolved
/// effective level; an explicit `logger_thread` overrides the file's worker
/// affinity. The primary directory is tried first, then
/// `<cwd>/.nexigen-exec`.
[[nodiscard]] core::Status init_execution_manager_logging(std::string_view primary_dir,
                                               const logging::Settings& settings,
                                               core::LogLevel level,
                                               const core::ThreadSpec& logger_thread = {},
                                               std::string_view execution_manager_name = {}) noexcept;

/// Flush and destroy the logger so no worker thread survives a fork.
void suspend_execution_manager_logging() noexcept;

/// Recreate the logger with the stored directory/settings/level/thread spec.
[[nodiscard]] core::Status resume_execution_manager_logging() noexcept;

/// Flush accepted records. Returns success when logging is inactive.
[[nodiscard]] core::Status flush_execution_manager_logging() noexcept;

/// Emit one execution_manager record; no-op when logging is inactive.
void log_execution_manager(core::LogLevel level, std::string_view message) noexcept;

/// Warning helper: writes a execution_manager warning record when active, otherwise one
/// stderr line.
void warn_execution_manager(std::string_view message) noexcept;

[[nodiscard]] bool execution_manager_logging_active() noexcept;

/// `<base_dir>/execution_manager.jsonl` when active, otherwise empty.
[[nodiscard]] std::string execution_manager_logging_path();

/// Post-pin worker mask when active, otherwise 0.
[[nodiscard]] std::uint64_t execution_manager_logging_writer_mask() noexcept;

/// Flush and destroy the logger; idempotent.
void shutdown_execution_manager_logging() noexcept;

/// Apply a full thread spec on the calling thread before its control loop,
/// exposing the per-attribute application report for diagnostics.
[[nodiscard]] core::Status apply_execution_manager_thread_spec(const core::ThreadSpec& spec,
                                                    core::ThreadApplyReport& report,
                                                    std::string_view context = {}) noexcept;

/// Apply a full thread spec on the calling thread before its control loop.
[[nodiscard]] core::Status apply_execution_manager_thread_spec(const core::ThreadSpec& spec,
                                                    std::string_view context = {}) noexcept;

/// Human-readable per-attribute rendering of a thread apply report for
/// operator-facing warnings (name/affinity/scheduler + first error).
[[nodiscard]] std::string describe_thread_apply_report(const core::ThreadApplyReport& report);

/// Legacy CPU-list adapter; empty means "leave untouched".
[[nodiscard]] core::Status apply_execution_manager_affinity(std::span<const std::uint32_t> cpus,
                                                 std::string_view context = {}) noexcept;

/// Reset the calling thread to all online CPUs. Used in forked children so a
/// execution_manager control pin never leaks into application processes.
[[nodiscard]] core::Status reset_execution_manager_affinity() noexcept;

} // namespace nexigen::exec
