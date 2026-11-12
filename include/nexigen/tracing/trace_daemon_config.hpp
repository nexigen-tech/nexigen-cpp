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

/// @brief Construction-time configuration of the central trace daemon.
///
/// A plain value type validated once at startup: the span and recovery service
/// names, the recovery event name, subscriber depth, drain interval, ring
/// capacity, dump directory and retention, the recovery trigger flag, the
/// readiness application name, and the background thread specification. Invalid
/// values are rejected before any port, thread, or directory is created.
///
/// @note Spec `tracing/trace-daemon`, requirement "Trace daemon
///       configuration".
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

    /// @brief Validate every field before the daemon creates any resource.
    ///
    /// Rejects an empty or oversized service/recovery/event name (`> 511`
    /// bytes), an empty or oversized application name (`> 63` bytes), a drain
    /// interval outside `[5, 1000]` ms, a zero ring capacity, a relative or
    /// empty dump directory, and a zero dump-file limit.
    ///
    /// @return Success when all fields are in range, otherwise
    ///         `core::ErrorCode::InvalidArgument`.
    /// @note Spec `tracing/trace-daemon`, requirement "Trace daemon
    ///       configuration"; validation runs before any port exists.
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

    /// @brief Render the configuration as a cold-path diagnostic string.
    ///
    /// @return Text naming both services, the recovery event, interval, ring
    ///         capacity, dump directory, retention, trigger flag, application
    ///         name, and background thread specification.
    /// @note Spec `tracing/trace-daemon`, requirement "Trace daemon
    ///       configuration diagnostics"; project mandate 9 (`to_string`).
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

/// @brief Free diagnostic helper mirroring `TraceDaemonConfig::to_string()`.
///
/// @param config Configuration to render.
/// @return The same text the member returns.
/// @note Spec `tracing/trace-daemon`, requirement "Trace daemon configuration
///       diagnostics"; project mandate 9 (`to_string`).
[[nodiscard]] inline std::string to_string(const TraceDaemonConfig& config) {
    return config.to_string();
}

/// @brief Serialize the configuration as JSON (cold path).
///
/// @param j Destination JSON value, overwritten with every configuration field.
/// @param config Configuration to serialize.
/// @note Spec `tracing/trace-daemon`, requirement "Trace daemon configuration
///       diagnostics"; project mandate 10 (JSON).
void to_json(nlohmann::json& j, const TraceDaemonConfig& config);

/// @brief Parse the configuration from JSON without throwing.
///
/// Safe under `JSON_NOEXCEPTION=1`: missing or wrongly typed fields keep their
/// prior value and malformed input leaves the parsed values unchanged.
///
/// @param j JSON object to read.
/// @param out Destination configuration, updated field by field.
/// @note Spec `tracing/trace-daemon`, requirement "Trace daemon configuration
///       diagnostics"; project mandate 10 (JSON).
void from_json(const nlohmann::json& j, TraceDaemonConfig& out) noexcept;

} // namespace nexigen::tracing

/// @brief `std::formatter` for `TraceDaemonConfig`.
///
/// @note Project mandate 11 (`std::formatter`); formatting is cold path only.
template <>
struct std::formatter<nexigen::tracing::TraceDaemonConfig> : std::formatter<std::string_view> {
    /// @brief Format the configuration through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param config Configuration to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::TraceDaemonConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
