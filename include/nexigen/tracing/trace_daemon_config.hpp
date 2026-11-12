// nexigen::tracing — TraceDaemon construction-time configuration (change
// add-trace-daemon; spec tracing/trace-daemon "Trace daemon configuration";
// design D7/D8).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>

namespace nexigen::tracing {

/// TraceDaemon construction-time configuration. Validated at startup; invalid
/// values are rejected before any port, thread, or directory is created.
struct TraceDaemonConfig {
    std::string service_name{"nexigen/trace/stream"};            ///< span subscription (≤ 511 B)
    std::string recovery_service_name{"Nexigen/SM/Recovery"};    ///< PHM→SM notification
    std::string recovery_event_name{"Nexigen/SM/RecoveryEvent"}; ///< notification wake
    std::uint64_t subscriber_max_buffer_depth{4096};             ///< span queue depth (0 = default)
    std::uint32_t drain_interval_ms{20};                         ///< WaitSet interval, 5..1000
    std::uint64_t ring_capacity_records{65'536};                 ///< flight-recorder slots, > 0
    std::string dump_directory{"/var/log/nexigen/traces"};       ///< absolute snapshot root
    std::uint32_t max_dump_files{8};                             ///< retained snapshots, > 0
    bool trigger_on_recovery{true};                              ///< dump on PHM notification
    std::string app_name{"TRCD"};                                ///< readiness channel (≤ 63 B)
    core::ThreadSpec bg_cpu_affinity{};                          ///< empty = inherited placement

    /// Startup validation: names, interval, ring, directory and retention.
    [[nodiscard]] core::Status validate() const noexcept {
        if (service_name.empty() || service_name.size() > 511U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (recovery_service_name.empty() || recovery_service_name.size() > 511U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (recovery_event_name.empty() || recovery_event_name.size() > 511U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (app_name.empty() || app_name.size() > 63U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (drain_interval_ms < 5U || drain_interval_ms > 1000U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (ring_capacity_records == 0U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (dump_directory.empty() || dump_directory.front() != '/') {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (max_dump_files == 0U) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        return {};
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        std::string out = "TraceDaemonConfig(service=" + service_name;
        out += ", recovery=" + recovery_service_name;
        out += ", recovery_event=" + recovery_event_name;
        out += ", interval_ms=" + std::to_string(drain_interval_ms);
        out += ", ring_records=" + std::to_string(ring_capacity_records);
        out += ", dump_dir=" + dump_directory;
        out += ", max_dumps=" + std::to_string(max_dump_files);
        out += ", trigger=" + std::string(trigger_on_recovery ? "true" : "false");
        out += ", app=" + app_name;
        out += ", bg=" + bg_cpu_affinity.to_string() + ")";
        return out;
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const TraceDaemonConfig& config) {
    return config.to_string();
}

/// Cold-path JSON round-trip (mandate 10), hostile-safe under JSON_NOEXCEPTION=1;
/// body lives in src/tracing/trace_daemon_config.cpp.
void to_json(nlohmann::json& j, const TraceDaemonConfig& config);
void from_json(const nlohmann::json& j, TraceDaemonConfig& out) noexcept;

} // namespace nexigen::tracing

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::tracing::TraceDaemonConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::TraceDaemonConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
