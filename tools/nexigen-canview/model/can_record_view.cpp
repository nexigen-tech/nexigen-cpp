#include "can_record_view.hpp"

#include <format>

namespace nexigen::canview {

std::string CanRecordView::to_string() const {
    if (message == nullptr) {
        return std::format("CanRecordView(seq={}, kind={})", seq, kind());
    }
    return std::format("CanRecordView(seq={}, kind={}, channel={}, id=0x{:x}, extended={})", seq,
                       kind(), message->channel_id, message->can_id, message->extended);
}

} // namespace nexigen::canview
