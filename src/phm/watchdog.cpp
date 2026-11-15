// nexigen::phm — watchdog feed policy implementation (change
// close-phm-safety-loop).
#include <nexigen/phm/watchdog.hpp>

#include <utility>

namespace nexigen::phm {

namespace {

constexpr const char* k_default_device = "/dev/watchdog";

} // namespace

core::Result<Watchdog> Watchdog::create(WatchdogDevice& device,
                                        const WatchdogConfig& config) noexcept {
    if (!config.enabled) {
        return Watchdog(device, config, 0);
    }
    const std::string_view path = config.device.empty() ? std::string_view(k_default_device)
                                                        : std::string_view(config.device);
    if (const core::Status opened = device.open(path); !opened) {
        return std::unexpected(opened.error());
    }
    auto granted = device.set_timeout(config.timeout_ms);
    if (!granted) {
        static_cast<void>(device.disarm());
        return std::unexpected(granted.error());
    }
    return Watchdog(device, config, *granted);
}

Watchdog::Watchdog(WatchdogDevice& device, WatchdogConfig config, std::uint32_t granted_ms) noexcept
    : device_(&device), config_(std::move(config)),
      state_(config_.enabled ? WatchdogState::Armed : WatchdogState::Disabled),
      granted_ms_(granted_ms), feeding_(config_.enabled) {}

void Watchdog::tick(Clock::time_point now, bool any_expired) noexcept {
    if (state_ == WatchdogState::Disabled) {
        return;
    }
    const auto period = std::chrono::milliseconds(config_.feed_period_ms);
    const bool progressed = !have_tick_ || (now - last_tick_) <= period;
    last_tick_ = now;
    have_tick_ = true;

    if (any_expired) {
        feeding_ = false;
        state_ = WatchdogState::Stopped;
        return;
    }
    if (!progressed) {
        return; // Stalled loop: skip this feed and wait for the next clean cycle.
    }
    if (!feeding_) {
        feeding_ = true;
        state_ = WatchdogState::Armed;
    }
    if (have_feed_ && (now - last_feed_) < period) {
        return;
    }
    if (!device_->feed()) {
        feeding_ = false;
        state_ = WatchdogState::Stopped;
        return;
    }
    ++feeds_;
    last_feed_ = now;
    have_feed_ = true;
}

void Watchdog::stop_feeding() noexcept {
    if (state_ == WatchdogState::Disabled) {
        return;
    }
    feeding_ = false;
    state_ = WatchdogState::Stopped;
}

void Watchdog::disarm() noexcept {
    if (state_ == WatchdogState::Disabled) {
        return;
    }
    static_cast<void>(device_->disarm());
    feeding_ = false;
    state_ = WatchdogState::Stopped;
}

std::string Watchdog::device_path() const {
    return config_.device.empty() ? std::string(k_default_device) : config_.device;
}

std::string Watchdog::to_string() const {
    return std::format("Watchdog(device={}, timeout_ms={}, feeds={}, state={})", device_path(),
                       granted_ms_, feeds_, nexigen::phm::to_string(state_));
}

} // namespace nexigen::phm
