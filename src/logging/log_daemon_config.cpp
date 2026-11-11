// nexigen::logging — cold-path JSON serialization for LogDaemonConfig
// (spec logging/log-daemon; change refactor-complexity-hotspots).
//
// Strictly non-throwing under `JSON_NOEXCEPTION=1`: the object is parsed into a
// local and assigned only on success.
// nlohmann/json headers trigger -Wnull-dereference under -O3 (pre-existing
// upstream issue). Suppress for this TU only — the JSON code is cold-path.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/core/affinity.hpp>
#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/log_daemon_config.hpp>

namespace nexigen::logging {

void to_json(nlohmann::json& j, const LogDaemonConfig& cfg) {
    j = nlohmann::json{
        {"service_name", cfg.service_name},
        {"log_directory", cfg.log_directory},
        {"max_file_size_mb", cfg.max_file_size_mb},
        {"max_archive_files", cfg.max_archive_files},
        {"min_log_level_storage", cfg.min_log_level_storage},
        {"bg_cpu_affinity", cfg.bg_cpu_affinity},
        {"batch_flush_interval_ms", cfg.batch_flush_interval_ms},
    };
}

void from_json(const nlohmann::json& j, LogDaemonConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    LogDaemonConfig parsed{};
    core::detail::read_string(j, "service_name", parsed.service_name);
    core::detail::read_string(j, "log_directory", parsed.log_directory);
    core::detail::read_unsigned(j, "max_file_size_mb", parsed.max_file_size_mb);
    core::detail::read_unsigned(j, "max_archive_files", parsed.max_archive_files);
    core::detail::read_unsigned(j, "batch_flush_interval_ms", parsed.batch_flush_interval_ms);

    // Non-throwing enum parse; an unknown name keeps the default threshold.
    std::string level_name;
    if (core::detail::read_string(j, "min_log_level_storage", level_name)) {
        core::LogLevel level = core::LogLevel::Warning;
        if (core::from_string(std::string_view(level_name), level)) {
            parsed.min_log_level_storage = level;
        }
    }
    core::detail::read_object(j, "bg_cpu_affinity",
                              [&parsed](const nlohmann::json& affinity) noexcept {
                                  from_json(affinity, parsed.bg_cpu_affinity);
                              });
    out = std::move(parsed);
}

} // namespace nexigen::logging
