// nexigen::logging — cold-path implementation of the global/local settings
// loader (change add-logging-settings-loader; design D1–D5).
//
// Everything in this unit is initialization-time code: file I/O, JSON,
// std::string and diagnostics are explicitly allowed here and never reach a
// producer or writer hot path. nlohmann/json headers trigger
// -Wnull-dereference under -O3 (pre-existing upstream issue), so the pragma
// guards this cold unit only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>

#include <nexigen/core/settings_io.hpp>
#include <nexigen/logging/settings.hpp>
#pragma GCC diagnostic pop

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nexigen::logging {
namespace {

// Shared settings-io helpers (change fix-p2-observability-and-writers);
// Imports stay inside the TU's anonymous namespace.
using nexigen::core::settings_io::FileRead;
using nexigen::core::settings_io::read_text_file;

[[nodiscard]] const nlohmann::json::string_t* string_ptr(const nlohmann::json& value) noexcept {
    return value.get_ptr<const nlohmann::json::string_t*>();
}

[[nodiscard]] bool plain_string(const nlohmann::json& value, std::size_t max_size,
                                bool require_non_empty, std::string& out) noexcept {
    const auto* const text = string_ptr(value);
    if (text == nullptr) {
        return false;
    }
    if (text->empty() && require_non_empty) {
        return false;
    }
    if (text->size() > max_size || text->find('\0') != std::string::npos) {
        return false;
    }
    out = *text;
    return true;
}

[[nodiscard]] bool unsigned_value(const nlohmann::json& value, std::uint64_t& out) noexcept {
    if (!value.is_number_unsigned()) {
        return false;
    }
    out = value.get<std::uint64_t>();
    return true;
}

void add_diagnostic(Settings& settings, core::SettingsSource source, std::string field,
                    std::string message) {
    // Delegates to the shared vector helper (change
    // fix-p2-observability-and-writers): one implementation.
    nexigen::core::settings_io::add_diagnostic(settings.diagnostics, source, std::move(field),
                                               std::move(message));
}

void sanitize_string_field(const nlohmann::json& document, core::SettingsSource source,
                           const char* key, std::size_t max_size, bool require_non_empty,
                           const char* error_message, Settings& settings, nlohmann::json& clean) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return;
    }
    std::string value;
    if (plain_string(*it, max_size, require_non_empty, value)) {
        clean[key] = std::move(value);
    } else {
        add_diagnostic(settings, source, key, error_message);
    }
}

void sanitize_log_level_field(const nlohmann::json& document, core::SettingsSource source,
                              const char* key, LogLevel fallback, const char* error_message,
                              Settings& settings, nlohmann::json& clean) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return;
    }
    const auto* const text = string_ptr(*it);
    LogLevel level = fallback;
    if (text != nullptr && from_string(std::string_view(*text), level)) {
        clean[key] = *text;
    } else {
        add_diagnostic(settings, source, key, error_message);
    }
}

void sanitize_unsigned_field(const nlohmann::json& document, core::SettingsSource source,
                             const char* key, const char* error_message, Settings& settings,
                             nlohmann::json& clean) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return;
    }
    std::uint64_t value = 0;
    if (unsigned_value(*it, value)) {
        clean[key] = value;
    } else {
        add_diagnostic(settings, source, key, error_message);
    }
}

void sanitize_positive_u32_field(const nlohmann::json& document, core::SettingsSource source,
                                 const char* key, bool& set_flag, const char* error_message,
                                 Settings& settings, nlohmann::json& clean) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return;
    }
    set_flag = true;
    std::uint64_t value = 0;
    if (unsigned_value(*it, value) && value > 0ULL && value <= 0xFFFFFFFFULL) {
        clean[key] = static_cast<std::uint32_t>(value);
    } else {
        add_diagnostic(settings, source, key, error_message);
    }
}

void sanitize_bool_field(const nlohmann::json& document, core::SettingsSource source,
                         const char* key, const char* error_message, Settings& settings,
                         nlohmann::json& clean) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return;
    }
    if (it->is_boolean()) {
        clean[key] = it->get<bool>();
    } else {
        add_diagnostic(settings, source, key, error_message);
    }
}

