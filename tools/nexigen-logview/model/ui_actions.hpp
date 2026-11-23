// nexigen-logview — UI actions extracted for headless verification.
#pragma once

#include <cstddef>
#include <string>

#include <nexigen/core/result.hpp>

#include "inspector_text.hpp"
#include "log_view_model.hpp"

namespace nexigen::logview::ui {

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

/// @brief Clipboard text for the currently selected record.
/// @param model Source model.
/// @return Clipboard text, or an empty string when nothing usable is selected.
[[nodiscard]] std::string copy_selected(LogViewModel& model);

/// @brief Whether the selected record's payload may have been truncated.
/// @param model Source model.
/// @return `true` when the selected payload filled the frame.
[[nodiscard]] bool selected_payload_truncated(const LogViewModel& model) noexcept;

} // namespace nexigen::logview::ui
