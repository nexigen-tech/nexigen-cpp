// nexigen-canview — ImGui/GLFW workspace.
#pragma once

#include "canview_config.hpp"
#include "cli.hpp"

namespace nexigen::canview {

/// @brief Run the desktop workspace.
/// @param options Parsed command line.
/// @param config Effective configuration.
/// @return Process exit code.
[[nodiscard]] int run_gui(const CliOptions& options, CanViewConfig& config);

} // namespace nexigen::canview
