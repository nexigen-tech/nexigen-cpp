#include "inspector_text.hpp"

#include <format>

namespace nexigen::logview {

std::string inspector_clipboard(const RecordView& view, const FormattedTime& stamp) {
    return std::format(
        "index={} timestamp={} app={} level={} thread={} pid={} dropped={} version={} "
        "message={}",
        view.seq, stamp.text.data(), view.frame->app_id_text(),
        nexigen::core::to_string(view.frame->log_level), view.frame->thread_id, view.frame->pid,
        view.frame->dropped_counter, view.frame->version, view.frame->payload_text());
}

bool payload_may_be_truncated(const nexigen::logging::LogFrame& frame) noexcept {
    return frame.payload_size == sizeof(frame.payload);
}

} // namespace nexigen::logview
