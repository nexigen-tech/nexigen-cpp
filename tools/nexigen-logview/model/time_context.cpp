#include "time_context.hpp"

#include <format>

namespace nexigen::logview {

std::string TimeContext::to_string() const {
    return std::format(
        "TimeContext(time_base={}, offset_ns={}, anchor={}, threshold_ns={}, mixed_boot={})",
        time_base_attached, utc_offset_ns, anchor_ns.has_value() ? *anchor_ns : 0LL,
        anomaly_threshold_ns, mixed_boot);
}

std::string to_string(const TimeContext& context) {
    return context.to_string();
}

} // namespace nexigen::logview
