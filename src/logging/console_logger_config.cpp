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
#pragma GCC diagnostic pop

#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/core/thread_management.hpp>
#include <nexigen/logging/console_logger_config.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging {

namespace {

[[nodiscard]] bool try_read_u64_field(const nlohmann::json& j, const char* key,
                                      std::uint64_t& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return true;
    }
    if (!it->is_number_unsigned()) {
        return false;
    }
    out = it->get<std::uint64_t>();
    return true;
}

[[nodiscard]] bool try_read_log_level_field(const nlohmann::json& j, const char* key,
                                            LogLevel& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return true;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return false;
    }
    return from_string(std::string_view(*text), out);
}

[[nodiscard]] bool try_read_worker_thread_field(const nlohmann::json& j, const char* key,
                                                core::ThreadSpec& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return true;
    }
    return core::parse_thread_spec_json(*it, out);
}

} // namespace

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
    if (!try_read_log_level_field(j, "initial_level", parsed.initial_level)) {
        return;
    }
    if (!try_read_worker_thread_field(j, "worker_thread", parsed.worker_thread)) {
        return;
    }
    std::uint64_t poll = parsed.worker_poll_interval_us;
    if (!try_read_u64_field(j, "worker_poll_interval_us", poll) || poll > 0xffffffffULL) {
        return;
    }
    parsed.worker_poll_interval_us = static_cast<std::uint32_t>(poll);
    out = parsed;
}

} // namespace nexigen::logging
