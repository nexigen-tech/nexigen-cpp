// nexigen-logview — GL-free headless mode.
#pragma once

#include "cli.hpp"
#include "model/log_view_model.hpp"

namespace nexigen::logview {

/// @brief Run the headless dump path.
///
/// Handles `--offline`, `--mock`, and live capture without OpenGL. One line is
/// printed per ingested record, bounded by `--max-records`.
///
/// @param model Configured model.
/// @param options Parsed command line.
/// @return Process exit code.
int run_headless(LogViewModel& model, const CliOptions& options);

/// @brief Dump up to `limit` retained records to stdout.
/// @param model Model to dump.
/// @param limit Maximum records; zero means every retained record.
/// @return Number of lines printed.
std::uint64_t dump_records(LogViewModel& model, std::uint64_t limit);

} // namespace nexigen::logview
