// nexigen::logging — window throttle parameters (design D9; spec
// logging/file-logger). Public vocabulary consumed by FileLoggerConfig and
// Producer::configure_throttle.
#pragma once

#include <cstdint>
#include <string>

namespace nexigen::logging {

/// Window throttle parameters. Validation belongs to FileLogger::initialize;
/// configure_throttle copies as-is and only enables when max/window are
/// non-zero.
struct ThrottleConfig {
    bool enabled{false};
    std::uint64_t max_records_per_window{0}; ///< accepted budget per window
    std::uint64_t window_ns{0};              ///< window length, monotonic domain

    /// Cold-path diagnostic (mandate 9): full policy dump.
    [[nodiscard]] std::string to_string() const {
        return "ThrottleConfig(enabled=" + std::string(enabled ? "true" : "false") +
               ", max_records_per_window=" + std::to_string(max_records_per_window) +
               ", window_ns=" + std::to_string(window_ns) + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const ThrottleConfig& cfg) {
    return cfg.to_string();
}

} // namespace nexigen::logging
