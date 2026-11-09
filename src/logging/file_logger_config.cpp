// nexigen::logging — cold-path JSON serialization for FileLoggerConfig and
// the settings→config mapping (spec logging/file-logger, logging/logging-settings).
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
#include <utility>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/logging/file_logger_config.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/rotation_schedule.hpp>
#include <nexigen/logging/settings.hpp>

namespace nexigen::logging {

namespace {

constexpr std::uint64_t kNanoseconds_per_second = 1'000'000'000ULL;

[[nodiscard]] bool try_read_string_field(const nlohmann::json& j, const char* key,
                                         std::string& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return true;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return false;
    }
    out = *text;
    return true;
}

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

[[nodiscard]] bool try_read_bool_field(const nlohmann::json& j, const char* key,
                                       bool& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return true;
    }
    if (!it->is_boolean()) {
        return false;
    }
    out = it->get<bool>();
    return true;
}

[[nodiscard]] bool try_read_log_level_field(const nlohmann::json& j, const char* key,
                                            core::LogLevel& out) noexcept {
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

[[nodiscard]] bool try_read_rotation_kind_field(const nlohmann::json& j, const char* key,
                                                RotationSchedule& out) noexcept {
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

[[nodiscard]] bool try_read_throttle_field(const nlohmann::json& j,
                                           FileLoggerConfig& out) noexcept {
    const auto it = j.find("throttle");
    if (it == j.end()) {
        return true;
    }
    if (!it->is_object()) {
        return false;
    }
    return try_read_bool_field(*it, "enabled", out.throttle.enabled) &&
           try_read_u64_field(*it, "max_records_per_window", out.throttle.max_records_per_window) &&
           try_read_u64_field(*it, "window_ns", out.throttle.window_ns);
}

[[nodiscard]] bool try_read_rotation_field(const nlohmann::json& j,
                                           FileLoggerConfig& out) noexcept {
    const auto it = j.find("rotation");
    if (it == j.end()) {
        return true;
    }
    if (!it->is_object()) {
        return false;
    }
    return try_read_rotation_kind_field(*it, "kind", out.rotation.kind) &&
           try_read_u64_field(*it, "custom_ns", out.rotation.custom_ns);
}

} // namespace

void to_json(nlohmann::json& j, const FileLoggerConfig& cfg) {
    j = nlohmann::json{
        {"app_name", cfg.app_name},
        {"base_dir", cfg.base_dir},
        {"file_name", cfg.file_name},
        {"initial_level", std::string(to_string(cfg.initial_level))},
        {"throttle",
         {
             {"enabled", cfg.throttle.enabled},
             {"max_records_per_window", cfg.throttle.max_records_per_window},
             {"window_ns", cfg.throttle.window_ns},
         }},
        {"rotation",
         {
             {"kind", std::string(to_string(cfg.rotation.kind))},
             {"custom_ns", cfg.rotation.custom_ns},
         }},
        {"worker_thread", cfg.worker_thread},
        {"max_file_size_bytes", cfg.max_file_size_bytes},
        {"worker_poll_interval_us", cfg.worker_poll_interval_us},
    };
}

void from_json(const nlohmann::json& j, FileLoggerConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    FileLoggerConfig parsed = out;
    if (!try_read_string_field(j, "app_name", parsed.app_name) ||
        !try_read_string_field(j, "base_dir", parsed.base_dir) ||
        !try_read_string_field(j, "file_name", parsed.file_name)) {
        return;
    }
    if (!try_read_log_level_field(j, "initial_level", parsed.initial_level)) {
        return;
    }
    if (!try_read_throttle_field(j, parsed)) {
        return;
    }
    if (!try_read_rotation_field(j, parsed)) {
        return;
    }
    if (!try_read_worker_thread_field(j, "worker_thread", parsed.worker_thread)) {
        return;
    }
    if (!try_read_u64_field(j, "max_file_size_bytes", parsed.max_file_size_bytes)) {
        return;
    }
    std::uint64_t poll = parsed.worker_poll_interval_us;
    if (!try_read_u64_field(j, "worker_poll_interval_us", poll) || poll > 0xffffffffULL) {
        return;
    }
    parsed.worker_poll_interval_us = static_cast<std::uint32_t>(poll);
    out = std::move(parsed);
}

core::Status apply_to(const Settings& settings, FileLoggerConfig& out) noexcept {
    const LoggerOptions& options = settings.options;
    if (options.log_root_directory.empty() || options.log_root_directory.size() > 511 ||
        options.log_root_directory.find('\0') != std::string::npos) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    out = FileLoggerConfig{};
    out.base_dir = options.log_root_directory;
    out.initial_level = options.initial_log_level;
    out.max_file_size_bytes = options.max_file_size_bytes;
    out.worker_thread = options.worker_thread;
    out.worker_poll_interval_us = options.worker_poll_interval_us;

    const std::uint64_t per_second = options.spam_protection.max_messages_per_sec_per_shard;
    if (per_second != 0ULL) {
        out.throttle.enabled = true;
        out.throttle.max_records_per_window = per_second;
        out.throttle.window_ns = kNanoseconds_per_second;
    }
    return {};
}

} // namespace nexigen::logging
