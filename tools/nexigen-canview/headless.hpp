// nexigen-canview — headless text/JSONL runner.
#pragma once

#include "canview_config.hpp"
#include "cli.hpp"

namespace nexigen::canview {

/// @brief Run without a window.
/// @param options Parsed command line.
/// @param config Effective configuration.
/// @return Process exit code.
[[nodiscard]] int run_headless(const CliOptions& options, const CanViewConfig& config);

} // namespace nexigen::canview
