// nexigen::logging — rotation policy configuration (spec logging/file-logger).
#pragma once

#include <cstdint>
#include <string>

#include <nexigen/logging/rotation_schedule.hpp>

namespace nexigen::logging {

/// Rotation policy: None (single fixed directory), Hourly, Daily, or Custom
/// epoch-anchored windows of custom_ns.
struct RotationConfig {
    RotationSchedule kind{RotationSchedule::Hourly};
    std::uint64_t custom_ns{0}; ///< required when kind == Custom

    /// Cold-path diagnostic (mandate 9): schedule + custom period.
    [[nodiscard]] std::string to_string() const {
        return "RotationConfig(kind=" + std::string(logging::to_string(kind)) +
               ", custom_ns=" + std::to_string(custom_ns) + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const RotationConfig& cfg) {
    return cfg.to_string();
}

} // namespace nexigen::logging
