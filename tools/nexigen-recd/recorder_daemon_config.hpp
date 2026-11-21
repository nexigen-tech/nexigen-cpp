// nexigen-recd — construction-time configuration (change
// record-telemetry-sessions; spec recorder/telemetry-capture).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexigen::recorder {

/// @brief Configuration for the telemetry session collector.
///
/// Cold-path value aggregate: the collector copies it at startup. Service
/// names identify the suite-wide streams; the flags select which streams are
/// recorded.
///
/// @note Spec `recorder/telemetry-capture`, requirement "Collector
///       configuration serialization".
struct RecorderDaemonConfig {
    std::string session_root{"/var/log/nexigen/sessions"};
    std::string service_log{"Nexigen/Log/nexigen"};
    std::string service_metrics{"Nexigen/Metrics/nexigen"};
    std::string service_traces{"Nexigen/Tracing/nexigen"};
    std::vector<std::string> event_services{};
    bool logs{true};
    bool metrics{true};
    bool traces{true};
    bool events{false};
    std::uint64_t max_segment_bytes{1ULL << 30U};
    std::uint64_t retention_bytes{0};

    [[nodiscard]] std::string to_string() const {
        return std::format(
            "RecorderDaemonConfig(root={}, log={}, metrics={}, traces={}, events={}, "
            "max_segment_bytes={}, retention_bytes={})",
            session_root, service_log, service_metrics, service_traces, events, max_segment_bytes,
            retention_bytes);
    }
};

[[nodiscard]] inline std::string to_string(const RecorderDaemonConfig& config) {
    return config.to_string();
}

inline void to_json(nlohmann::json& j, const RecorderDaemonConfig& config) {
    j = nlohmann::json{{"session_root", config.session_root},
                       {"service_log", config.service_log},
                       {"service_metrics", config.service_metrics},
                       {"service_traces", config.service_traces},
                       {"event_services", config.event_services},
                       {"logs", config.logs},
                       {"metrics", config.metrics},
                       {"traces", config.traces},
                       {"events", config.events},
                       {"max_segment_bytes", config.max_segment_bytes},
                       {"retention_bytes", config.retention_bytes}};
}

inline void from_json(const nlohmann::json& j, RecorderDaemonConfig& config) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto read_string = [&j](const char* key, std::string& out) {
        if (const auto it = j.find(key); it != j.end() && it->is_string()) {
            out = it->get<std::string>();
        }
    };
    const auto read_bool = [&j](const char* key, bool& out) {
        if (const auto it = j.find(key); it != j.end() && it->is_boolean()) {
            out = it->get<bool>();
        }
    };
    const auto read_u64 = [&j](const char* key, std::uint64_t& out) {
        if (const auto it = j.find(key); it != j.end() && it->is_number_unsigned()) {
            out = it->get<std::uint64_t>();
        }
    };
    read_string("session_root", config.session_root);
    read_string("service_log", config.service_log);
    read_string("service_metrics", config.service_metrics);
    read_string("service_traces", config.service_traces);
    if (const auto it = j.find("event_services"); it != j.end() && it->is_array()) {
        config.event_services = it->get<std::vector<std::string>>();
    }
    read_bool("logs", config.logs);
    read_bool("metrics", config.metrics);
    read_bool("traces", config.traces);
    read_bool("events", config.events);
    read_u64("max_segment_bytes", config.max_segment_bytes);
    read_u64("retention_bytes", config.retention_bytes);
}

} // namespace nexigen::recorder

template <>
struct std::formatter<nexigen::recorder::RecorderDaemonConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::recorder::RecorderDaemonConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
