// nexigen::app — cold-path settings loaders (change add-app-class; design D7).
//
// This unit duplicates the small file/JSON reader used by logging rather than
// importing another module's detail/ (architecture rule: dependency arrows
// stay one-way; shared machinery is promoted or duplicated). Everything here
// runs during initialization: std::string, nlohmann::json and diagnostics are
// allowed; no producer or tick path calls into this unit.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <nexigen/app/app_settings.hpp>

#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nexigen::app {
namespace {

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

void add_diagnostic(std::vector<Settings_diagnostic>& diagnostics,
                    logging::Settings_source source, std::string field,
                    std::string message) {
    diagnostics.push_back(Settings_diagnostic{source, std::move(field), std::move(message)});
}

[[nodiscard]] bool unsigned_value(const nlohmann::json& value, std::uint64_t& out) noexcept {
    if (!value.is_number_unsigned()) {
        return false;
    }
    out = value.get<std::uint64_t>();
    return true;
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

/// Read a JSON key into a uint64 target when present and valid.
void read_uint(const nlohmann::json& document, const char* key, std::uint64_t max_value,
               std::uint64_t& target, std::vector<Settings_diagnostic>& diagnostics,
               logging::Settings_source source) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return;
    }
    std::uint64_t value = 0;
    if (unsigned_value(*it, value) && value <= max_value) {
        target = value;
    } else {
        add_diagnostic(diagnostics, source, key, "expected an integer in range");
    }
}

} // namespace

std::string Metrics_settings::to_string() const {
    std::string out = "Metrics_settings(global_loaded=" +
                      std::string(global_loaded ? "true" : "false") +
                      ", local_loaded=" + std::string(local_loaded ? "true" : "false") +
                      ", options=" + nexigen::metrics::to_string(options) +
                      ", diagnostics=[";
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        if (i != 0) {
            out += "; ";
        }
        out += diagnostics[i].to_string();
    }
    out += "])";
    return out;
}

std::string Tracing_settings::to_string() const {
    std::string out = "Tracing_settings(global_loaded=" +
                      std::string(global_loaded ? "true" : "false") +
                      ", local_loaded=" + std::string(local_loaded ? "true" : "false") +
                      ", options=" + nexigen::tracing::to_string(options) +
                      ", diagnostics=[";
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        if (i != 0) {
            out += "; ";
        }
        out += diagnostics[i].to_string();
    }
    out += "])";
    return out;
}

std::string App_settings_paths::to_string() const {
    return "App_settings_paths(logging=" + logging.to_string() +
           ", metrics=" + metrics.to_string() + ", tracing=" + tracing.to_string() +
           ", chrono=" + chrono.to_string() + ")";
}

std::string App_settings::to_string() const {
    return "App_settings(logging=" + logging.to_string() +
           ", metrics=" + metrics.to_string() + ", tracing=" + tracing.to_string() +
           ", chrono=" + chrono.to_string() + ")";
}

void to_json(nlohmann::json& j, const Metrics_settings& settings) {
    const metrics::MetricProvider_config& options = settings.options;
    nlohmann::json diagnostics = nlohmann::json::array();
    for (const Settings_diagnostic& diag : settings.diagnostics) {
        nlohmann::json entry;
        to_json(entry, diag);
        diagnostics.push_back(std::move(entry));
    }
    j = nlohmann::json{{"global_loaded", settings.global_loaded},
                       {"local_loaded", settings.local_loaded},
                       {"collect_interval_ns", options.collect_interval.count()},
                       {"core_mask", options.core_mask},
                       {"max_meters", options.max_meters},
                       {"max_counters", options.max_counters},
                       {"max_gauges", options.max_gauges},
                       {"max_statistics", options.max_statistics},
                       {"max_explicit_histograms", options.max_explicit_histograms},
                       {"max_exponential_histograms", options.max_exponential_histograms},
                       {"diagnostics", std::move(diagnostics)}};
}

void to_json(nlohmann::json& j, const Tracing_settings& settings) {
    const tracing::TracerProvider_config& options = settings.options;
    nlohmann::json diagnostics = nlohmann::json::array();
    for (const Settings_diagnostic& diag : settings.diagnostics) {
        nlohmann::json entry;
        to_json(entry, diag);
        diagnostics.push_back(std::move(entry));
    }
    j = nlohmann::json{{"global_loaded", settings.global_loaded},
                       {"local_loaded", settings.local_loaded},
                       {"core_mask", options.core_mask},
                       {"max_tracers", options.max_tracers},
                       {"max_spans_per_window", options.max_spans_per_window},
                       {"window_ns", options.window_ns},
                       {"diagnostics", std::move(diagnostics)}};
}

void to_json(nlohmann::json& j, const App_settings_paths& paths) {
    j = nlohmann::json{{"logging", nlohmann::json{{"global_file", paths.logging.global_file},
                                                  {"local_file", paths.logging.local_file}}},
                       {"metrics", nlohmann::json{{"global_file", paths.metrics.global_file},
                                                  {"local_file", paths.metrics.local_file}}},
                       {"tracing", nlohmann::json{{"global_file", paths.tracing.global_file},
                                                  {"local_file", paths.tracing.local_file}}},
                       {"chrono", nlohmann::json{{"global_file", paths.chrono.global_file},
                                                 {"local_file", paths.chrono.local_file}}}};
}