void sanitize_overflow_policy_field(const nlohmann::json& document, core::SettingsSource source,
                                    Settings& settings, nlohmann::json& clean) {
    const char* key = "overflow_policy";
    const auto it = document.find(key);
    if (it == document.end()) {
        return;
    }
    const auto* const text = string_ptr(*it);
    OverflowPolicy policy = settings.options.overflow_policy;
    if (text != nullptr && from_string(std::string_view(*text), policy)) {
        clean[key] = *text;
    } else {
        add_diagnostic(settings, source, key, "expected drop_newest or overwrite_oldest");
    }
}

void sanitize_core_mask_field(const nlohmann::json& document, core::SettingsSource source,
                              Settings& settings, nlohmann::json& clean) {
    const char* key = "core_mask";
    const auto it = document.find(key);
    if (it == document.end()) {
        return;
    }
    std::uint64_t value = 0;
    if (unsigned_value(*it, value)) {
        clean[key] = value;
    } else {
        add_diagnostic(settings, source, key, "expected a non-negative 64-bit mask");
    }
}

void sanitize_flush_cpu_affinity_field(const nlohmann::json& document, core::SettingsSource source,
                                       Settings& settings, nlohmann::json& clean) {
    const char* key = "flush_cpu_affinity";
    const auto it = document.find(key);
    if (it == document.end()) {
        return;
    }
    core::CpuSet cpus{};
    if (core::parse_cpu_set_json(*it, cpus)) {
        clean[key] = *it;
    } else {
        add_diagnostic(settings, source, key,
                       "expected a unique array of CPU ids between 0 and 1023");
    }
}

void sanitize_worker_thread_field(const nlohmann::json& document, core::SettingsSource source,
                                  Settings& settings, nlohmann::json& clean) {
    const char* key = "worker_thread";
    const auto it = document.find(key);
    if (it == document.end()) {
        return;
    }
    settings.worker_thread_set = true;
    if (it->contains("cpus")) {
        settings.worker_cpus_set = true;
    }
    core::ThreadSpec worker = settings.options.worker_thread;
    if (core::parse_thread_spec_json(*it, worker)) {
        clean[key] = *it;
    } else {
        add_diagnostic(settings, source, key,
                       "expected valid name/cpus/policy/priority/mode fields");
    }
}

void sanitize_spam_protection_unsigned_field(const nlohmann::json& parent,
                                             core::SettingsSource source, const char* key,
                                             std::uint64_t max_value, bool& valid,
                                             nlohmann::json& spam, Settings& settings) {
    const auto entry = parent.find(key);
    if (entry == parent.end()) {
        return;
    }
    std::uint64_t value = 0;
    if (unsigned_value(*entry, value) && value <= max_value) {
        spam[key] = value;
    } else {
        valid = false;
        add_diagnostic(settings, source, std::string("spam_protection.") + key,
                       "expected a non-negative integer in range");
    }
}

void sanitize_spam_protection_bool_field(const nlohmann::json& parent, core::SettingsSource source,
                                         const char* key, bool& valid, nlohmann::json& spam,
                                         Settings& settings) {
    const auto entry = parent.find(key);
    if (entry == parent.end()) {
        return;
    }
    if (entry->is_boolean()) {
        spam[key] = entry->get<bool>();
    } else {
        valid = false;
        add_diagnostic(settings, source, std::string("spam_protection.") + key,
                       "expected a boolean");
    }
}

void sanitize_spam_protection_level_field(const nlohmann::json& parent, core::SettingsSource source,
                                          bool& valid, nlohmann::json& spam, Settings& settings) {
    const char* key = "bypass_min_level";
    const auto entry = parent.find(key);
    if (entry == parent.end()) {
        return;
    }
    const auto* const text = string_ptr(*entry);
    LogLevel level = settings.options.spam_protection.bypass_min_level;
    if (text != nullptr && from_string(std::string_view(*text), level)) {
        spam[key] = *text;
    } else {
        valid = false;
        add_diagnostic(settings, source, std::string("spam_protection.") + key,
                       "expected a known log level name");
    }
}

