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

#include <nexigen/logging/file_logger.hpp>
#include <nexigen/logging/logging_settings.hpp>
#pragma GCC diagnostic pop

#include <cerrno>
#include <charconv>
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

constexpr std::uint64_t kNanoseconds_per_second = 1'000'000'000ULL;

enum class File_read { Absent, Ok, Failed };

[[nodiscard]] File_read read_settings_file(std::string_view path,
                                           std::string& out) noexcept {
    if (path.empty()) {
        return File_read::Absent;
    }
    const std::string path_buf{path};
    const int fd = ::open(path_buf.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return errno == ENOENT ? File_read::Absent : File_read::Failed;
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size < 0) {
        static_cast<void>(::close(fd));
        return File_read::Failed;
    }
    out.resize(static_cast<std::size_t>(st.st_size));
    std::size_t offset = 0;
    while (offset < out.size()) {
        const ssize_t got = ::read(fd, out.data() + offset, out.size() - offset);
        if (got > 0) {
            offset += static_cast<std::size_t>(got);
        } else if (got == 0) {
            break;
        } else if (errno != EINTR) {
            static_cast<void>(::close(fd));
            return File_read::Failed;
        }
    }
    out.resize(offset);
    static_cast<void>(::close(fd));
    return File_read::Ok;
}

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

void add_diagnostic(Logging_settings& settings, Settings_source source,
                    std::string field, std::string message) {
    settings.diagnostics.push_back(
        Settings_diagnostic{source, std::move(field), std::move(message)});
}

