// nexigen::logging — window throttle parameters (design D9; spec
// logging/file-logger). Public vocabulary consumed by FileLoggerConfig and
// Producer::configure_throttle.
#pragma once

#include <cstdint>
#include <string>

namespace nexigen::logging {

/// @brief Window throttle parameters shared by the file and iceoryx2 sinks.
///
/// Plain copyable vocabulary: the value is read during construction or
/// `configure_throttle()` and never mutated afterwards. Validation belongs to
/// `FileLogger::initialize()`; `configure_throttle` copies the struct as-is and
/// only enables throttling when both `max_records_per_window` and `window_ns`
/// are non-zero.
///
/// @note Spec `logging/file-logger`, requirement "Hot-path rate limiting".
struct ThrottleConfig {
    /// @brief Turns the window throttle on; a disabled throttle costs no hot-path work.
    bool enabled{false};
    /// @brief Accepted-record budget for each window; `0` with a non-zero
    ///        window is treated as not configured.
    std::uint64_t max_records_per_window{0};
    /// @brief Window length in nanoseconds, measured in the monotonic clock domain.
    std::uint64_t window_ns{0};

    /// @brief Cold-path diagnostic rendering of the full throttle policy.
    ///
    /// @return Text of the form `ThrottleConfig(enabled=<bool>,
    ///         max_records_per_window=<n>, window_ns=<n>)`.
    /// @note Project mandate 9 (`to_string` on every public value type).
    [[nodiscard]] std::string to_string() const {
        return "ThrottleConfig(enabled=" + std::string(enabled ? "true" : "false") +
               ", max_records_per_window=" + std::to_string(max_records_per_window) +
               ", window_ns=" + std::to_string(window_ns) + ")";
    }
};

/// @brief Free `to_string` entry point for the throttle policy (mandate 9).
///
/// @param cfg Policy to render.
/// @return Exactly `cfg.to_string()`.
/// @note Project mandate 9; ADL-friendly mirror of the member function.
[[nodiscard]] inline std::string to_string(const ThrottleConfig& cfg) {
    return cfg.to_string();
}

} // namespace nexigen::logging
