// nexigen::logging — cold-path JSON serialization for LoggerOptions (change
// rework-logging-hot-path-engine, design D7; spec logging/logger-options).
//
// Strictly non-throwing under `JSON_NOEXCEPTION=1`: every access is guarded by
// `is_*` checks, and a wrong shape leaves the target untouched.
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
#include <nexigen/logging/logger_options.hpp>

namespace nexigen::logging {

namespace {

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

void read_log_level_field(const nlohmann::json& j, const char* key, core::LogLevel& out) noexcept {
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

} // namespace

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

std::string LoggerOptions::to_string() const {
    return "LoggerOptions(log_root_directory=" + log_root_directory +
           ", initial_log_level=" + std::string(nexigen::core::to_string(initial_log_level)) +
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
