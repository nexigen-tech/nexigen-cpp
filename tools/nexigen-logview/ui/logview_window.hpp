// nexigen-logview — GLFW/ImGui window.
#pragma once

#include <format>
#include <string>

#include "model/log_view_model.hpp"

namespace nexigen::logview {

/// @brief UI launch options.
struct UiOptions {
    bool live{true};          ///< Attach to the log service.
    bool smoke{false};        ///< Render two frames and exit (CI smoke).
    bool actions_test{false}; ///< Exercise copy/truncation/open actions and exit.
    bool input_test{false};   ///< Drive scripted ImGui clicks and exit.
    std::string open_path{};  ///< File used by the actions test.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with live and smoke flags.
    [[nodiscard]] std::string to_string() const {
        return std::format("UiOptions(live={}, smoke={})", live, smoke);
    }
};

/// @brief Run the GUI until the window closes.
/// @param model Configured model.
/// @param options Launch options.
/// @return Process exit code.
int run_ui(LogViewModel& model, const UiOptions& options);

} // namespace nexigen::logview
