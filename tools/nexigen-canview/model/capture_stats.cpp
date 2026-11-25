#include "capture_stats.hpp"

#include <format>

namespace nexigen::canview {

std::string CaptureStats::to_string() const {
    return std::format(
        "CaptureStats(received={}, bus_idle={}, synthetic={}, overflow={}, mismatched={}, "
        "duplicates={}, resets={}, hw_gaps={}, transport={}, ingress_losses={}, driver_drops={}, "
        "active={}, failed={}, ingress={}, error={})",
        received, bus_idle, synthetic, overflow, mismatched, duplicates, resets, hardware_gaps,
        transport_losses, ingress_losses, driver_drops, active_channels, failed_channels,
        ingress_size, last_error);
}

} // namespace nexigen::canview
