// nexigen::chrono — cold-path implementation of the global/local settings
// loader (change add-chrono-clock-service; spec chrono/chrono-settings).
//
// Everything in this unit is initialization-time code: file I/O, JSON,
// std::string and diagnostics are explicitly allowed here and never reach a
// snapshot or producer path. nlohmann/json headers trigger -Wnull-dereference
// under -O3 (pre-existing upstream issue), so the pragma guards this cold unit
// only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>

#include <nexigen/chrono/chrono_settings.hpp>
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

namespace nexigen::chrono {
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

void read_sink_bool(const nlohmann::json& sink, const char* key, bool& target,
                    std::vector<core::SettingsDiagnostic>& diagnostics,
                    core::SettingsSource source) {
    const auto it = sink.find(key);
    if (it == sink.end()) {
        return;
    }
    if (!it->is_boolean()) {
        add_diagnostic(diagnostics, source, sink_field(key), "expected a boolean");
        return;
    }
    target = it->get<bool>();
}

} // namespace

std::string Settings::to_string() const {
    std::string out = "Settings(global_loaded=" +
                      std::string(global_loaded ? "true" : "false") +
                      ", local_loaded=" + std::string(local_loaded ? "true" : "false") +
                      ", options=" + nexigen::chrono::to_string(options) +
                      ", sink=" + nexigen::chrono::to_string(sink) +
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
    const ChronoProviderConfig& options = settings.options;
    nlohmann::json diagnostics = nlohmann::json::array();
    for (const core::SettingsDiagnostic& diag : settings.diagnostics) {
        nlohmann::json entry;
        to_json(entry, diag);
        diagnostics.push_back(std::move(entry));
    }
    j = nlohmann::json{{"global_loaded", settings.global_loaded},
                       {"local_loaded", settings.local_loaded},
                       {"collect_interval_ns", options.collect_interval.count()},
                       {"core_mask", options.core_mask},
                       {"sink", nlohmann::json{{"path", settings.sink.path},
                                               {"append", settings.sink.append}}},
                       {"diagnostics", std::move(diagnostics)}};
}

namespace {

void merge_document(const nlohmann::json& document, core::SettingsSource source,
                    Settings& out) {
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
        out.options.collect_interval =
            std::chrono::nanoseconds{static_cast<std::int64_t>(interval)};
    }
    std::uint64_t core_mask = out.options.core_mask;
    read_uint(document, "core_mask", std::numeric_limits<std::uint64_t>::max(), core_mask,
              out.diagnostics, source);
    out.options.core_mask = core_mask;

    const nlohmann::json* const sink = find_sink(document, out.diagnostics, source);
    if (sink != nullptr) {
        read_sink_string(*sink, "path", out.sink.path, out.diagnostics, source);
        read_sink_bool(*sink, "append", out.sink.append, out.diagnostics, source);
    }
}

[[nodiscard]] std::string_view env_value(const char* name) noexcept {
    const char* const value = std::getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

void apply_environment(Settings& out) {
    std::string_view value = env_value("NEXIGEN_CHRONO_COLLECT_INTERVAL_NS");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed) &&
            parsed <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            out.options.collect_interval =
                std::chrono::nanoseconds{static_cast<std::int64_t>(parsed)};
        } else {
            add_diagnostic(out.diagnostics, core::SettingsSource::Environment,
                           "collect_interval_ns", "expected a non-negative nanosecond count");
        }
    }
    value = env_value("NEXIGEN_CHRONO_CORE_MASK");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed)) {
            out.options.core_mask = parsed;
        } else {
            add_diagnostic(out.diagnostics, core::SettingsSource::Environment, "core_mask",
                           "expected a non-negative integer mask");
        }
    }
}

} // namespace

core::Status load_chrono_settings(const core::SettingsPaths& paths,
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
            merge_document(document, core::SettingsSource::Global, out);
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
            merge_document(document, core::SettingsSource::Local, out);
            invalid = invalid || out.diagnostics.size() != before;
        }
    } else if (read == FileRead::Failed) {
        system_error = true;
        add_diagnostic(out.diagnostics, core::SettingsSource::Local, "",
                       "file exists but could not be read");
    }

    const std::size_t before_env = out.diagnostics.size();
    apply_environment(out);
    invalid = invalid || out.diagnostics.size() != before_env;

    if (system_error) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (invalid) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

core::Status apply_to(const Settings& settings, ChronoProviderConfig& out) noexcept {
    out = settings.options;
    return {};
}

} // namespace nexigen::chrono
