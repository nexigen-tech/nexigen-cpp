#include "ui_actions.hpp"

#include "importer.hpp"
#include "time_formatter.hpp"

namespace nexigen::logview::ui {

bool open_requires_confirmation(bool live, std::size_t retained) noexcept {
    return live && retained > 0U;
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
