// nexigen::tracing — cold-path JSON serialization for Iceoryx2TraceSinkConfig
// (change add-iceoryx2-trace-sink; spec tracing/iceoryx2-trace-sink "Sink
// configuration and diagnostics").
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
#include <nexigen/tracing/iceoryx2_trace_sink_config.hpp>

namespace nexigen::tracing {

void to_json(nlohmann::json& j, const Iceoryx2TraceSinkConfig& config) {
    j = nlohmann::json{
        {"service_name", config.service_name},
        {"app_id", config.app_id},
        {"subscriber_max_buffer_depth", config.subscriber_max_buffer_depth},
    };
}

void from_json(const nlohmann::json& j, Iceoryx2TraceSinkConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    Iceoryx2TraceSinkConfig parsed{};
    core::detail::read_string(j, "service_name", parsed.service_name);
    core::detail::read_string(j, "app_id", parsed.app_id);
    core::detail::read_unsigned(j, "subscriber_max_buffer_depth",
                                parsed.subscriber_max_buffer_depth);
    out = std::move(parsed);
}

} // namespace nexigen::tracing
