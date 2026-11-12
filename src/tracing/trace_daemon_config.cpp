// nexigen::tracing — cold-path JSON serialization for TraceDaemonConfig
// (change add-trace-daemon; spec tracing/trace-daemon "Trace daemon
// configuration diagnostics").
//
// Strictly non-throwing under `JSON_NOEXCEPTION=1`: the object is parsed into a
// local and assigned only on success. nlohmann/json headers trigger
// -Wnull-dereference under -O3 (pre-existing upstream issue); suppress for
// this cold-path TU only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <cstdint>
#include <string>
#include <utility>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/tracing/trace_daemon_config.hpp>

namespace nexigen::tracing {

void to_json(nlohmann::json& j, const TraceDaemonConfig& config) {
    j = nlohmann::json{
        {"service_name", config.service_name},
        {"recovery_service_name", config.recovery_service_name},
        {"recovery_event_name", config.recovery_event_name},
        {"subscriber_max_buffer_depth", config.subscriber_max_buffer_depth},
        {"drain_interval_ms", config.drain_interval_ms},
        {"ring_capacity_records", config.ring_capacity_records},
        {"dump_directory", config.dump_directory},
        {"max_dump_files", config.max_dump_files},
        {"trigger_on_recovery", config.trigger_on_recovery},
        {"app_name", config.app_name},
        {"bg_cpu_affinity", config.bg_cpu_affinity},
    };
}

void from_json(const nlohmann::json& j, TraceDaemonConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    TraceDaemonConfig parsed{};
    core::detail::read_string(j, "service_name", parsed.service_name);
    core::detail::read_string(j, "recovery_service_name", parsed.recovery_service_name);
    core::detail::read_string(j, "recovery_event_name", parsed.recovery_event_name);
    core::detail::read_unsigned(j, "subscriber_max_buffer_depth",
                                parsed.subscriber_max_buffer_depth);
    core::detail::read_unsigned(j, "drain_interval_ms", parsed.drain_interval_ms);
    core::detail::read_unsigned(j, "ring_capacity_records", parsed.ring_capacity_records);
    core::detail::read_string(j, "dump_directory", parsed.dump_directory);
    core::detail::read_unsigned(j, "max_dump_files", parsed.max_dump_files);
    core::detail::read_bool(j, "trigger_on_recovery", parsed.trigger_on_recovery);
    core::detail::read_string(j, "app_name", parsed.app_name);
    core::detail::read_object(j, "bg_cpu_affinity",
                              [&parsed](const nlohmann::json& affinity) noexcept {
                                  from_json(affinity, parsed.bg_cpu_affinity);
                              });
    out = std::move(parsed);
}

} // namespace nexigen::tracing
