// nexigen::logging — cold-path JSON serialization for Iceoryx2LoggerConfig
// (spec logging/iceoryx2-logger; change refactor-complexity-hotspots).
//
// Strictly non-throwing under `JSON_NOEXCEPTION=1`: the whole object is parsed
// into a local and only then assigned, so a hostile document leaves the
// caller's `out` untouched.
// nlohmann/json headers trigger -Wnull-dereference under -O3 (pre-existing
// upstream issue). Suppress for this TU only — the JSON code is cold-path.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <cstdint>
#include <string>
#include <utility>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/logging/iceoryx2_logger_config.hpp>

namespace nexigen::logging {

namespace {

// Rate-limit section: every field is tolerated on its own.
void parse_throttle(const nlohmann::json& j, ThrottleConfig& throttle) noexcept {
    core::detail::read_bool(j, "enabled", throttle.enabled);
    core::detail::read_unsigned(j, "max_records_per_window", throttle.max_records_per_window);
    core::detail::read_unsigned(j, "window_ns", throttle.window_ns);
}

// Worker attributes: thread-management's reader is already field-tolerant.
void parse_worker_thread(const nlohmann::json& j, core::ThreadSpec& worker) noexcept {
    from_json(j, worker);
}

} // namespace

void to_json(nlohmann::json& j, const Iceoryx2LoggerConfig& cfg) {
    j = nlohmann::json{
        {"service_name", cfg.service_name},
        {"app_id", cfg.app_id},
        {"max_subscribers", cfg.max_subscribers},
        {"history_size", cfg.history_size},
        {"initial_level", cfg.initial_level},
        {"throttle", nlohmann::json{{"enabled", cfg.throttle.enabled},
                                    {"max_records_per_window", cfg.throttle.max_records_per_window},
                                    {"window_ns", cfg.throttle.window_ns}}},
        {"worker_thread", cfg.worker_thread},
        {"worker_poll_interval_us", cfg.worker_poll_interval_us},
    };
}

void from_json(const nlohmann::json& j, Iceoryx2LoggerConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    Iceoryx2LoggerConfig parsed{};
    core::detail::read_string(j, "service_name", parsed.service_name);
    core::detail::read_string(j, "app_id", parsed.app_id);
    core::detail::read_unsigned(j, "max_subscribers", parsed.max_subscribers);
    core::detail::read_unsigned(j, "history_size", parsed.history_size);
    core::detail::read_unsigned(j, "worker_poll_interval_us", parsed.worker_poll_interval_us);

    // Non-throwing enum parse; an unknown name keeps the default level.
    std::string level_name;
    if (core::detail::read_string(j, "initial_level", level_name)) {
        core::LogLevel level = core::LogLevel::Info;
        if (core::from_string(std::string_view(level_name), level)) {
            parsed.initial_level = level;
        }
    }
    core::detail::read_object(j, "throttle", [&parsed](const nlohmann::json& throttle) noexcept {
        parse_throttle(throttle, parsed.throttle);
    });
    core::detail::read_object(j, "worker_thread", [&parsed](const nlohmann::json& worker) noexcept {
        parse_worker_thread(worker, parsed.worker_thread);
    });
    out = std::move(parsed);
}

} // namespace nexigen::logging
