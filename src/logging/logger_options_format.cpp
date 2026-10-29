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

namespace nexigen::logging {

void to_json(nlohmann::json& j, const SpamProtectionConfig& cfg) {
    j = nlohmann::json{
        {"enable_repeat_suppression", cfg.enable_repeat_suppression},
        {"repeat_suppression_window_ms", cfg.repeat_suppression_window_ms},
        {"repeat_suppression_max_count", cfg.repeat_suppression_max_count},
        {"max_messages_per_sec_per_shard", cfg.max_messages_per_sec_per_shard},
        {"max_bytes_per_sec_per_shard", cfg.max_bytes_per_sec_per_shard},
        {"max_total_bytes_per_sec", cfg.max_total_bytes_per_sec},
        {"bypass_min_level", std::string(to_string(cfg.bypass_min_level))},
    };
}

void from_json(const nlohmann::json& j, SpamProtectionConfig& out) noexcept {
    if (!j.is_object()) { return; }
    auto it = j.find("enable_repeat_suppression");
    if (it != j.end() && it->is_boolean()) { out.enable_repeat_suppression = it->get<bool>(); }
    it = j.find("repeat_suppression_window_ms");
    if (it != j.end() && it->is_number_unsigned()) { out.repeat_suppression_window_ms = it->get<std::uint32_t>(); }
    it = j.find("repeat_suppression_max_count");
    if (it != j.end() && it->is_number_unsigned()) { out.repeat_suppression_max_count = it->get<std::uint32_t>(); }
    it = j.find("max_messages_per_sec_per_shard");
    if (it != j.end() && it->is_number_unsigned()) { out.max_messages_per_sec_per_shard = it->get<std::uint32_t>(); }
    it = j.find("max_bytes_per_sec_per_shard");
    if (it != j.end() && it->is_number_unsigned()) { out.max_bytes_per_sec_per_shard = it->get<std::uint64_t>(); }
    it = j.find("max_total_bytes_per_sec");
    if (it != j.end() && it->is_number_unsigned()) { out.max_total_bytes_per_sec = it->get<std::uint64_t>(); }
    it = j.find("bypass_min_level");
    if (it != j.end() && it->is_string()) {
        static_cast<void>(from_string(std::string_view(*it->get_ptr<const nlohmann::json::string_t*>()),
                                      out.bypass_min_level));
    }
}

void to_json(nlohmann::json& j, const LoggerOptions& opts) {
    j = nlohmann::json{
        {"log_root_directory", opts.log_root_directory},
        {"app_name", opts.app_name},
        {"initial_log_level", std::string(to_string(opts.initial_log_level))},
        {"max_file_size_bytes", opts.max_file_size_bytes},
        {"spsc_ring_buffer_capacity", opts.spsc_ring_buffer_capacity},
        {"max_producer_threads", opts.max_producer_threads},
        {"overflow_policy", std::string(to_string(opts.overflow_policy))},
        {"flush_cpu_affinity", opts.flush_cpu_affinity},
        {"worker_poll_interval_us", opts.worker_poll_interval_us},
        {"sync_flush_on_critical", opts.sync_flush_on_critical},
        {"spam_protection", opts.spam_protection},
    };
}

void from_json(const nlohmann::json& j, LoggerOptions& out) noexcept {
    if (!j.is_object()) { return; }
    auto it = j.find("log_root_directory");
    if (it != j.end() && it->is_string()) { out.log_root_directory = it->get<std::string>(); }
    it = j.find("app_name");
    if (it != j.end() && it->is_string()) { out.app_name = it->get<std::string>(); }
    it = j.find("initial_log_level");
    if (it != j.end() && it->is_string()) {
        static_cast<void>(from_string(std::string_view(*it->get_ptr<const nlohmann::json::string_t*>()),
                                      out.initial_log_level));
    }
    it = j.find("max_file_size_bytes");
    if (it != j.end() && it->is_number_unsigned()) { out.max_file_size_bytes = it->get<std::uint64_t>(); }
    it = j.find("spsc_ring_buffer_capacity");
    if (it != j.end() && it->is_number_unsigned()) { out.spsc_ring_buffer_capacity = it->get<std::uint32_t>(); }
    it = j.find("max_producer_threads");
    if (it != j.end() && it->is_number_unsigned()) { out.max_producer_threads = it->get<std::uint32_t>(); }
    it = j.find("overflow_policy");
    if (it != j.end() && it->is_string()) {
        static_cast<void>(from_string(std::string_view(*it->get_ptr<const nlohmann::json::string_t*>()),
                                      out.overflow_policy));
    }
    it = j.find("flush_cpu_affinity");
    if (it != j.end() && it->is_array()) {
        out.flush_cpu_affinity.clear();
        for (const auto& item : *it) {
            if (item.is_number_unsigned()) {
                out.flush_cpu_affinity.push_back(item.get<std::uint64_t>());
            }
        }
    }
    it = j.find("worker_poll_interval_us");
    if (it != j.end() && it->is_number_unsigned()) { out.worker_poll_interval_us = it->get<std::uint32_t>(); }
    it = j.find("sync_flush_on_critical");
    if (it != j.end() && it->is_boolean()) { out.sync_flush_on_critical = it->get<bool>(); }
    it = j.find("spam_protection");
    if (it != j.end() && it->is_object()) {
        from_json(*it, out.spam_protection);
    }
}

std::string SpamProtectionConfig::to_string() const {
    return "SpamProtectionConfig(enable_repeat_suppression=" +
           std::string(enable_repeat_suppression ? "true" : "false") +
           ", repeat_suppression_window_ms=" + std::to_string(repeat_suppression_window_ms) +
           ", repeat_suppression_max_count=" + std::to_string(repeat_suppression_max_count) +
           ", max_messages_per_sec_per_shard=" + std::to_string(max_messages_per_sec_per_shard) +
           ", max_bytes_per_sec_per_shard=" + std::to_string(max_bytes_per_sec_per_shard) +
           ", max_total_bytes_per_sec=" + std::to_string(max_total_bytes_per_sec) +
           ", bypass_min_level=" + std::string(nexigen::logging::to_string(bypass_min_level)) + ")";
}

std::string LoggerOptions::to_string() const {
    return "LoggerOptions(log_root_directory=" + log_root_directory +
           ", app_name=" + app_name +
           ", initial_log_level=" + std::string(nexigen::logging::to_string(initial_log_level)) +
           ", max_file_size_bytes=" + std::to_string(max_file_size_bytes) +
           ", spsc_ring_buffer_capacity=" + std::to_string(spsc_ring_buffer_capacity) +
           ", max_producer_threads=" + std::to_string(max_producer_threads) +
           ", overflow_policy=" + std::string(nexigen::logging::to_string(overflow_policy)) +
           ", worker_poll_interval_us=" + std::to_string(worker_poll_interval_us) +
           ", sync_flush_on_critical=" + std::string(sync_flush_on_critical ? "true" : "false") +
           ", spam_protection=" + spam_protection.to_string() + ")";
}

} // namespace nexigen::logging