void sanitize_spam_protection_field(const nlohmann::json& document, core::SettingsSource source,
                                    Settings& settings, nlohmann::json& clean) {
    const char* key = "spam_protection";
    const auto it = document.find(key);
    if (it == document.end()) {
        return;
    }
    if (!it->is_object()) {
        add_diagnostic(settings, source, key, "expected a JSON object");
        return;
    }
    nlohmann::json spam = nlohmann::json::object();
    bool valid = true;
    sanitize_spam_protection_bool_field(*it, source, "enable_repeat_suppression", valid, spam,
                                        settings);
    sanitize_spam_protection_unsigned_field(*it, source, "repeat_suppression_window_ms",
                                            0xFFFFFFFFULL, valid, spam, settings);
    sanitize_spam_protection_unsigned_field(*it, source, "max_messages_per_sec_per_shard",
                                            0xFFFFFFFFULL, valid, spam, settings);
    sanitize_spam_protection_level_field(*it, source, valid, spam, settings);
    if (valid) {
        clean[key] = std::move(spam);
    }
}

/// Validate every known field of one document, keep only the valid ones in
/// `clean`, and leave the rest to the existing LoggerOptions from_json so the
/// field mapping stays in one place.
void sanitize_document(const nlohmann::json& document, core::SettingsSource source,
                       Settings& settings, nlohmann::json& clean) {
    clean = nlohmann::json::object();
    if (!document.is_object()) {
        add_diagnostic(settings, source, "", "document is not a JSON object");
        return;
    }

    sanitize_string_field(document, source, "log_root_directory", 511, true,
                          "expected a non-empty string of at most 511 bytes without NUL", settings,
                          clean);
    sanitize_log_level_field(
        document, source, "initial_log_level", settings.options.initial_log_level,
        "expected one of verbose/debug/info/warning/error/fatal/off", settings, clean);
    sanitize_unsigned_field(document, source, "max_file_size_bytes",
                            "expected a non-negative integer", settings, clean);
    sanitize_positive_u32_field(document, source, "spsc_ring_buffer_capacity",
                                settings.ring_capacity_set, "expected a positive 32-bit integer",
                                settings, clean);
    sanitize_positive_u32_field(document, source, "max_producer_threads", settings.max_threads_set,
                                "expected a positive 32-bit integer", settings, clean);
    sanitize_positive_u32_field(document, source, "message_capacity", settings.message_capacity_set,
                                "expected a positive 32-bit integer", settings, clean);
    sanitize_overflow_policy_field(document, source, settings, clean);
    sanitize_core_mask_field(document, source, settings, clean);
    sanitize_flush_cpu_affinity_field(document, source, settings, clean);
    sanitize_worker_thread_field(document, source, settings, clean);
    sanitize_unsigned_field(document, source, "worker_poll_interval_us",
                            "expected a 32-bit unsigned integer", settings, clean);
    sanitize_bool_field(document, source, "sync_flush_on_critical", "expected a boolean", settings,
                        clean);
    sanitize_spam_protection_field(document, source, settings, clean);
}

