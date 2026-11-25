#include "can_ingress_record.hpp"

namespace nexigen::canview {

std::string CanIngressRecord::to_string() const {
    return std::format("CanIngressRecord(kind={}, channel={}, id=0x{:x}, extended={}, lost={})",
                       kind, msg.channel_id, msg.can_id, msg.extended, lost_before);
}

} // namespace nexigen::canview
