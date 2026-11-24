#include "ui_actions.hpp"

#include "importer.hpp"
#include "time_formatter.hpp"

namespace nexigen::logview::ui {

bool open_requires_confirmation(bool live, std::size_t retained) noexcept {
    return live && retained > 0U;
}

std::optional<std::uint64_t> first_visible_sequence(const LogView& view) noexcept {
    if (view.display_count() == 0U) {
        return std::nullopt;
    }
    return view.display_at(0);
}

std::optional<std::uint64_t> last_visible_sequence(const LogView& view) noexcept {
    if (view.display_count() == 0U) {
        return std::nullopt;
    }
    return view.display_at(view.display_count() - 1U);
}

bool auto_scroll_active(bool enabled, bool live, bool paused) noexcept {
    return enabled && live && !paused;
}

void navigate_first(const LogView& view, ViewScrollState& state) noexcept {
    if (const auto seq = first_visible_sequence(view); seq.has_value()) {
        state.scroll_to = seq;
        state.auto_scroll = false;
    }
}

void navigate_last(const LogView& view, ViewScrollState& state) noexcept {
    if (const auto seq = last_visible_sequence(view); seq.has_value()) {
        state.scroll_to = seq;
    }
}

void toggle_auto_scroll(const LogView& view, ViewScrollState& state) noexcept {
    state.auto_scroll = !state.auto_scroll;
    if (state.auto_scroll) {
        if (const auto seq = last_visible_sequence(view); seq.has_value()) {
            state.scroll_to = seq;
        }
    }
}

bool manual_scroll_disables_auto_scroll(bool enabled, bool moved_away) noexcept {
    return enabled && moved_away;
}

bool shortcuts_allowed(bool log_view_focused, bool want_text_input) noexcept {
    return log_view_focused && !want_text_input;
}

nexigen::core::Status open_file(LogViewModel& model, const std::string& path) noexcept {
    ImportStats stats{};
    model.clear_history();
    return model.begin_import(path, stats);
}

std::string copy_selected(LogViewModel& model) {
    const auto selected = model.last_selected();
    if (!selected.has_value()) {
        return {};
    }
    const auto view = model.ring().find(*selected);
    if (!view.has_value()) {
        return {};
    }
    const FormattedTime stamp = TimeFormatter::format_timestamp(
        view->frame->timestamp_ns, model.config().timestamp_mode, model.time());
    return inspector_clipboard(*view, stamp);
}

bool selected_payload_truncated(const LogViewModel& model) noexcept {
    const auto selected = model.last_selected();
    if (!selected.has_value()) {
        return false;
    }
    const auto view = model.ring().find(*selected);
    return view.has_value() && payload_may_be_truncated(*view->frame);
}

} // namespace nexigen::logview::ui
