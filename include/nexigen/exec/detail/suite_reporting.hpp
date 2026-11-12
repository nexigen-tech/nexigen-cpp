// nexigen::exec — suite report builders and file writers
// (change refactor-app-suite-layers; spec execution_manager/app-suite).
//
// COLD PATH ONLY. These functions own the `suite.json`,
// `suite-state.json`, and `suite-exit.json` contract so orchestration code
// never assembles JSON directly.
#pragma once

#include <cstdint>
#include <string_view>

#include <nexigen/exec/suite_exit.hpp>
#include <nexigen/exec/suite_manifest.hpp>
#include <nexigen/exec/suite_metadata.hpp>
#include <nexigen/exec/suite_status.hpp>

namespace nexigen::exec::detail {

/// @brief Build the `suite.json` record for a fresh supervisor process.
///
/// Copies identity, roots, settings path, manifest path, and thread specs from
/// the manifest, fills the supervisor pid and start instants, and emits one
/// `SuiteAppMetadata` per application with its deterministic control instance
/// directory.
///
/// @param manifest Source suite manifest.
/// @param pid Supervisor pid recorded in the suite record.
/// @param start_mono_ns Monotonic start instant in nanoseconds.
/// @return The populated suite record.
/// @note Spec `exec/app-suite`, requirement "Suite metadata lives in a
///       deterministic suite directory".
[[nodiscard]] SuiteMetadata build_suite_metadata(const SuiteManifest& manifest, std::int32_t pid,
                                                 std::uint64_t start_mono_ns);

/// @brief Write `<suite_dir>/suite.json`.
///
/// The write is atomic and is the marker `start_suite` waits for.
///
/// @param metadata Suite record to write.
/// @param suite_dir Absolute suite directory.
/// @return `true` on a successful write; `false` after a failed write, in
///         which case the caller owns the start-report and process-exit
///         policy.
/// @note Spec `exec/app-suite`, requirement "Suite metadata lives in a
///       deterministic suite directory".
/// @note Spec `exec/app-suite`, scenario "Metadata writes are atomic".
[[nodiscard]] bool write_suite_metadata_file(const SuiteMetadata& metadata,
                                             std::string_view suite_dir);

/// @brief Write one `suite-state.json` document atomically.
///
/// @param status Aggregate and per-application state to write.
/// @param state_path Absolute destination path.
/// @note Spec `exec/app-suite`, scenario "Metadata writes are atomic".
/// @note Spec `exec/logging`, requirement "State write failures are reported"
///       (failures are reported through the warning channel by the caller).
void write_suite_status_file(const SuiteStatus& status, std::string_view state_path) noexcept;

/// @brief Write one `suite-exit.json` document atomically.
///
/// @param exit Final record to write.
/// @param exit_path Absolute destination path.
/// @note Spec `exec/app-suite`, scenario "Metadata writes are atomic".
/// @note Spec `exec/logging`, requirement "State write failures are reported".
void write_suite_exit_file(const SuiteExit& exit, std::string_view exit_path) noexcept;

/// @brief Write the terminal records used when the supervisor cannot finalize
///        a stop.
///
/// Writes a `suite-state.json` with aggregate and every application
/// `stopped`, and a `suite-exit.json` with reason `forced_stop`.
///
/// @param metadata Suite record supplying suite identity and applications.
/// @param state_path Absolute `suite-state.json` path.
/// @param exit_path Absolute `suite-exit.json` path.
/// @note Spec `exec/app-suite`, requirement "`stop_suite` is split into
///       request, wait, and finalize phases".
/// @note Spec `exec/app-suite`, scenario "Forced stop writes terminal files".
void write_terminal_suite_state(const SuiteMetadata& metadata, std::string_view state_path,
                                std::string_view exit_path);

} // namespace nexigen::exec::detail
