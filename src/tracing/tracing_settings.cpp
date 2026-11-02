// nexigen::tracing — cold-path implementation of the global/local settings
// loader (change refactor-settings-into-subsystem-modules; spec
// tracing/tracing-settings).
//
// Everything in this unit is initialization-time code: file I/O, JSON,
// std::string and diagnostics are explicitly allowed here and never reach a
// span or sink hot path. nlohmann/json headers trigger -Wnull-dereference
// under -O3 (pre-existing upstream issue), so the pragma guards this cold
// unit only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>

#include <nexigen/tracing/json_file_trace_sink.hpp>
#include <nexigen/tracing/tracer_provider.hpp>
#include <nexigen/tracing/tracing_settings.hpp>
#pragma GCC diagnostic pop

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nexigen::tracing {
namespace {

enum class FileRead : std::uint8_t { Absent, Ok, Failed };

[[nodiscard]] FileRead read_settings_file(std::string_view path,
                                           std::string& out) noexcept {
    if (path.empty()) {
        return FileRead::Absent;
    }
    const std::string path_buf{path};
    const int fd = ::open(path_buf.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return errno == ENOENT ? FileRead::Absent : FileRead::Failed;
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size < 0) {
        static_cast<void>(::close(fd));
        return FileRead::Failed;
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
            return FileRead::Failed;
        }
    }
    out.resize(offset);
    static_cast<void>(::close(fd));
    return FileRead::Ok;
}

void add_diagnostic(std::vector<core::SettingsDiagnostic>& diagnostics,
                    core::SettingsSource source, std::string field,
                    std::string message) {
    diagnostics.push_back(
        core::SettingsDiagnostic{source, std::move(field), std::move(message)});
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
               std::uint64_t& target, std::vector<core::SettingsDiagnostic>& diagnostics,
               core::SettingsSource source) {
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

[[nodiscard]] std::string sink_field(const char* key) {
    return std::string("sink.") + key;
}

void read_sink_string(const nlohmann::json& sink, const char* key, std::string& target,
                      std::vector<core::SettingsDiagnostic>& diagnostics,
                      core::SettingsSource source) {
    const auto it = sink.find(key);
    if (it == sink.end()) {
        return;
    }
    if (!it->is_string()) {
        add_diagnostic(diagnostics, source, sink_field(key), "expected a string");
        return;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text != nullptr) {
        target = std::string(*text);
    }
}

void read_sink_uint(const nlohmann::json& sink, const char* key, std::uint64_t& target,
                    std::vector<core::SettingsDiagnostic>& diagnostics,
                    core::SettingsSource source) {
    const auto it = sink.find(key);
    if (it == sink.end()) {
        return;
    }
    std::uint64_t value = 0;
    if (unsigned_value(*it, value)) {
        target = value;
    } else {
        add_diagnostic(diagnostics, source, sink_field(key),
                       "expected a non-negative integer");
    }
}

/// Locate the optional sink object. Returns nullptr when absent or invalid;
/// an invalid value is reported once and leaves the previous sink intact.
[[nodiscard]] const nlohmann::json* find_sink(
    const nlohmann::json& document, std::vector<core::SettingsDiagnostic>& diagnostics,
    core::SettingsSource source) {
    const auto it = document.find("sink");
    if (it == document.end()) {
        return nullptr;
    }
    if (!it->is_object()) {
        add_diagnostic(diagnostics, source, "sink", "expected a JSON object");
        return nullptr;
    }
    return &*it;
}

} // namespace

std::string Settings::to_string() const {
    std::string out = "Settings(global_loaded=" +
                      std::string(global_loaded ? "true" : "false") +
                      ", local_loaded=" + std::string(local_loaded ? "true" : "false") +
                      ", options=" + nexigen::tracing::to_string(options) +
                      ", sink=" + nexigen::tracing::to_string(sink) +
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

void to_json(nlohmann::json& j, const Settings& settings) {
    const TracerProviderConfig& options = settings.options;
    nlohmann::json diagnostics = nlohmann::json::array();
    for (const core::SettingsDiagnostic& diag : settings.diagnostics) {
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
                       {"sink", nlohmann::json{
                                    {"path", settings.sink.path},
                                    {"rotation", std::string(to_string(settings.sink.rotation))},
                                    {"max_spans_per_window",
                                     settings.sink.max_spans_per_window},
                                    {"window_ns", settings.sink.window_ns}}},
                       {"diagnostics", std::move(diagnostics)}};
}

namespace {

void merge_tracing_sink(const nlohmann::json& document, core::SettingsSource source,
                        Settings& out) {
    const nlohmann::json* const sink = find_sink(document, out.diagnostics, source);
    if (sink == nullptr) {
        return;
    }
    read_sink_string(*sink, "path", out.sink.path, out.diagnostics, source);
    const auto rotation_it = sink->find("rotation");
    if (rotation_it != sink->end()) {
        if (!rotation_it->is_string()) {
            add_diagnostic(out.diagnostics, source, "sink.rotation", "expected a string");
        } else {
            const auto* const name =
                rotation_it->get_ptr<const nlohmann::json::string_t*>();
            RotationPolicy parsed = out.sink.rotation;
            if (name != nullptr &&
                from_string(std::string_view(*name), parsed)) {
                out.sink.rotation = parsed;
            } else {
                add_diagnostic(out.diagnostics, source, "sink.rotation",
                               "expected None, Hourly or Daily");
            }
        }
    }
    read_sink_uint(*sink, "max_spans_per_window", out.sink.max_spans_per_window,
                   out.diagnostics, source);
    read_sink_uint(*sink, "window_ns", out.sink.window_ns, out.diagnostics, source);
}

void merge_tracing_document(const nlohmann::json& document,
                            core::SettingsSource source,
                            Settings& out) {
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
    merge_tracing_sink(document, source, out);
}

[[nodiscard]] std::string_view env_value(const char* name) noexcept {
    const char* const value = std::getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

void apply_tracing_environment(Settings& out) {
    std::string_view value = env_value("NEXIGEN_TRACING_CORE_MASK");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed)) {
            out.options.core_mask = parsed;
        } else {
            add_diagnostic(out.diagnostics, core::SettingsSource::Environment, "core_mask",
                           "expected a non-negative integer mask");
        }
    }
    value = env_value("NEXIGEN_TRACING_MAX_TRACERS");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed) && parsed >= 1ULL && parsed <= 64ULL) {
            out.options.max_tracers = static_cast<std::uint32_t>(parsed);
        } else {
            add_diagnostic(out.diagnostics, core::SettingsSource::Environment, "max_tracers",
                           "expected an integer in 1..64");
        }
    }
    value = env_value("NEXIGEN_TRACING_MAX_SPANS_PER_WINDOW");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed)) {
            out.options.max_spans_per_window = parsed;
        } else {
            add_diagnostic(out.diagnostics, core::SettingsSource::Environment,
                           "max_spans_per_window", "expected a non-negative integer");
        }
    }
    value = env_value("NEXIGEN_TRACING_WINDOW_NS");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed)) {
            out.options.window_ns = parsed;
        } else {
            add_diagnostic(out.diagnostics, core::SettingsSource::Environment, "window_ns",
                           "expected a non-negative integer");
        }
    }
}

} // namespace

core::Status load_tracing_settings(const core::SettingsPaths& paths,
                                   Settings& out) noexcept {
    out = Settings{};
    bool invalid = false;
    bool system_error = false;
    std::string text;

    FileRead read = read_settings_file(paths.global_file, text);
    if (read == FileRead::Ok) {
        out.global_loaded = true;
        const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out.diagnostics, core::SettingsSource::Global, "",
                           "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            merge_tracing_document(document, core::SettingsSource::Global, out);
            invalid = invalid || out.diagnostics.size() != before;
        }
    } else if (read == FileRead::Failed) {
        system_error = true;
        add_diagnostic(out.diagnostics, core::SettingsSource::Global, "",
                       "file exists but could not be read");
    }

    text.clear();
    read = read_settings_file(paths.local_file, text);
    if (read == FileRead::Ok) {
        out.local_loaded = true;
        const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out.diagnostics, core::SettingsSource::Local, "",
                           "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            merge_tracing_document(document, core::SettingsSource::Local, out);
            invalid = invalid || out.diagnostics.size() != before;
        }
    } else if (read == FileRead::Failed) {
        system_error = true;
        add_diagnostic(out.diagnostics, core::SettingsSource::Local, "",
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

} // namespace nexigen::tracing