void to_json(nlohmann::json& j, const App_settings& settings) {
    nlohmann::json logging_json;
    to_json(logging_json, settings.logging);
    nlohmann::json metrics_json;
    to_json(metrics_json, settings.metrics);
    nlohmann::json tracing_json;
    to_json(tracing_json, settings.tracing);
    nlohmann::json chrono_json;
    to_json(chrono_json, settings.chrono);
    j = nlohmann::json{{"logging", std::move(logging_json)},
                       {"metrics", std::move(metrics_json)},
                       {"tracing", std::move(tracing_json)},
                       {"chrono", std::move(chrono_json)}};
}

namespace {

void merge_metrics_document(const nlohmann::json& document,
                            logging::Settings_source source,
                            Metrics_settings& out) {
    if (!document.is_object()) {
        add_diagnostic(out.diagnostics, source, "", "document is not a JSON object");
        return;
    }
    std::uint64_t interval = static_cast<std::uint64_t>(
        out.options.collect_interval.count() < 0 ? 0 : out.options.collect_interval.count());
    const std::size_t before = out.diagnostics.size();
    read_uint(document, "collect_interval_ns",
              static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), interval,
              out.diagnostics, source);
    if (out.diagnostics.size() == before) {
        out.options.collect_interval = std::chrono::nanoseconds{static_cast<std::int64_t>(interval)};
    }
    std::uint64_t core_mask = out.options.core_mask;
    read_uint(document, "core_mask", std::numeric_limits<std::uint64_t>::max(), core_mask,
              out.diagnostics, source);
    out.options.core_mask = core_mask;

    const auto read_u32 = [&](const char* key, std::uint32_t& target) {
        std::uint64_t value = target;
        const std::size_t diagnostics_before = out.diagnostics.size();
        read_uint(document, key, std::numeric_limits<std::uint32_t>::max(), value,
                  out.diagnostics, source);
        if (out.diagnostics.size() == diagnostics_before) {
            target = static_cast<std::uint32_t>(value);
        }
    };
    read_u32("max_meters", out.options.max_meters);
    read_u32("max_counters", out.options.max_counters);
    read_u32("max_gauges", out.options.max_gauges);
    read_u32("max_statistics", out.options.max_statistics);
    read_u32("max_explicit_histograms", out.options.max_explicit_histograms);
    read_u32("max_exponential_histograms", out.options.max_exponential_histograms);
}

void merge_tracing_document(const nlohmann::json& document,
                            logging::Settings_source source,
                            Tracing_settings& out) {
    if (!document.is_object()) {
        add_diagnostic(out.diagnostics, source, "", "document is not a JSON object");
        return;
    }
    std::uint64_t core_mask = out.options.core_mask;
    read_uint(document, "core_mask", std::numeric_limits<std::uint64_t>::max(), core_mask,
              out.diagnostics, source);
    out.options.core_mask = core_mask;

    std::uint64_t max_tracers = out.options.max_tracers;
    const std::size_t before_tracers = out.diagnostics.size();
    read_uint(document, "max_tracers", 64ULL, max_tracers, out.diagnostics, source);
    if (out.diagnostics.size() == before_tracers && max_tracers > 0ULL) {
        out.options.max_tracers = static_cast<std::uint32_t>(max_tracers);
    } else if (out.diagnostics.size() == before_tracers) {
        add_diagnostic(out.diagnostics, source, "max_tracers", "expected 1..64");
    }

    std::uint64_t spans = out.options.max_spans_per_window;
    read_uint(document, "max_spans_per_window", std::numeric_limits<std::uint64_t>::max(),
              spans, out.diagnostics, source);
    out.options.max_spans_per_window = spans;

    std::uint64_t window = out.options.window_ns;
    read_uint(document, "window_ns", std::numeric_limits<std::uint64_t>::max(), window,
              out.diagnostics, source);
    out.options.window_ns = window;
}

[[nodiscard]] std::string_view env_value(const char* name) noexcept {
    const char* const value = std::getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

void apply_metrics_environment(Metrics_settings& out) {
    std::string_view value = env_value("NEXIGEN_METRICS_COLLECT_INTERVAL_NS");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed) &&
            parsed <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            out.options.collect_interval = std::chrono::nanoseconds{static_cast<std::int64_t>(parsed)};
        } else {
            add_diagnostic(out.diagnostics, logging::Settings_source::Environment,
                           "collect_interval_ns", "expected a non-negative nanosecond count");
        }
    }
    value = env_value("NEXIGEN_METRICS_CORE_MASK");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed)) {
            out.options.core_mask = parsed;
        } else {
            add_diagnostic(out.diagnostics, logging::Settings_source::Environment, "core_mask",
                           "expected a non-negative integer mask");
        }
    }
}