[[nodiscard]] bool parse_u64(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool parse_u32(std::string_view text, std::uint32_t& out) noexcept {
    std::uint64_t wide = 0;
    if (!parse_u64(text, wide) || wide > 0xFFFFFFFFULL) {
        return false;
    }
    out = static_cast<std::uint32_t>(wide);
    return true;
}

/// "0,2-3" -> CpuSet{0, 2, 3}. Rejects empty tokens, inverted ranges,
/// duplicates, and ids above 1023.
[[nodiscard]] bool parse_affinity(std::string_view text, core::CpuSet& out) noexcept {
    out = core::CpuSet{};
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t comma = text.find(',', pos);
        const std::string_view token =
            comma == std::string_view::npos ? text.substr(pos) : text.substr(pos, comma - pos);
        if (token.empty()) {
            return false;
        }
        const std::size_t dash = token.find('-');
        std::uint64_t first = 0;
        std::uint64_t last = 0;
        if (dash == std::string_view::npos) {
            if (!parse_u64(token, first)) {
                return false;
            }
            last = first;
        } else {
            if (!parse_u64(token.substr(0, dash), first) ||
                !parse_u64(token.substr(dash + 1), last) || first > last) {
                return false;
            }
        }
        if (last > core::CpuSet::max_cpu_id) {
            return false;
        }
        for (std::uint64_t cpu = first; cpu <= last; ++cpu) {
            if (!out.add(static_cast<std::int64_t>(cpu))) {
                return false;
            }
        }
        if (comma == std::string_view::npos) {
            break;
        }
        pos = comma + 1;
    }
    return !out.empty();
}

[[nodiscard]] std::string_view env_value(const char* name) noexcept {
    const char* const value = std::getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

void apply_log_level_env(std::string_view value, Settings& settings) noexcept {
    if (value.empty()) {
        return;
    }
    LogLevel level = settings.options.initial_log_level;
    if (from_string(value, level)) {
        settings.options.initial_log_level = level;
    } else {
        add_diagnostic(settings, core::SettingsSource::Environment, "initial_log_level",
                       "expected one of verbose/debug/info/warning/error/fatal/off");
    }
}

void apply_log_dir_env(std::string_view value, Settings& settings) noexcept {
    if (value.empty()) {
        return;
    }
    if (value.size() <= 511 && value.find('\0') == std::string_view::npos) {
        settings.options.log_root_directory = std::string(value);
    } else {
        add_diagnostic(settings, core::SettingsSource::Environment, "log_root_directory",
                       "expected a path of at most 511 bytes without NUL");
    }
}

void apply_u64_env(std::string_view value, const char* field, std::uint64_t& target,
                   Settings& settings) noexcept {
    if (value.empty()) {
        return;
    }
    std::uint64_t parsed = 0;
    if (parse_u64(value, parsed)) {
        target = parsed;
    } else {
        add_diagnostic(settings, core::SettingsSource::Environment, field,
                       "expected a non-negative integer");
    }
}

void apply_cpu_affinity_env(std::string_view value, Settings& settings) noexcept {
    if (value.empty()) {
        return;
    }
    core::CpuSet cpus{};
    if (parse_affinity(value, cpus)) {
        settings.options.worker_thread.set_cpus(cpus);
    } else {
        add_diagnostic(settings, core::SettingsSource::Environment, "flush_cpu_affinity",
                       "expected a unique comma-separated list of ids/ranges between 0 and 1023");
    }
}

void apply_positive_u32_env(std::string_view value, const char* field, bool& set_flag,
                            std::uint32_t& target, Settings& settings) noexcept {
    if (value.empty()) {
        return;
    }
    set_flag = true;
    std::uint32_t parsed = 0;
    if (parse_u32(value, parsed) && parsed > 0U) {
        target = parsed;
    } else {
        add_diagnostic(settings, core::SettingsSource::Environment, field,
                       "expected a positive 32-bit integer");
    }
}

void apply_overflow_policy_env(std::string_view value, Settings& settings) noexcept {
    if (value.empty()) {
        return;
    }
    OverflowPolicy policy = settings.options.overflow_policy;
    if (from_string(value, policy)) {
        settings.options.overflow_policy = policy;
    } else {
        add_diagnostic(settings, core::SettingsSource::Environment, "overflow_policy",
                       "expected drop_newest or overwrite_oldest");
    }
}

void apply_environment(Settings& settings) noexcept {
    apply_log_level_env(env_value("NEXIGEN_LOG_LEVEL"), settings);
    apply_log_dir_env(env_value("NEXIGEN_LOG_DIR"), settings);
    apply_u64_env(env_value("NEXIGEN_LOG_MAX_FILE_SIZE_BYTES"), "max_file_size_bytes",
                  settings.options.max_file_size_bytes, settings);
    apply_cpu_affinity_env(env_value("NEXIGEN_LOG_CPU_AFFINITY"), settings);
    apply_positive_u32_env(env_value("NEXIGEN_LOG_RING_CAPACITY"), "spsc_ring_buffer_capacity",
                           settings.ring_capacity_set, settings.options.spsc_ring_buffer_capacity,
                           settings);
    apply_positive_u32_env(env_value("NEXIGEN_LOG_MESSAGE_CAPACITY"), "message_capacity",
                           settings.message_capacity_set, settings.options.message_capacity,
                           settings);
    apply_positive_u32_env(env_value("NEXIGEN_LOG_MAX_PRODUCER_THREADS"), "max_producer_threads",
                           settings.max_threads_set, settings.options.max_producer_threads,
                           settings);
    apply_overflow_policy_env(env_value("NEXIGEN_LOG_OVERFLOW_POLICY"), settings);
}

} // namespace

