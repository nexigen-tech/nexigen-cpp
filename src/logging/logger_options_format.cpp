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
    auto it = j.find("enable_repeat_suppression");
    if (it != j.end() && it->is_boolean()) {
        out.enable_repeat_suppression = it->get<bool>();
    }
    it = j.find("repeat_suppression_window_ms");
    if (it != j.end() && it->is_number_unsigned()) {
        out.repeat_suppression_window_ms = it->get<std::uint32_t>();
    }
    it = j.find("max_messages_per_sec_per_shard");
    if (it != j.end() && it->is_number_unsigned()) {
        out.max_messages_per_sec_per_shard = it->get<std::uint32_t>();
    }
    it = j.find("bypass_min_level");
    if (it != j.end() && it->is_string()) {
        static_cast<void>(
            from_string(std::string_view(*it->get_ptr<const nlohmann::json::string_t*>()),
                        out.bypass_min_level));
    }
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
    auto it = j.find("log_root_directory");
    if (it != j.end() && it->is_string()) {
        out.log_root_directory = it->get<std::string>();
    }
    it = j.find("initial_log_level");
    if (it != j.end() && it->is_string()) {
        static_cast<void>(
            from_string(std::string_view(*it->get_ptr<const nlohmann::json::string_t*>()),
                        out.initial_log_level));
    }
    it = j.find("max_file_size_bytes");
    if (it != j.end() && it->is_number_unsigned()) {
        out.max_file_size_bytes = it->get<std::uint64_t>();
    }
    it = j.find("spsc_ring_buffer_capacity");
    if (it != j.end() && it->is_number_unsigned()) {
        out.spsc_ring_buffer_capacity = it->get<std::uint32_t>();
    }
    it = j.find("message_capacity");
    if (it != j.end() && it->is_number_unsigned()) {
        out.message_capacity = it->get<std::uint32_t>();
    }
    it = j.find("max_producer_threads");
    if (it != j.end() && it->is_number_unsigned()) {
        out.max_producer_threads = it->get<std::uint32_t>();
    }
    it = j.find("overflow_policy");
    if (it != j.end() && it->is_string()) {
        static_cast<void>(
            from_string(std::string_view(*it->get_ptr<const nlohmann::json::string_t*>()),
                        out.overflow_policy));
    }
    // Compatibility inputs are applied before the canonical worker object so
    // canonical fields win within this source. Legacy masks/lists affect only
    // CPUs; name, policy, priority, and mode are inherited normally.
    it = j.find("core_mask");
    if (it != j.end() && it->is_number_unsigned()) {
        out.worker_thread.set_cpus(core::CpuSet::from_core_mask(it->get<std::uint64_t>()));
    }
    it = j.find("flush_cpu_affinity");
    if (it != j.end()) {
        core::CpuSet cpus = out.worker_thread.cpus();
        if (core::parse_cpu_set_json(*it, cpus)) {
            out.worker_thread.set_cpus(cpus);
        }
    }
    it = j.find("worker_thread");
    if (it != j.end()) {
        static_cast<void>(core::parse_thread_spec_json(*it, out.worker_thread));
    }
    it = j.find("worker_poll_interval_us");
    if (it != j.end() && it->is_number_unsigned()) {
        out.worker_poll_interval_us = it->get<std::uint32_t>();
    }
    it = j.find("sync_flush_on_critical");
    if (it != j.end() && it->is_boolean()) {
        out.sync_flush_on_critical = it->get<bool>();
    }
    it = j.find("spam_protection");
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
    auto read_string = [&](const char* key, std::string& destination) {
        const auto it = j.find(key);
        if (it == j.end()) {
            return true;
        }
        const auto* const value = it->get_ptr<const nlohmann::json::string_t*>();
        if (value == nullptr) {
            return false;
        }
        destination = *value;
        return true;
    };
    auto read_u64 = [&](const char* key, std::uint64_t& destination) {
        const auto it = j.find(key);
        if (it == j.end()) {
            return true;
        }
        if (!it->is_number_unsigned()) {
            return false;
        }
        destination = it->get<std::uint64_t>();
        return true;
    };
    if (!read_string("app_name", parsed.app_name) || !read_string("base_dir", parsed.base_dir) ||
        !read_string("file_name", parsed.file_name)) {
        return;
    }
    const auto level = j.find("initial_level");
    if (level != j.end()) {
        const auto* const text = level->get_ptr<const nlohmann::json::string_t*>();
        if (text == nullptr || !from_string(std::string_view(*text), parsed.initial_level)) {
            return;
        }
    }
    const auto throttle = j.find("throttle");
    if (throttle != j.end()) {
        if (!throttle->is_object()) {
            return;
        }
        const auto enabled = throttle->find("enabled");
        if (enabled != throttle->end()) {
            if (!enabled->is_boolean()) {
                return;
            }
            parsed.throttle.enabled = enabled->get<bool>();
        }
        const auto maximum = throttle->find("max_records_per_window");
        if (maximum != throttle->end()) {
            if (!maximum->is_number_unsigned()) {
                return;
            }
            parsed.throttle.max_records_per_window = maximum->get<std::uint64_t>();
        }
        const auto window = throttle->find("window_ns");
        if (window != throttle->end()) {
            if (!window->is_number_unsigned()) {
                return;
            }
            parsed.throttle.window_ns = window->get<std::uint64_t>();
        }
    }
    const auto rotation = j.find("rotation");
    if (rotation != j.end()) {
        if (!rotation->is_object()) {
            return;
        }
        const auto kind = rotation->find("kind");
        if (kind != rotation->end()) {
            const auto* const text = kind->get_ptr<const nlohmann::json::string_t*>();
            if (text == nullptr ||
                !detail::from_string(std::string_view(*text), parsed.rotation.kind)) {
                return;
            }
        }
        const auto custom = rotation->find("custom_ns");
        if (custom != rotation->end()) {
            if (!custom->is_number_unsigned()) {
                return;
            }
            parsed.rotation.custom_ns = custom->get<std::uint64_t>();
        }
    }
    const auto worker = j.find("worker_thread");
    if (worker != j.end() && !core::parse_thread_spec_json(*worker, parsed.worker_thread)) {
        return;
    }
    if (!read_u64("max_file_size_bytes", parsed.max_file_size_bytes)) {
        return;
    }
    std::uint64_t poll = parsed.worker_poll_interval_us;
    if (!read_u64("worker_poll_interval_us", poll) || poll > 0xffffffffULL) {
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
    const auto level = j.find("initial_level");
    if (level != j.end()) {
        const auto* const text = level->get_ptr<const nlohmann::json::string_t*>();
        if (text == nullptr || !from_string(std::string_view(*text), parsed.initial_level)) {
            return;
        }
    }
    const auto worker = j.find("worker_thread");
    if (worker != j.end() && !core::parse_thread_spec_json(*worker, parsed.worker_thread)) {
        return;
    }
    const auto poll = j.find("worker_poll_interval_us");
    if (poll != j.end()) {
        if (!poll->is_number_unsigned() || poll->get<std::uint64_t>() > 0xffffffffULL) {
            return;
        }
        parsed.worker_poll_interval_us = poll->get<std::uint32_t>();
    }
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
