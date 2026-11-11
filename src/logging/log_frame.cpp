// nexigen::logging — cold-path JSON serialization for the zero-copy LogFrame
// (spec logging/iceoryx2-logger; change refactor-complexity-hotspots).
//
// The hot-path header keeps only the inline accessors and the layout asserts;
// the diagnostic JSON pair is compiled here once. Deserialization is
// all-or-nothing: the frame is built in a local and assigned on success, and
// the fixed-size buffers are filled through the shared bound-copy helpers.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/log_frame.hpp>

namespace nexigen::logging {

void to_json(nlohmann::json& j, const LogFrame& frame) {
    j = nlohmann::json{
        {"timestamp_ns", frame.timestamp_ns},
        {"app_id", std::string(frame.app_id_text())},
        {"context_id", frame.context_id},
        {"thread_id", frame.thread_id},
        {"pid", frame.pid},
        {"dropped_counter", frame.dropped_counter},
        {"payload_size", frame.payload_size},
        {"log_level", frame.log_level},
        {"payload", std::string(frame.payload_text())},
    };
}

void from_json(const nlohmann::json& j, LogFrame& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    LogFrame parsed{};
    core::detail::read_unsigned(j, "timestamp_ns", parsed.timestamp_ns);
    core::detail::read_unsigned(j, "context_id", parsed.context_id);
    core::detail::read_unsigned(j, "thread_id", parsed.thread_id);
    core::detail::read_unsigned(j, "pid", parsed.pid);
    core::detail::read_unsigned(j, "dropped_counter", parsed.dropped_counter);
    core::detail::read_unsigned(j, "payload_size", parsed.payload_size);
    core::detail::read_fixed_string(j, "app_id", parsed.app_id, sizeof(parsed.app_id));
    core::detail::read_fixed_bytes(j, "payload", parsed.payload, sizeof(parsed.payload));

    // Non-throwing enum parse; an unknown name keeps the default level.
    std::string level_name;
    if (core::detail::read_string(j, "log_level", level_name)) {
        core::LogLevel level = core::LogLevel::Info;
        if (core::from_string(std::string_view(level_name), level)) {
            parsed.log_level = level;
        }
    }
    out = parsed;
}

} // namespace nexigen::logging
