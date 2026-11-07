// nexigen::logging — cold-path JSON serialization for LoggerOptions /
// SpamProtectionConfig (change rework-logging-hot-path-engine, design D7;
// spec logging/logger-options).
//
// All bodies live here (compiled unit) so consumer TUs stay lean and
// `std::string` / `nlohmann::json` never appear on a hot path. Strictly
// non-throwing under `JSON_NOEXCEPTION=1`: every access is guarded by
// `is_*` checks, and a wrong shape leaves the target untouched.
// nlohmann/json headers trigger -Wnull-dereference under -O3 (pre-existing
// upstream issue). Suppress for this TU only — the JSON code is cold-path.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nexigen/logging/logger_options.hpp>
#pragma GCC diagnostic pop
#include <nexigen/logging/console_logger.hpp>
#include <nexigen/logging/file_logger.hpp>

namespace nexigen::logging {

namespace {

// ---- best-effort field readers: missing or wrong-shape leaves `out` ----
// unchanged. Used by LoggerOptions and SpamProtectionConfig where each
// field is independent.

void read_string_field(const nlohmann::json& j, const char* key, std::string& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return;
    }
    out = *text;
}

void read_u64_field(const nlohmann::json& j, const char* key, std::uint64_t& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    if (!it->is_number_unsigned()) {
        return;
    }
    out = it->get<std::uint64_t>();
}

void read_u32_field(const nlohmann::json& j, const char* key, std::uint32_t& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    if (!it->is_number_unsigned()) {
        return;
    }
    out = it->get<std::uint32_t>();
}

void read_bool_field(const nlohmann::json& j, const char* key, bool& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    if (!it->is_boolean()) {
        return;
    }
    out = it->get<bool>();
}

void read_log_level_field(const nlohmann::json& j, const char* key, LogLevel& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return;
    }
    static_cast<void>(from_string(std::string_view(*text), out));
}

void read_overflow_policy_field(const nlohmann::json& j, const char* key,
                                OverflowPolicy& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return;
    }
    static_cast<void>(from_string(std::string_view(*text), out));
}

void read_core_mask_field(const nlohmann::json& j, const char* key,
                          core::ThreadSpec& worker_thread) noexcept {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_unsigned()) {
        return;
    }
    worker_thread.set_cpus(core::CpuSet::from_core_mask(it->get<std::uint64_t>()));
}

void read_flush_cpu_affinity_field(const nlohmann::json& j, const char* key,
                                   core::ThreadSpec& worker_thread) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    core::CpuSet cpus = worker_thread.cpus();
    if (core::parse_cpu_set_json(*it, cpus)) {
        worker_thread.set_cpus(cpus);
    }
}

void read_worker_thread_field(const nlohmann::json& j, const char* key,
                              core::ThreadSpec& worker_thread) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    static_cast<void>(core::parse_thread_spec_json(*it, worker_thread));
}

// ---- strict field readers: return false when the key is present but ----
// malformed. Used by FileLoggerConfig / ConsoleLoggerConfig where a wrong
// shape aborts the whole object and leaves the caller's `out` untouched.

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
    return detail::from_string(std::string_view(*text), out);
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

void to_json(nlohmann::json& j, const SpamProtectionConfig& cfg) {
    j = nlohmann::json{
        {"enable_repeat_suppression", cfg.enable_repeat_suppression},
        {"repeat_suppression_window_ms", cfg.repeat_suppression_window_ms},
        {"max_messages_per_sec_per_shard", cfg.max_messages_per_sec_per_shard},
        {"bypass_min_level", std::string(to_string(cfg.bypass_min_level))},
    };
}

void from_json(const nlohmann::json& j, SpamProtectionConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_bool_field(j, "enable_repeat_suppression", out.enable_repeat_suppression);
    read_u32_field(j, "repeat_suppression_window_ms", out.repeat_suppression_window_ms);
    read_u32_field(j, "max_messages_per_sec_per_shard", out.max_messages_per_sec_per_shard);
    read_log_level_field(j, "bypass_min_level", out.bypass_min_level);
}

