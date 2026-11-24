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
    bool view_test{false};    ///< Exercise multi-view docking, clone, and limits.
    bool focus_test{false};   ///< Exercise keyboard focus routing under Xvfb.
    float ui_scale{0.0F};     ///< Explicit UI scale; zero means auto-detect.
    std::string open_path{};  ///< File used by the actions test.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with live, smoke, actions, input, and scale flags.
    [[nodiscard]] std::string to_string() const {
        return std::format("UiOptions(live={}, smoke={}, actions={}, input={}, views={}, focus={}, "
                           "scale={:.2f})",
                           live, smoke, actions_test, input_test, view_test, focus_test, ui_scale);
    }
};

/// @brief Run the GUI until the window closes.
/// @param model Configured model.
/// @param options Launch options.
/// @return Process exit code.
int run_ui(LogViewModel& model, const UiOptions& options);

} // namespace nexigen::logview