/// Validate every known field of one document, keep only the valid ones in
/// `clean`, and leave the rest to the existing LoggerOptions from_json so the
/// field mapping stays in one place. `allow_app_name` is false for the global
/// source (app_name is local-only and is silently ignored there).
void sanitize_document(const nlohmann::json& document, Settings_source source,
                       bool allow_app_name, Logging_settings& settings,
                       nlohmann::json& clean) {
    clean = nlohmann::json::object();
    if (!document.is_object()) {
        add_diagnostic(settings, source, "", "document is not a JSON object");
        return;
    }

    auto it = document.find("log_root_directory");
    if (it != document.end()) {
        std::string value;
        if (plain_string(*it, 511, true, value)) {
            clean["log_root_directory"] = value;
        } else {
            add_diagnostic(settings, source, "log_root_directory",
                           "expected a non-empty string of at most 511 bytes without NUL");
        }
    }

    it = document.find("app_name");
    if (it != document.end() && allow_app_name) {
        std::string value;
        if (plain_string(*it, 63, true, value)) {
            clean["app_name"] = value;
        } else {
            add_diagnostic(settings, source, "app_name",
                           "expected a non-empty string of at most 63 bytes without NUL");
        }
    }

    it = document.find("initial_log_level");
    if (it != document.end()) {
        const auto* const text = string_ptr(*it);
        LogLevel level = settings.options.initial_log_level;
        if (text != nullptr && from_string(std::string_view(*text), level)) {
            clean["initial_log_level"] = *text;
        } else {
            add_diagnostic(settings, source, "initial_log_level",
                           "expected one of verbose/debug/info/warning/error/fatal/off");
        }
    }

    it = document.find("max_file_size_bytes");
    if (it != document.end()) {
        std::uint64_t value = 0;
        if (unsigned_value(*it, value)) {
            clean["max_file_size_bytes"] = value;
        } else {
            add_diagnostic(settings, source, "max_file_size_bytes",
                           "expected a non-negative integer");
        }
    }

    it = document.find("spsc_ring_buffer_capacity");
    if (it != document.end()) {
        settings.ring_capacity_set = true;
        std::uint64_t value = 0;
        if (unsigned_value(*it, value) && value > 0ULL && value <= 0xFFFFFFFFULL) {
            clean["spsc_ring_buffer_capacity"] = static_cast<std::uint32_t>(value);
        } else {
            add_diagnostic(settings, source, "spsc_ring_buffer_capacity",
                           "expected a positive 32-bit integer");
        }
    }

    it = document.find("max_producer_threads");
    if (it != document.end()) {
        settings.max_threads_set = true;
        std::uint64_t value = 0;
        if (unsigned_value(*it, value) && value > 0ULL && value <= 0xFFFFFFFFULL) {
            clean["max_producer_threads"] = static_cast<std::uint32_t>(value);
        } else {
            add_diagnostic(settings, source, "max_producer_threads",
                           "expected a positive 32-bit integer");
        }
    }

    it = document.find("overflow_policy");
    if (it != document.end()) {
        const auto* const text = string_ptr(*it);
        OverflowPolicy policy = settings.options.overflow_policy;
        if (text != nullptr && from_string(std::string_view(*text), policy)) {
            clean["overflow_policy"] = *text;
        } else {
            add_diagnostic(settings, source, "overflow_policy",
                           "expected drop_newest or overwrite_oldest");
        }
    }

    it = document.find("flush_cpu_affinity");
    if (it != document.end()) {
        bool valid = it->is_array();
        std::vector<std::uint64_t> cores;
        if (valid) {
            cores.reserve(it->size());
            for (const auto& item : *it) {
                std::uint64_t core = 0;
                if (!unsigned_value(item, core) || core > 63ULL) {
                    valid = false;
                    break;
                }
                cores.push_back(core);
            }
        }
        if (valid) {
            clean["flush_cpu_affinity"] = cores;
        } else {
            add_diagnostic(settings, source, "flush_cpu_affinity",
                           "expected an array of CPU ids between 0 and 63");
        }
    }

    it = document.find("worker_poll_interval_us");
    if (it != document.end()) {
        std::uint64_t value = 0;
        if (unsigned_value(*it, value) && value <= 0xFFFFFFFFULL) {
            clean["worker_poll_interval_us"] = static_cast<std::uint32_t>(value);
        } else {
            add_diagnostic(settings, source, "worker_poll_interval_us",
                           "expected a 32-bit unsigned integer");
        }
    }

    it = document.find("sync_flush_on_critical");
    if (it != document.end()) {
        if (it->is_boolean()) {
            clean["sync_flush_on_critical"] = it->get<bool>();
        } else {
            add_diagnostic(settings, source, "sync_flush_on_critical",
                           "expected a boolean");
        }
    }

    it = document.find("spam_protection");
    if (it != document.end()) {
        if (!it->is_object()) {
            add_diagnostic(settings, source, "spam_protection",
                           "expected a JSON object");
        } else {
            nlohmann::json spam = nlohmann::json::object();
            bool valid = true;
            const auto check_unsigned = [&](const char* key, std::uint64_t max_value) {
                const auto entry = it->find(key);
                if (entry == it->end()) {
                    return;
                }
                std::uint64_t value = 0;
                if (unsigned_value(*entry, value) && value <= max_value) {
                    spam[key] = value;
                } else {
                    valid = false;
                    add_diagnostic(settings, source,
                                   std::string("spam_protection.") + key,
                                   "expected a non-negative integer in range");
                }
            };
            const auto entry = it->find("enable_repeat_suppression");
            if (entry != it->end()) {
                if (entry->is_boolean()) {
                    spam["enable_repeat_suppression"] = entry->get<bool>();
                } else {
                    valid = false;
                    add_diagnostic(settings, source,
                                   "spam_protection.enable_repeat_suppression",
                                   "expected a boolean");
                }
            }
            check_unsigned("repeat_suppression_window_ms", 0xFFFFFFFFULL);
            check_unsigned("repeat_suppression_max_count", 0xFFFFFFFFULL);
            check_unsigned("max_messages_per_sec_per_shard", 0xFFFFFFFFULL);
            check_unsigned("max_bytes_per_sec_per_shard", 0xFFFFFFFFFFFFFFFFULL);
            check_unsigned("max_total_bytes_per_sec", 0xFFFFFFFFFFFFFFFFULL);

            const auto bypass = it->find("bypass_min_level");
            if (bypass != it->end()) {
                const auto* const text = string_ptr(*bypass);
                LogLevel level = settings.options.spam_protection.bypass_min_level;
                if (text != nullptr && from_string(std::string_view(*text), level)) {
                    spam["bypass_min_level"] = *text;
                } else {
                    valid = false;
                    add_diagnostic(settings, source,
                                   "spam_protection.bypass_min_level",
                                   "expected a known log level name");
                }
            }
            if (valid) {
                clean["spam_protection"] = std::move(spam);
            }
        }
    }
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

/// "0,2-3" -> {0, 2, 3}. Rejects empty tokens, inverted ranges and ids > 63.
[[nodiscard]] bool parse_affinity(std::string_view text,
                                  std::vector<std::uint64_t>& out) noexcept {
    out.clear();
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
        if (last > 63ULL) {
            return false;
        }
        for (std::uint64_t core = first; core <= last; ++core) {
            out.push_back(core);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        pos = comma + 1;
    }
    return !out.empty();
}

void apply_environment(Logging_settings& settings) noexcept {
    const auto env = [](const char* name) -> std::string_view {
        const char* const value = std::getenv(name);
        return value == nullptr ? std::string_view{} : std::string_view{value};
    };

    std::string_view value = env("NEXIGEN_LOG_LEVEL");
    if (!value.empty()) {
        LogLevel level = settings.options.initial_log_level;
        if (from_string(value, level)) {
            settings.options.initial_log_level = level;
        } else {
            add_diagnostic(settings, Settings_source::Environment, "initial_log_level",
                           "expected one of verbose/debug/info/warning/error/fatal/off");
        }
    }

    value = env("NEXIGEN_LOG_DIR");
    if (!value.empty()) {
        if (value.size() <= 511 && value.find('\0') == std::string_view::npos) {
            settings.options.log_root_directory = std::string(value);
        } else {
            add_diagnostic(settings, Settings_source::Environment, "log_root_directory",
                           "expected a path of at most 511 bytes without NUL");
        }
    }

    value = env("NEXIGEN_LOG_APP_NAME");
    if (!value.empty()) {
        if (value.size() <= 63 && value.find('\0') == std::string_view::npos) {
            settings.options.app_name = std::string(value);
        } else {
            add_diagnostic(settings, Settings_source::Environment, "app_name",
                           "expected at most 63 bytes without NUL");
        }
    }

    value = env("NEXIGEN_LOG_MAX_FILE_SIZE_BYTES");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed)) {
            settings.options.max_file_size_bytes = parsed;
        } else {
            add_diagnostic(settings, Settings_source::Environment,
                           "max_file_size_bytes", "expected a non-negative integer");
        }
    }

    value = env("NEXIGEN_LOG_CPU_AFFINITY");
    if (!value.empty()) {
        std::vector<std::uint64_t> cores;
        if (parse_affinity(value, cores)) {
            settings.options.flush_cpu_affinity = std::move(cores);
        } else {
            add_diagnostic(settings, Settings_source::Environment,
                           "flush_cpu_affinity",
                           "expected a comma-separated list of ids/ranges between 0 and 63");
        }
    }

    value = env("NEXIGEN_LOG_RING_CAPACITY");
    if (!value.empty()) {
        settings.ring_capacity_set = true;
        std::uint32_t parsed = 0;
        if (parse_u32(value, parsed) && parsed > 0U) {
            settings.options.spsc_ring_buffer_capacity = parsed;
        } else {
            add_diagnostic(settings, Settings_source::Environment,
                           "spsc_ring_buffer_capacity",
                           "expected a positive 32-bit integer");
        }
    }

    value = env("NEXIGEN_LOG_MAX_PRODUCER_THREADS");
    if (!value.empty()) {
        settings.max_threads_set = true;
        std::uint32_t parsed = 0;
        if (parse_u32(value, parsed) && parsed > 0U) {
            settings.options.max_producer_threads = parsed;
        } else {
            add_diagnostic(settings, Settings_source::Environment,
                           "max_producer_threads",
                           "expected a positive 32-bit integer");
        }
    }

    value = env("NEXIGEN_LOG_OVERFLOW_POLICY");
    if (!value.empty()) {
        OverflowPolicy policy = settings.options.overflow_policy;
        if (from_string(value, policy)) {
            settings.options.overflow_policy = policy;
        } else {
            add_diagnostic(settings, Settings_source::Environment, "overflow_policy",
                           "expected drop_newest or overwrite_oldest");
        }
    }
}

} // namespace