void to_json(nlohmann::json& j, const LoggerOptions& opts) {
    j = nlohmann::json{
        {"log_root_directory", opts.log_root_directory},
        {"initial_log_level", std::string(to_string(opts.initial_log_level))},
        {"max_file_size_bytes", opts.max_file_size_bytes},
        {"spsc_ring_buffer_capacity", opts.spsc_ring_buffer_capacity},
        {"message_capacity", opts.message_capacity},
        {"max_producer_threads", opts.max_producer_threads},
        {"overflow_policy", std::string(to_string(opts.overflow_policy))},
        {"worker_thread", opts.worker_thread},
        {"worker_poll_interval_us", opts.worker_poll_interval_us},
        {"sync_flush_on_critical", opts.sync_flush_on_critical},
        {"spam_protection", opts.spam_protection},
    };
}

void from_json(const nlohmann::json& j, LoggerOptions& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_string_field(j, "log_root_directory", out.log_root_directory);
    read_log_level_field(j, "initial_log_level", out.initial_log_level);
    read_u64_field(j, "max_file_size_bytes", out.max_file_size_bytes);
    read_u32_field(j, "spsc_ring_buffer_capacity", out.spsc_ring_buffer_capacity);
    read_u32_field(j, "message_capacity", out.message_capacity);
    read_u32_field(j, "max_producer_threads", out.max_producer_threads);
    read_overflow_policy_field(j, "overflow_policy", out.overflow_policy);
    // Compatibility inputs are applied before the canonical worker object so
    // canonical fields win within this source. Legacy masks/lists affect only
    // CPUs; name, policy, priority, and mode are inherited normally.
    read_core_mask_field(j, "core_mask", out.worker_thread);
    read_flush_cpu_affinity_field(j, "flush_cpu_affinity", out.worker_thread);
    read_worker_thread_field(j, "worker_thread", out.worker_thread);
    read_u32_field(j, "worker_poll_interval_us", out.worker_poll_interval_us);
    read_bool_field(j, "sync_flush_on_critical", out.sync_flush_on_critical);
    const auto it = j.find("spam_protection");
    if (it != j.end() && it->is_object()) {
        from_json(*it, out.spam_protection);
    }
}

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
             {"kind", std::string(detail::to_string(cfg.rotation.kind))},
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

std::string SpamProtectionConfig::to_string() const {
    return "SpamProtectionConfig(enable_repeat_suppression=" +
           std::string(enable_repeat_suppression ? "true" : "false") +
           ", repeat_suppression_window_ms=" + std::to_string(repeat_suppression_window_ms) +
           ", max_messages_per_sec_per_shard=" + std::to_string(max_messages_per_sec_per_shard) +
           ", bypass_min_level=" + std::string(nexigen::logging::to_string(bypass_min_level)) + ")";
}

std::string LoggerOptions::to_string() const {
    return "LoggerOptions(log_root_directory=" + log_root_directory +
           ", initial_log_level=" + std::string(nexigen::logging::to_string(initial_log_level)) +
           ", max_file_size_bytes=" + std::to_string(max_file_size_bytes) +
           ", spsc_ring_buffer_capacity=" + std::to_string(spsc_ring_buffer_capacity) +
           ", message_capacity=" + std::to_string(message_capacity) +
           ", max_producer_threads=" + std::to_string(max_producer_threads) +
           ", overflow_policy=" + std::string(nexigen::logging::to_string(overflow_policy)) +
           ", worker_thread=" + worker_thread.to_string() +
           ", worker_poll_interval_us=" + std::to_string(worker_poll_interval_us) +
           ", sync_flush_on_critical=" + std::string(sync_flush_on_critical ? "true" : "false") +
           ", spam_protection=" + spam_protection.to_string() + ")";
}

} // namespace nexigen::logging
