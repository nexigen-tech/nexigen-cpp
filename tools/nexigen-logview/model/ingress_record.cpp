#include "ingress_record.hpp"

#include <format>

namespace nexigen::logview {

std::string IngressRecord::to_string() const {
    return std::format("IngressRecord(kind={}, lost_before={}, frame={})", kind, lost_before,
                       frame.to_string());
}

} // namespace nexigen::logview
