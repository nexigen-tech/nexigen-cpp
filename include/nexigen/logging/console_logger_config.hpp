// nexigen::logging — construction-time configuration shared by every
// ConsoleLogger instantiation (spec logging/console-logger).
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/worker_tuning.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/settings.hpp>

namespace nexigen::logging {

/// @brief Construction-time configuration shared by every `ConsoleLogger` instantiation.
///
/// Cold-path aggregate: `ConsoleLogger::initialize()` reads the level and the
/// worker attributes once, then the writer thread owns them. Producers never
/// touch this struct — runtime level changes go through the logger's own atomic
/// setter instead.
///
/// @note Spec `logging/console-logger`, requirement
///       "Console logger config exposes the poll ceiling".
struct ConsoleLoggerConfig {
    /// @brief Severity threshold applied at initialization.
    core::LogLevel initial_level{core::LogLevel::Info};
    /// @brief Writer thread attributes; an empty CPU set inherits the creator's affinity.
    core::ThreadSpec worker_thread{};
    /// @brief Ceiling of the worker's idle sleep; clamped to `[50, 100000]` us.
    std::uint32_t worker_poll_interval_us{core::WorkerTuning::default_poll_interval_us};

    /// @brief Cold-path diagnostic rendering of the initial threshold and worker settings.
    ///
    /// @return Text of the form `ConsoleLogger::Config(level=<name>,
    ///         worker_thread=<spec>, worker_poll_interval_us=<n>)`.
    /// @note Project mandate 9 (`to_string` on every public value type).
    [[nodiscard]] std::string to_string() const {
        return "ConsoleLogger::Config(level=" + std::string(core::to_string(initial_level)) +
               ", worker_thread=" + worker_thread.to_string() +
               ", worker_poll_interval_us=" + std::to_string(worker_poll_interval_us) + ")";
    }

    /// @brief Free form via ADL (mandate 9).
    ///
    /// Nested types are non-deducible for template helpers, so a class-scope
    /// friend is the standard discovery route.
    ///
    /// @param cfg Config to render.
    /// @return Exactly `cfg.to_string()`.
    /// @note Project mandate 9.
    [[nodiscard]] friend std::string to_string(const ConsoleLoggerConfig& cfg) {
        return cfg.to_string();
    }
};

/// @brief Cold-path JSON writer for the console configuration.
///
/// @param j Destination JSON object; overwritten with the config's field map.
/// @param cfg Config to serialize.
/// @note Project mandate 10 (JSON for every configuration value type); the body lives in
///       `console_logger_config.cpp`.
void to_json(nlohmann::json& j, const ConsoleLoggerConfig& cfg);
/// @brief Cold-path JSON reader that is hostile-input safe.
///
/// Absent or ill-typed fields leave the corresponding member untouched, so the
/// call never throws.
///
/// @param j Source JSON object.
/// @param out Destination config, updated field by field.
/// @note Project mandate 10; non-throwing under `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, ConsoleLoggerConfig& out) noexcept;

/// @brief Map merged settings onto a console logger construction config.
///
/// Copies the initial level and the worker poll interval, and inherits the
/// canonical `worker_thread` spec only when the merged settings carry one;
/// without an explicit CPU set the inherited affinity is cleared so the OS
/// places the thread.
///
/// @param settings Merged settings produced by `load_logging_settings`.
/// @param out Destination config, overwritten in place; it owns its own strings.
/// @return Always an empty (success) `core::Status`; the signature keeps the uniform
///         settings-mapping shape used by every sink.
/// @note Spec `logging/logging-settings`, requirement
///       "Merged settings apply to logger configuration".
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
