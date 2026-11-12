// nexigen::logging — construction-time configuration for LogDaemon
// (change add-log-daemon; spec logging/log-daemon; design D8).
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/affinity.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/log_level.hpp>

namespace nexigen::logging {

/// @brief `LogDaemon` construction-time configuration.
///
/// Validated at startup: invalid values are rejected before the subscriber or the
/// worker thread is created, so a typo never leaves a half-initialized daemon.
/// After construction the daemon owns the values and no producer process ever
/// reads them.
///
/// @note Spec `logging/log-daemon`, requirement "LogDaemon configuration".
struct LogDaemonConfig {
    /// @brief Pub/sub service to subscribe to (at most 511 bytes).
    std::string service_name{"Nexigen/Log/nexigen"};
    /// @brief Absolute root of the output tree (at most 511 bytes).
    std::string log_directory{"/var/log/nexigen"};
    /// @brief Size-based rotation threshold in megabytes; `0` disables size rotation.
    std::uint64_t max_file_size_mb{20};
    /// @brief Number of rotated archive files kept after the active one.
    std::uint32_t max_archive_files{5};
    /// @brief Severity threshold for records persisted to disk.
    core::LogLevel min_log_level_storage{core::LogLevel::Warning};
    /// @brief Legacy service-core affinity mask; empty means "inherit".
    ///
    /// Kept for compatibility: an explicit `main_thread` wins over this field.
    core::CpuSet bg_cpu_affinity{};
    /// @brief Buffer flush timer interval in milliseconds.
    std::uint32_t batch_flush_interval_ms{500};
    /// @brief Main-thread specification; empty means derive from `bg_cpu_affinity`.
    core::ThreadSpec main_thread{};

    /// @brief Cold-path diagnostic rendering of every configuration field.
    ///
    /// @return Text containing the service, directory, size/archive limits, storage level,
    ///         flush interval, legacy mask, and main-thread spec.
    /// @note Project mandate 9 (`to_string` on every public value type).
    [[nodiscard]] std::string to_string() const {
        std::string out = "LogDaemonConfig(service=" + service_name;
        out += ", log_dir=" + log_directory;
        out += ", max_file_mb=" + std::to_string(max_file_size_mb);
        out += ", max_archives=" + std::to_string(max_archive_files);
        out += ", min_level=" + std::string(core::to_string(min_log_level_storage));
        out += ", flush_interval_ms=" + std::to_string(batch_flush_interval_ms);
        out += ", bg_affinity=" + bg_cpu_affinity.to_string();
        out += ", main_thread=" + main_thread.to_string() + ")";
        return out;
    }
};

/// @brief Free `to_string` entry point for the daemon config (mandate 9).
///
/// @param cfg Config to render.
/// @return Exactly `cfg.to_string()`.
/// @note Project mandate 9; ADL-friendly mirror of the member function.
[[nodiscard]] inline std::string to_string(const LogDaemonConfig& cfg) {
    return cfg.to_string();
}

/// @brief Resolve the effective main-thread specification for the daemon.
///
/// An explicit `main_thread` wins; otherwise the legacy `bg_cpu_affinity` mask is
/// converted into a placement-only fallback spec, which is what pre-existing
/// configuration files carry.
///
/// @param cfg Daemon configuration to resolve.
/// @return The merged `core::ThreadSpec`; never throws.
/// @note Spec `logging/log-daemon`, requirement
///       "LogDaemon main thread applies a configurable spec".
[[nodiscard]] inline core::ThreadSpec
effective_main_thread_spec(const LogDaemonConfig& cfg) noexcept {
    core::ThreadSpec legacy{};
    if (!cfg.bg_cpu_affinity.empty()) {
        legacy.set_cpus(cfg.bg_cpu_affinity);
    }
    return core::effective_thread_spec(cfg.main_thread, legacy);
}

/// @brief Cold-path JSON writer for the daemon configuration.
///
/// @param j Destination JSON object; overwritten with the config's field map.
/// @param cfg Config to serialize.
/// @note Project mandate 10; the body lives in `log_daemon_config.cpp`.
/// @note Spec `logging/log-daemon`, requirement "LogDaemon configuration".
void to_json(nlohmann::json& j, const LogDaemonConfig& cfg);
/// @brief Cold-path JSON reader that is hostile-input safe.
///
/// Absent or ill-typed fields leave the corresponding member untouched, so the
/// call never throws under `JSON_NOEXCEPTION=1`.
///
/// @param j Source JSON object.
/// @param out Destination config, updated field by field.
/// @note Project mandate 10; the body lives in `log_daemon_config.cpp`.
void from_json(const nlohmann::json& j, LogDaemonConfig& out) noexcept;

} // namespace nexigen::logging

/// @brief `std::formatter` specialization: the config formats as its diagnostic string.
///
/// Forwards to `std::formatter<std::string_view>` after materializing
/// `to_string()`, so every string format spec applies.
///
/// @note Project mandate 11 (`std::formatter` for every public value type).
template <>
struct std::formatter<nexigen::logging::LogDaemonConfig> : std::formatter<std::string_view> {
    /// @brief Write the config's diagnostic text into the format output.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param cfg Config to render.
    /// @param ctx Format context receiving the text.
    /// @return Iterator past the last written character, as returned by the string formatter.
    /// @note Allocates the temporary diagnostic string; cold path only.
    template <class Ctx> auto format(const nexigen::logging::LogDaemonConfig& cfg, Ctx& ctx) const {
        const std::string text = cfg.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