std::string Settings::to_string() const {
    std::string out =
        "Settings(global_loaded=" + std::string(global_loaded ? "true" : "false") +
        ", local_loaded=" + std::string(local_loaded ? "true" : "false") +
        ", ring_capacity_set=" + std::string(ring_capacity_set ? "true" : "false") +
        ", max_threads_set=" + std::string(max_threads_set ? "true" : "false") +
        ", message_capacity_set=" + std::string(message_capacity_set ? "true" : "false") +
        ", options=" + options.to_string() + ", diagnostics=[";
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        if (i != 0) {
            out += "; ";
        }
        out += diagnostics[i].to_string();
    }
    out += "])";
    return out;
}

void to_json(nlohmann::json& j, const Settings& settings) {
    nlohmann::json options;
    to_json(options, settings.options);
    nlohmann::json diagnostics = nlohmann::json::array();
    for (const core::SettingsDiagnostic& diag : settings.diagnostics) {
        nlohmann::json entry;
        to_json(entry, diag);
        diagnostics.push_back(std::move(entry));
    }
    j = nlohmann::json{{"global_loaded", settings.global_loaded},
                       {"local_loaded", settings.local_loaded},
                       {"ring_capacity_set", settings.ring_capacity_set},
                       {"max_threads_set", settings.max_threads_set},
                       {"message_capacity_set", settings.message_capacity_set},
                       {"options", std::move(options)},
                       {"diagnostics", std::move(diagnostics)}};
}

core::Status load_logging_settings(const core::SettingsPaths& paths, Settings& out) noexcept {
    out = Settings{};
    bool invalid = false;
    bool system_error = false;

    std::string text;
    FileRead read = read_text_file(paths.global_file, text);
    if (read == FileRead::Ok) {
        out.global_loaded = true;
        nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out, core::SettingsSource::Global, "", "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            nlohmann::json clean;
            sanitize_document(document, core::SettingsSource::Global, out, clean);
            from_json(clean, out.options);
            if (out.diagnostics.size() != before) {
                invalid = true;
            }
        }
    } else if (read == FileRead::Failed) {
        system_error = true;
        add_diagnostic(out, core::SettingsSource::Global, "", "file exists but could not be read");
    }

    text.clear();
    read = read_text_file(paths.local_file, text);
    if (read == FileRead::Ok) {
        out.local_loaded = true;
        nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out, core::SettingsSource::Local, "", "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            nlohmann::json clean;
            sanitize_document(document, core::SettingsSource::Local, out, clean);
            from_json(clean, out.options);
            if (out.diagnostics.size() != before) {
                invalid = true;
            }
        }
    } else if (read == FileRead::Failed) {
        system_error = true;
        add_diagnostic(out, core::SettingsSource::Local, "", "file exists but could not be read");
    }

    const std::size_t before_env = out.diagnostics.size();
    apply_environment(out);
    if (out.diagnostics.size() != before_env) {
        invalid = true;
    }

    if (system_error) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (invalid) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

} // namespace nexigen::logging
