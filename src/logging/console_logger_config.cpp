// nexigen::logging — cold-path JSON serialization for ConsoleLoggerConfig
// (spec logging/console-logger).
//
// Strictly non-throwing under `JSON_NOEXCEPTION=1`: a wrong shape aborts the
// whole object and leaves the caller's `out` untouched.
// nlohmann/json headers trigger -Wnull-dereference under -O3 (pre-existing
// upstream issue). Suppress for this TU only — the JSON code is cold-path.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/enum_traits.hpp>
#pragma GCC diagnostic pop

#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/core/thread_management.hpp>
#include <nexigen/logging/console_logger_config.hpp>
#include <nexigen/core/log_level.hpp>

namespace nexigen::logging {

namespace {} // namespace

void to_json(nlohmann::json& j, const ConsoleLoggerConfig& cfg) {
    j = nlohmann::json{
        {"initial_level", std::string(to_string(cfg.initial_level))},
        {"worker_thread", cfg.worker_thread},
        {"worker_poll_interval_us", cfg.worker_poll_interval_us},
    };
}

void from_json(const nlohmann::json& j, ConsoleLoggerConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    ConsoleLoggerConfig parsed = out;
    if (!core::mandate::read_optional_enum(j, "initial_level", parsed.initial_level)) {
        return;
    }
    if (!core::detail::read_optional_object(j, "worker_thread", [&](const nlohmann::json& obj) {
            return core::parse_thread_spec_json(obj, parsed.worker_thread);
        })) {
        return;
    }
    std::uint64_t poll = parsed.worker_poll_interval_us;
    if (!core::detail::read_optional_unsigned(j, "worker_poll_interval_us", poll) ||
        poll > 0xffffffffULL) {
        return;
    }
    parsed.worker_poll_interval_us = static_cast<std::uint32_t>(poll);
    out = parsed;
}

} // namespace nexigen::logging
