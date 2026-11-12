// nexigen::logging — rotation policy configuration (spec logging/file-logger).
#pragma once

#include <cstdint>
#include <string>

#include <nexigen/logging/rotation_schedule.hpp>

namespace nexigen::logging {

/// @brief Declarative rotation policy copied into `FileLoggerConfig` at construction.
///
/// Cold-path vocabulary: `FileLogger::initialize()` validates it once and the
/// background writer derives every boundary from it, so no producer thread ever
/// reads these fields. The struct owns no resources and is trivially copyable.
///
/// @note Spec `logging/file-logger`, requirement "Hitch-free scheduled directory rotation".
struct RotationConfig {
    /// @brief Schedule selector; the `Custom` enumerator makes `custom_ns` authoritative.
    RotationSchedule kind{RotationSchedule::Hourly};
    /// @brief Custom epoch-anchored window length in nanoseconds.
    ///
    /// Meaningful only when `kind == RotationSchedule::Custom`; the writer
    /// ignores it for the calendar-based schedules.
    std::uint64_t custom_ns{0};

    /// @brief Cold-path diagnostic rendering of the schedule and custom period.
    ///
    /// @return Text of the form `RotationConfig(kind=<name>, custom_ns=<n>)`.
    /// @note Project mandate 9 (`to_string` on every public value type).
    [[nodiscard]] std::string to_string() const {
        return "RotationConfig(kind=" + std::string(logging::to_string(kind)) +
               ", custom_ns=" + std::to_string(custom_ns) + ")";
    }
};

/// @brief Free `to_string` entry point for the rotation policy (mandate 9).
///
/// @param cfg Policy to render.
/// @return Exactly `cfg.to_string()`.
/// @note Project mandate 9; ADL-friendly mirror of the member function.
[[nodiscard]] inline std::string to_string(const RotationConfig& cfg) {
    return cfg.to_string();
}

} // namespace nexigen::logging
