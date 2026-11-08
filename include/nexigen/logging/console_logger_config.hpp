// nexigen::logging — construction-time configuration shared by every
// ConsoleLogger instantiation (spec logging/console-logger).
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/worker_tuning.hpp>
#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/settings.hpp>

namespace nexigen::logging {

/// Construction-time configuration shared by every ConsoleLogger instantiation.
struct ConsoleLoggerConfig {
    LogLevel initial_level{LogLevel::Info};
    core::ThreadSpec worker_thread{}; ///< empty = inherited worker attributes
    /// Ceiling of the worker's idle sleep; clamped to [50, 100000] us.
    std::uint32_t worker_poll_interval_us{core::WorkerTuning::default_poll_interval_us};

    /// Cold-path diagnostic (mandate 9): initial threshold + worker poll ceiling.
    [[nodiscard]] std::string to_string() const {
        return "ConsoleLogger::Config(level=" + std::string(logging::to_string(initial_level)) +
               ", worker_thread=" + worker_thread.to_string() +
               ", worker_poll_interval_us=" + std::to_string(worker_poll_interval_us) + ")";
    }

    /// Free form via ADL (nested types are non-deducible for template
    /// helpers; a class-scope friend is the standard discovery route).
    [[nodiscard]] friend std::string to_string(const ConsoleLoggerConfig& cfg) {
        return cfg.to_string();
    }
};

/// Cold-path JSON round-trip; bodies live in src/logging/console_logger_config.cpp.
void to_json(nlohmann::json& j, const ConsoleLoggerConfig& cfg);
void from_json(const nlohmann::json& j, ConsoleLoggerConfig& out) noexcept;

/// Apply merged settings to a console logger config (spec logging/logging-settings
/// "Merged settings apply to logger configuration"): the console sink carries
/// the initial level and the worker poll interval.
[[nodiscard]] inline core::Status apply_to(const Settings& settings,
                                           ConsoleLoggerConfig& out) noexcept {
    out.initial_level = settings.options.initial_log_level;
    if (settings.worker_thread_set) {
        out.worker_thread = settings.options.worker_thread;
        if (!settings.worker_cpus_set) {
            out.worker_thread.set_cpus(core::CpuSet{});
        }
    }
    out.worker_poll_interval_us = settings.options.worker_poll_interval_us;
    return {};
}

} // namespace nexigen::logging
