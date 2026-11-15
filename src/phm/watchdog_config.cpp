// nexigen::phm — watchdog section serialization (change close-phm-safety-loop).
#include <nexigen/phm/watchdog_config.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::phm {

std::string WatchdogConfig::to_string() const {
    return std::format("WatchdogConfig(enabled={}, device={}, timeout_ms={}, feed_period_ms={})",
                       enabled, device.empty() ? "/dev/watchdog" : device, timeout_ms,
                       feed_period_ms);
}

void to_json(nlohmann::json& j, const WatchdogConfig& config) {
    j = nlohmann::json{{"enabled", config.enabled},
                       {"device", config.device},
                       {"timeout_ms", config.timeout_ms},
                       {"feed_period_ms", config.feed_period_ms}};
}

void from_json(const nlohmann::json& j, WatchdogConfig& config) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_bool(j, "enabled", config.enabled);
    core::detail::read_string(j, "device", config.device);
    core::detail::read_unsigned(j, "timeout_ms", config.timeout_ms);
    core::detail::read_unsigned(j, "feed_period_ms", config.feed_period_ms);
}

} // namespace nexigen::phm
