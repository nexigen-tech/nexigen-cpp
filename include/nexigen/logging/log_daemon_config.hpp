// nexigen::logging — construction-time configuration for LogDaemon
// (change add-log-daemon; spec logging/log-daemon; design D8).
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/affinity.hpp>
#include <nexigen/core/log_level.hpp>

namespace nexigen::logging {

/// LogDaemon construction-time configuration. Validated at startup; invalid
/// values are rejected before the subscriber or worker thread is created.
struct LogDaemonConfig {
    std::string service_name{"Nexigen/Log/nexigen"}; ///< subscription (≤ 511 B)
    std::string log_directory{"/var/log/nexigen"};   ///< absolute output root (≤ 511 B)
    std::uint64_t max_file_size_mb{20};              ///< 0 = no size-based rotation
    std::uint32_t max_archive_files{5};              ///< rotated files kept
    core::LogLevel min_log_level_storage{core::LogLevel::Warning}; ///< threshold for disk
    core::CpuSet bg_cpu_affinity{};                  ///< service-core mask (empty = inhertit)
    std::uint32_t batch_flush_interval_ms{500};      ///< flush timer (ms)

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        std::string out = "LogDaemonConfig(service=" + service_name;
        out += ", log_dir=" + log_directory;
        out += ", max_file_mb=" + std::to_string(max_file_size_mb);
        out += ", max_archives=" + std::to_string(max_archive_files);
        out += ", min_level=" + std::string(core::to_string(min_log_level_storage));
        out += ", flush_interval_ms=" + std::to_string(batch_flush_interval_ms);
        out += ", bg_affinity=" + bg_cpu_affinity.to_string() + ")";
        return out;
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const LogDaemonConfig& cfg) {
    return cfg.to_string();
}

/// Cold-path JSON round-trip (mandate 10), hostile-safe under JSON_NOEXCEPTION=1.
inline void to_json(nlohmann::json& j, const LogDaemonConfig& cfg) {
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

inline void from_json(const nlohmann::json& j, LogDaemonConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    LogDaemonConfig parsed{};
    const auto read_str = [&j](const char* key, std::string& dst) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_string()) {
            dst = it->get<std::string>();
        }
    };
    const auto read_u64 = [&j](const char* key, std::uint64_t& dst) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_unsigned()) {
            dst = it->get<std::uint64_t>();
        }
    };
    const auto read_u32 = [&j](const char* key, std::uint32_t& dst) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_unsigned()) {
            dst = it->get<std::uint32_t>();
        }
    };
    read_str("service_name", parsed.service_name);
    read_str("log_directory", parsed.log_directory);
    read_u64("max_file_size_mb", parsed.max_file_size_mb);
    read_u32("max_archive_files", parsed.max_archive_files);
    read_u32("batch_flush_interval_ms", parsed.batch_flush_interval_ms);

    const auto lv = j.find("min_log_level_storage");
    if (lv != j.end() && lv->is_string()) {
        core::LogLevel level = core::LogLevel::Warning;
        if (core::from_string(lv->get<std::string>(), level)) {
            parsed.min_log_level_storage = level;
        }
    }
    const auto af = j.find("bg_cpu_affinity");
    if (af != j.end() && af->is_object()) {
        from_json(*af, parsed.bg_cpu_affinity);
    }
    out = std::move(parsed);
}

} // namespace nexigen::logging

/// The config formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::logging::LogDaemonConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::logging::LogDaemonConfig& cfg, Ctx& ctx) const {
        const std::string text = cfg.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};