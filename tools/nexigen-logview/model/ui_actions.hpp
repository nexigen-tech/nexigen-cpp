// nexigen-logview — UI actions extracted for headless verification.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <nexigen/core/result.hpp>

#include "inspector_text.hpp"
#include "log_view_model.hpp"

namespace nexigen::logview::ui {

/// @brief Per-view scroll position and auto-follow toggle.
///
/// The UI keeps one instance per Log view and shares it with headless tests so
/// the navigation actions can be verified without an OpenGL context.
struct ViewScrollState {
    bool auto_scroll{true};                   ///< Operator toggle for tail following.
    std::optional<std::uint64_t> scroll_to{}; ///< Pending row to reveal on the next frame.
};

/// @brief Whether opening a file while live needs the replacement confirmation.
/// @param live Whether live capture is running.
/// @param retained Number of retained history records.
/// @return `true` when the UI must ask before clearing the live history.
[[nodiscard]] bool open_requires_confirmation(bool live, std::size_t retained) noexcept;

/// @brief Replace the current history with a file import.
/// @param model Target model.
/// @param path File to open.
/// @return `Status{}` when the import session started.
[[nodiscard]] nexigen::core::Status open_file(LogViewModel& model,
                                              const std::string& path) noexcept;

/// @brief Oldest record currently visible in one view.
/// @param view Log view to inspect.
/// @return Absolute record number, or `std::nullopt` for an empty view.
[[nodiscard]] std::optional<std::uint64_t> first_visible_sequence(const LogView& view) noexcept;

/// @brief Newest record currently visible in one view.
/// @param view Log view to inspect.
/// @return Absolute record number, or `std::nullopt` for an empty view.
[[nodiscard]] std::optional<std::uint64_t> last_visible_sequence(const LogView& view) noexcept;

/// @brief Whether auto-scroll should move the table to the newest row.
/// @param enabled Operator toggle.
/// @param live Whether the view follows the live ring.
/// @param paused Whether the displayed range is paused.
/// @return `true` only for an enabled, unpaused LIVE view.
[[nodiscard]] bool auto_scroll_active(bool enabled, bool live, bool paused) noexcept;

/// @brief Show the oldest visible record and turn auto-scroll off.
/// @param view Log view to navigate.
/// @param state Per-view scroll state to update.
/// @note Does nothing for an empty view.
void navigate_first(const LogView& view, ViewScrollState& state) noexcept;

/// @brief Show the newest visible record without changing the toggle.
/// @param view Log view to navigate.
/// @param state Per-view scroll state to update.
/// @note Does nothing for an empty view.
void navigate_last(const LogView& view, ViewScrollState& state) noexcept;

/// @brief Flip the auto-scroll toggle, revealing the newest row when it turns on.
/// @param view Log view to inspect.
/// @param state Per-view scroll state to update.
void toggle_auto_scroll(const LogView& view, ViewScrollState& state) noexcept;

/// @brief Whether a manual scroll away from the tail must turn auto-scroll off.
/// @param enabled Operator toggle.
/// @param moved_away Whether the operator scrolled away from the newest row.
/// @return `true` when the toggle must be cleared.
[[nodiscard]] bool manual_scroll_disables_auto_scroll(bool enabled, bool moved_away) noexcept;

/// @brief Whether Log-window keyboard shortcuts may run.
/// @param log_view_focused Whether any Log window has ImGui focus.
/// @param want_text_input Whether an ImGui text field is consuming keys.
/// @return `true` only when a Log window owns focus and no text field is active.
[[nodiscard]] bool shortcuts_allowed(bool log_view_focused, bool want_text_input) noexcept;

/// @brief Clipboard text for the currently selected record.
/// @param model Source model.
/// @return Clipboard text, or an empty string when nothing usable is selected.
[[nodiscard]] std::string copy_selected(LogViewModel& model);

/// @brief Whether the selected record's payload may have been truncated.
/// @param model Source model.
/// @return `true` when the selected payload filled the frame.
[[nodiscard]] bool selected_payload_truncated(const LogViewModel& model) noexcept;

} // namespace nexigen::logview::ui