std::string Settings_paths::to_string() const {
    return "Settings_paths(global_file=\"" + global_file + "\", local_file=\"" +
           local_file + "\")";
}

std::string Settings_diagnostic::to_string() const {
    return "Settings_diagnostic(source=" + std::string(nexigen::logging::to_string(source)) +
           ", field=" + field + ", message=" + message + ")";
}

std::string Logging_settings::to_string() const {
    std::string out = "Logging_settings(global_loaded=" +
                      std::string(global_loaded ? "true" : "false") +
                      ", local_loaded=" + std::string(local_loaded ? "true" : "false") +
                      ", ring_capacity_set=" +
                      std::string(ring_capacity_set ? "true" : "false") +
                      ", max_threads_set=" +
                      std::string(max_threads_set ? "true" : "false") +
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

void to_json(nlohmann::json& j, const Settings_paths& paths) {
    j = nlohmann::json{{"global_file", paths.global_file},
                       {"local_file", paths.local_file}};
}

void from_json(const nlohmann::json& j, Settings_paths& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    auto it = j.find("global_file");
    if (it != j.end() && it->is_string()) {
        out.global_file = it->get<std::string>();
    }
    it = j.find("local_file");
    if (it != j.end() && it->is_string()) {
        out.local_file = it->get<std::string>();
    }
}

void to_json(nlohmann::json& j, const Settings_diagnostic& diag) {
    j = nlohmann::json{{"source", std::string(nexigen::logging::to_string(diag.source))},
                       {"field", diag.field},
                       {"message", diag.message}};
}

void from_json(const nlohmann::json& j, Settings_diagnostic& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    auto it = j.find("source");
    if (it != j.end()) {
        const auto* const name = string_ptr(*it);
        if (name != nullptr) {
            static_cast<void>(from_string(std::string_view(*name), out.source));
        }
    }
    it = j.find("field");
    if (it != j.end() && it->is_string()) {
        out.field = it->get<std::string>();
    }
    it = j.find("message");
    if (it != j.end() && it->is_string()) {
        out.message = it->get<std::string>();
    }
}

void to_json(nlohmann::json& j, const Logging_settings& settings) {
    nlohmann::json options;
    to_json(options, settings.options);
    nlohmann::json diagnostics = nlohmann::json::array();
    for (const Settings_diagnostic& diag : settings.diagnostics) {
        nlohmann::json entry;
        to_json(entry, diag);
        diagnostics.push_back(std::move(entry));
    }
    j = nlohmann::json{{"global_loaded", settings.global_loaded},
                       {"local_loaded", settings.local_loaded},
                       {"ring_capacity_set", settings.ring_capacity_set},
                       {"max_threads_set", settings.max_threads_set},
                       {"options", std::move(options)},
                       {"diagnostics", std::move(diagnostics)}};
}

core::Status load_logging_settings(const Settings_paths& paths,
                                   Logging_settings& out) noexcept {
    out = Logging_settings{};
    bool invalid = false;
    bool system_error = false;

    std::string text;
    File_read read = read_settings_file(paths.global_file, text);
    if (read == File_read::Ok) {
        out.global_loaded = true;
        nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out, Settings_source::Global, "", "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            nlohmann::json clean;
            sanitize_document(document, Settings_source::Global, false, out, clean);
            from_json(clean, out.options);
            if (out.diagnostics.size() != before) {
                invalid = true;
            }
        }
    } else if (read == File_read::Failed) {
        system_error = true;
        add_diagnostic(out, Settings_source::Global, "",
                       "file exists but could not be read");
    }

    text.clear();
    read = read_settings_file(paths.local_file, text);
    if (read == File_read::Ok) {
        out.local_loaded = true;
        nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out, Settings_source::Local, "", "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            nlohmann::json clean;
            sanitize_document(document, Settings_source::Local, true, out, clean);
            from_json(clean, out.options);
            if (out.diagnostics.size() != before) {
                invalid = true;
            }
        }
    } else if (read == File_read::Failed) {
        system_error = true;
        add_diagnostic(out, Settings_source::Local, "",
                       "file exists but could not be read");
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

core::Status apply_to(const Logging_settings& settings, FileLogger_config& out) noexcept {
    const LoggerOptions& options = settings.options;
    if (options.log_root_directory.empty() || options.log_root_directory.size() > 511 ||
        options.log_root_directory.find('\0') != std::string::npos ||
        options.app_name.empty() || options.app_name.size() > 63 ||
        options.app_name.find('\0') != std::string::npos) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    std::uint64_t core_mask = 0;
    for (const std::uint64_t core : options.flush_cpu_affinity) {
        if (core > 63ULL) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        core_mask |= (1ULL << core);
    }

    out = FileLogger_config{};
    out.app_name = options.app_name;
    out.base_dir = options.log_root_directory;
    out.initial_level = options.initial_log_level;
    out.max_file_size_bytes = options.max_file_size_bytes;
    out.core_mask = core_mask;

    const std::uint64_t per_second =
        options.spam_protection.max_messages_per_sec_per_shard;
    if (per_second != 0ULL) {
        out.throttle.enabled = true;
        out.throttle.max_records_per_window = per_second;
        out.throttle.window_ns = kNanoseconds_per_second;
    }
    return {};
}

} // namespace nexigen::logging
