// nexigen::logging — construction-time configuration for FileLogger (spec
// logging/file-logger). All views are copied into fixed buffers during
// initialize(); lengths beyond the buffers are rejected.
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/worker_tuning.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/rotation_config.hpp>
#include <nexigen/logging/settings.hpp>
#include <nexigen/logging/throttle_config.hpp>

namespace nexigen::logging {

/// @brief Construction-time configuration for the asynchronous JSON-lines file sink.
///
/// Cold-path aggregate read once by `FileLogger::initialize()`: every string view
/// is copied into a fixed writer-owned buffer, and lengths beyond those buffers
/// are rejected rather than truncated. After initialization the writer thread
/// owns all of these values, so producers never read or mutate them.
///
/// @note Spec `logging/file-logger`, requirement
///       "Asynchronous wait-free JSON-lines file output".
struct FileLoggerConfig {
    /// @brief Application name written to the JSON `app` field (at most 63 bytes).
    std::string app_name{"nexigen"};
    /// @brief Required output root directory (at most 511 bytes).
    std::string base_dir{};
    /// @brief Active file name inside each period directory (at most 63 bytes).
    std::string file_name{"app.jsonl"};
    /// @brief Severity threshold applied at initialization.
    core::LogLevel initial_level{core::LogLevel::Info};
    /// @brief Window throttle policy; `enabled = false` adds no hot-path work.
    ThrottleConfig throttle{};
    /// @brief Scheduled rotation policy; `None` keeps a single fixed directory.
    RotationConfig rotation{};
    /// @brief Writer thread attributes; an empty CPU set inherits the creator's affinity.
    core::ThreadSpec worker_thread{};
    /// @brief Size-based rotation threshold in bytes; `0` disables segment rotation.
    std::uint64_t max_file_size_bytes{0};
    /// @brief Ceiling of the worker's idle sleep (backoff grows up to it); clamped to
    ///        `[50, 100000]` us.
    ///
    /// Maps `LoggerOptions::worker_poll_interval_us`.
    std::uint32_t worker_poll_interval_us{core::WorkerTuning::default_poll_interval_us};

    /// @brief Cold-path diagnostic rendering of every policy field, with sub-structs inlined.
    ///
    /// @return Text containing the app/base/file names, level, size limit, throttle, rotation,
    ///         worker spec, and poll ceiling.
    /// @note Project mandate 9 (`to_string` on every public value type).
    [[nodiscard]] std::string to_string() const {
        std::string out = "FileLoggerConfig(app=";
        out += std::string(app_name);
        out += ", base_dir=" + std::string(base_dir);
        out += ", file_name=" + std::string(file_name);
        out += ", level=" + std::string(core::to_string(initial_level));
        out += ", max_file_size_bytes=" + std::to_string(max_file_size_bytes);
        out += ", throttle=" + throttle.to_string();
        out += ", rotation=" + rotation.to_string();
        out += ", worker_thread=" + worker_thread.to_string();
        out += ", worker_poll_interval_us=" + std::to_string(worker_poll_interval_us) + ")";
        return out;
    }
};

/// @brief Free `to_string` entry point for the file logger config (mandate 9).
///
/// @param cfg Config to render.
/// @return Exactly `cfg.to_string()`.
/// @note Project mandate 9; ADL-friendly mirror of the member function.
[[nodiscard]] inline std::string to_string(const FileLoggerConfig& cfg) {
    return cfg.to_string();
}

/// @brief Cold-path JSON writer for the file logger configuration.
///
/// @param j Destination JSON object; overwritten with the config's field map.
/// @param cfg Config to serialize.
/// @note Project mandate 10 (JSON for every configuration value type); the body lives in
///       `file_logger_config.cpp`.
void to_json(nlohmann::json& j, const FileLoggerConfig& cfg);
/// @brief Cold-path JSON reader that is hostile-input safe.
///
/// Absent or ill-typed fields leave the corresponding member untouched, so a bad
/// document can never erase a valid configuration.
///
/// @param j Source JSON object.
/// @param out Destination config, updated field by field.
/// @note Project mandate 10; non-throwing under `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, FileLoggerConfig& out) noexcept;

/// @brief Map merged settings onto a file logger construction config.
///
/// Copies the mapped level, worker spec, poll ceiling, size limit, throttle, and
/// rotation policy; the destination owns its strings.
///
/// @param settings Merged settings produced by `load_logging_settings`.
/// @param out Destination config, overwritten in place.
/// @return Empty `core::Status` on success, or `ErrorCode::InvalidArgument` when a mapped
///         value violates the file logger's fixed-buffer limits.
/// @note Spec `logging/logging-settings`, requirement
///       "Merged settings apply to logger configuration"; the body lives in
///       `file_logger_config.cpp`.
[[nodiscard]] core::Status apply_to(const Settings& settings, FileLoggerConfig& out) noexcept;

} // namespace nexigen::logging
