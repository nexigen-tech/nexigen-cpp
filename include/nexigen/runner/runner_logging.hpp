// nexigen::runner — runner-owned logging and CPU affinity
// (change nexigen-run-logging; spec runner/logging).
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
#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/logging_settings.hpp>

namespace nexigen::runner {

/// Load `<global_settings_path>/logging.json` through the logging-settings
/// pipeline. An empty path leaves defaults (no file loaded).
[[nodiscard]] core::Status load_runner_logging_settings(
    std::string_view global_settings_path,
    logging::Settings& out) noexcept;

/// Initialize the process-local runner logger. `level` is the already-resolved
/// effective level; `logger_cpus`, when non-empty, overrides the file's
/// `flush_cpu_affinity`. The primary directory is tried first, then
/// `<cwd>/.nexigen-run`.
[[nodiscard]] core::Status init_runner_logging(
    std::string_view primary_dir,
    const logging::Settings& settings,
    logging::LogLevel level,
    std::span<const std::uint32_t> logger_cpus = {},
    std::string_view runner_name = {}) noexcept;

/// Flush and destroy the logger so no worker thread survives a fork.
void suspend_runner_logging() noexcept;

/// Recreate the logger with the stored directory/settings/level/CPU list.
[[nodiscard]] core::Status resume_runner_logging() noexcept;

/// Flush accepted records. Returns success when logging is inactive.
[[nodiscard]] core::Status flush_runner_logging() noexcept;

/// Emit one runner record; no-op when logging is inactive.
void log_runner(logging::LogLevel level, std::string_view message) noexcept;

/// Warning helper: writes a runner warning record when active, otherwise one
/// stderr line.
void warn_runner(std::string_view message) noexcept;

[[nodiscard]] bool runner_logging_active() noexcept;

/// `<base_dir>/runner.jsonl` when active, otherwise empty.
[[nodiscard]] std::string runner_logging_path();

/// Post-pin worker mask when active, otherwise 0.
[[nodiscard]] std::uint64_t runner_logging_writer_mask() noexcept;

/// Flush and destroy the logger; idempotent.
void shutdown_runner_logging() noexcept;

/// Pin the calling thread to `cpus`; empty means "leave untouched".
[[nodiscard]] core::Status apply_runner_affinity(
    std::span<const std::uint32_t> cpus,
    std::string_view context = {}) noexcept;

/// Reset the calling thread to all online CPUs. Used in forked children so a
/// runner control pin never leaks into application processes.
[[nodiscard]] core::Status reset_runner_affinity() noexcept;

} // namespace nexigen::runner