void apply_tracing_environment(Tracing_settings& out) {
    std::string_view value = env_value("NEXIGEN_TRACING_CORE_MASK");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed)) {
            out.options.core_mask = parsed;
        } else {
            add_diagnostic(out.diagnostics, logging::Settings_source::Environment, "core_mask",
                           "expected a non-negative integer mask");
        }
    }
    value = env_value("NEXIGEN_TRACING_MAX_TRACERS");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed) && parsed >= 1ULL && parsed <= 64ULL) {
            out.options.max_tracers = static_cast<std::uint32_t>(parsed);
        } else {
            add_diagnostic(out.diagnostics, logging::Settings_source::Environment, "max_tracers",
                           "expected an integer in 1..64");
        }
    }
    value = env_value("NEXIGEN_TRACING_MAX_SPANS_PER_WINDOW");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed)) {
            out.options.max_spans_per_window = parsed;
        } else {
            add_diagnostic(out.diagnostics, logging::Settings_source::Environment,
                           "max_spans_per_window", "expected a non-negative integer");
        }
    }
    value = env_value("NEXIGEN_TRACING_WINDOW_NS");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed)) {
            out.options.window_ns = parsed;
        } else {
            add_diagnostic(out.diagnostics, logging::Settings_source::Environment, "window_ns",
                           "expected a non-negative integer");
        }
    }
}

} // namespace

core::Status load_metrics_settings(const Settings_paths& paths,
                                   Metrics_settings& out) noexcept {
    out = Metrics_settings{};
    bool invalid = false;
    bool system_error = false;
    std::string text;

    File_read read = read_settings_file(paths.global_file, text);
    if (read == File_read::Ok) {
        out.global_loaded = true;
        const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out.diagnostics, logging::Settings_source::Global, "",
                           "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            merge_metrics_document(document, logging::Settings_source::Global, out);
            invalid = invalid || out.diagnostics.size() != before;
        }
    } else if (read == File_read::Failed) {
        system_error = true;
        add_diagnostic(out.diagnostics, logging::Settings_source::Global, "",
                       "file exists but could not be read");
    }

    text.clear();
    read = read_settings_file(paths.local_file, text);
    if (read == File_read::Ok) {
        out.local_loaded = true;
        const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out.diagnostics, logging::Settings_source::Local, "",
                           "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            merge_metrics_document(document, logging::Settings_source::Local, out);
            invalid = invalid || out.diagnostics.size() != before;
        }
    } else if (read == File_read::Failed) {
        system_error = true;
        add_diagnostic(out.diagnostics, logging::Settings_source::Local, "",
                       "file exists but could not be read");
    }

    const std::size_t before_env = out.diagnostics.size();
    apply_metrics_environment(out);
    invalid = invalid || out.diagnostics.size() != before_env;

    if (system_error) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (invalid) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

core::Status load_tracing_settings(const Settings_paths& paths,
                                   Tracing_settings& out) noexcept {
    out = Tracing_settings{};
    bool invalid = false;
    bool system_error = false;
    std::string text;

    File_read read = read_settings_file(paths.global_file, text);
    if (read == File_read::Ok) {
        out.global_loaded = true;
        const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out.diagnostics, logging::Settings_source::Global, "",
                           "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            merge_tracing_document(document, logging::Settings_source::Global, out);
            invalid = invalid || out.diagnostics.size() != before;
        }
    } else if (read == File_read::Failed) {
        system_error = true;
        add_diagnostic(out.diagnostics, logging::Settings_source::Global, "",
                       "file exists but could not be read");
    }

    text.clear();
    read = read_settings_file(paths.local_file, text);
    if (read == File_read::Ok) {
        out.local_loaded = true;
        const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out.diagnostics, logging::Settings_source::Local, "",
                           "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            merge_tracing_document(document, logging::Settings_source::Local, out);
            invalid = invalid || out.diagnostics.size() != before;
        }
    } else if (read == File_read::Failed) {
        system_error = true;
        add_diagnostic(out.diagnostics, logging::Settings_source::Local, "",
                       "file exists but could not be read");
    }

    const std::size_t before_env = out.diagnostics.size();
    apply_tracing_environment(out);
    invalid = invalid || out.diagnostics.size() != before_env;

    if (system_error) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (invalid) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

core::Status load_app_settings(const App_settings_paths& paths,
                               App_settings& out) noexcept {
    out = App_settings{};
    const core::Status logging_status =
        logging::load_logging_settings(paths.logging, out.logging);
    const core::Status metrics_status = load_metrics_settings(paths.metrics, out.metrics);
    const core::Status tracing_status = load_tracing_settings(paths.tracing, out.tracing);
    const core::Status chrono_status = chrono::load_chrono_settings(paths.chrono, out.chrono);

    if (!logging_status || !metrics_status || !tracing_status || !chrono_status) {
        if ((!logging_status && logging_status.error() == core::ErrorCode::SystemError) ||
            (!metrics_status && metrics_status.error() == core::ErrorCode::SystemError) ||
            (!tracing_status && tracing_status.error() == core::ErrorCode::SystemError) ||
            (!chrono_status && chrono_status.error() == core::ErrorCode::SystemError)) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

} // namespace nexigen::app
