// nexigen::runner — suite report builders and file writers
// (change refactor-app-suite-layers; spec runner/app-suite).
//
// COLD PATH ONLY. These functions own the `suite.json`,
// `suite-state.json`, and `suite-exit.json` contract so orchestration code
// never assembles JSON directly.
#pragma once

#include <cstdint>
#include <string_view>

#include <nexigen/runner/suite_exit.hpp>
#include <nexigen/runner/suite_manifest.hpp>
#include <nexigen/runner/suite_metadata.hpp>
#include <nexigen/runner/suite_status.hpp>

namespace nexigen::runner::detail {

/// Build the `suite.json` record for a fresh supervisor process.
[[nodiscard]] SuiteMetadata build_suite_metadata(const SuiteManifest& manifest, std::int32_t pid,
                                                 std::uint64_t start_mono_ns);

/// Write `<suite_dir>/suite.json`. Returns false after a failed write; the
/// caller owns the start-report and process-exit policy.
[[nodiscard]] bool write_suite_metadata_file(const SuiteMetadata& metadata,
                                             std::string_view suite_dir);

/// Write one `suite-state.json` document atomically.
void write_suite_status_file(const SuiteStatus& status, std::string_view state_path) noexcept;

/// Write one `suite-exit.json` document atomically.
void write_suite_exit_file(const SuiteExit& exit, std::string_view exit_path) noexcept;

/// Write the terminal records used when the supervisor cannot finalize a stop:
/// aggregate + every application `stopped`, and `suite-exit.json` with reason
/// `forced_stop`.
void write_terminal_suite_state(const SuiteMetadata& metadata, std::string_view state_path,
                                std::string_view exit_path);

} // namespace nexigen::runner::detail
