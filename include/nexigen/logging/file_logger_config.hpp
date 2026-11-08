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
#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/rotation_config.hpp>
#include <nexigen/logging/settings.hpp>
#include <nexigen/logging/throttle_config.hpp>

namespace nexigen::logging {

/// Construction-time configuration. All views are copied into fixed buffers
/// during initialize(); lengths beyond the buffers are rejected.
struct FileLoggerConfig {
    std::string app_name{"nexigen"};    ///< JSON "app" field (≤ 63 B)
    std::string base_dir{};             ///< required, ≤ 511 B
    std::string file_name{"app.jsonl"}; ///< ≤ 63 B
    LogLevel initial_level{LogLevel::Info};
    ThrottleConfig throttle{};
    RotationConfig rotation{};
    core::ThreadSpec worker_thread{};     ///< empty = inherited worker attributes
    std::uint64_t max_file_size_bytes{0}; ///< 0 == no size-based rotation
    /// Ceiling of the worker's idle sleep (backoff grows up to it); clamped to
    /// [50, 100000] us. Maps LoggerOptions::worker_poll_interval_us.
    std::uint32_t worker_poll_interval_us{core::WorkerTuning::default_poll_interval_us};

    /// Cold-path diagnostic (mandate 9): every policy field, sub-structs inlined.
    [[nodiscard]] std::string to_string() const {
        std::string out = "FileLoggerConfig(app=";
        out += std::string(app_name);
        out += ", base_dir=" + std::string(base_dir);
        out += ", file_name=" + std::string(file_name);
        out += ", level=" + std::string(logging::to_string(initial_level));
        out += ", max_file_size_bytes=" + std::to_string(max_file_size_bytes);
        out += ", throttle=" + throttle.to_string();
        out += ", rotation=" + rotation.to_string();
        out += ", worker_thread=" + worker_thread.to_string();
        out += ", worker_poll_interval_us=" + std::to_string(worker_poll_interval_us) + ")";
        return out;
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const FileLoggerConfig& cfg) {
    return cfg.to_string();
}

/// Cold-path JSON round-trip; bodies live in src/logging/file_logger_config.cpp.
void to_json(nlohmann::json& j, const FileLoggerConfig& cfg);
void from_json(const nlohmann::json& j, FileLoggerConfig& out) noexcept;

/// Map merged settings into a file logger construction config. Returns
/// InvalidArgument when a mapped value violates the file logger's fixed-buffer
/// limits; the destination owns its strings. Body in file_logger_config.cpp.
[[nodiscard]] core::Status apply_to(const Settings& settings, FileLoggerConfig& out) noexcept;

} // namespace nexigen::logging
