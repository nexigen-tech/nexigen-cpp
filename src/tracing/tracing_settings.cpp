// nexigen::tracing — cold-path implementation of the global/local settings
// loader (change refactor-settings-into-subsystem-modules; spec
// tracing/tracing-settings; refactor-settings-table-parsing declares the
// fields in tables over the shared core::settings_io drivers).
//
// Everything in this unit is initialization-time code: file I/O, JSON,
// std::string and diagnostics are explicitly allowed here and never reach a
// span or sink hot path. nlohmann/json headers trigger -Wnull-dereference
// under -O3 (pre-existing upstream issue), so the pragma guards this cold unit
// only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>

#include <nexigen/core/settings_io.hpp>
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

namespace io = nexigen::core::settings_io;
using nexigen::core::settings_io::add_diagnostic;
using nexigen::core::settings_io::FileRead;
using nexigen::core::settings_io::parse_u64;
using nexigen::core::settings_io::read_text_file;

} // namespace

std::string Settings::to_string() const {
    return nexigen::core::settings_io::format_settings_summary(
        global_loaded, local_loaded, nexigen::tracing::to_string(options),
        nexigen::tracing::to_string(sink), diagnostics);
}

namespace {

// --- field hooks (refactor-settings-table-parsing) --------------------------
// One line per field (max_tracers keeps its bespoke 1..64 rule); the key
// arrives from the table, shared meanings live in core::settings_io. Table
// order = the exact sequence merge_tracing_document used to read fields.

void read_core_mask(const char* key, const nlohmann::json& document, core::SettingsSource source,
                    std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_core_mask_into(document, key, source, diagnostics, out.options.worker_thread);
}

void read_worker_thread(const char* key, const nlohmann::json& document,
                        core::SettingsSource source,
                        std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_worker_spec(document, key, source, diagnostics, out.options.worker_thread);
}

void read_max_tracers(const char* key, const nlohmann::json& document, core::SettingsSource source,
                      std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    std::uint64_t value = out.options.max_tracers;
    const std::size_t before = diagnostics.size();
    io::read_uint(document, key, 64ULL, value, diagnostics, source);
    if (diagnostics.size() == before && value > 0ULL) {
        out.options.max_tracers = static_cast<std::uint32_t>(value);
    } else if (diagnostics.size() == before) {
        add_diagnostic(diagnostics, source, key, "expected 1..64");
    }
}

void read_max_spans_per_window(const char* key, const nlohmann::json& document,
                               core::SettingsSource source,
                               std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_staged_u64(document, key, std::numeric_limits<std::uint64_t>::max(), source,
                        diagnostics, out.options.max_spans_per_window);
}

void read_window_ns(const char* key, const nlohmann::json& document, core::SettingsSource source,
                    std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_staged_u64(document, key, std::numeric_limits<std::uint64_t>::max(), source,
                        diagnostics, out.options.window_ns);
}

void read_poll_interval(const char* key, const nlohmann::json& document,
                        core::SettingsSource source,
                        std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_staged_u32(document, key, std::numeric_limits<std::uint32_t>::max(), source,
                        diagnostics, out.options.worker_poll_interval_us);
}

void read_sink_path(const char* key, const nlohmann::json& sink, core::SettingsSource source,
                    std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_sink_string(sink, key, out.sink.path, diagnostics, source);
}

void read_sink_rotation(const char* key, const nlohmann::json& sink, core::SettingsSource source,
                        std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_sink_enum(sink, key, source, diagnostics, out.sink.rotation,
                       "expected None, Hourly or Daily");
}

void read_sink_max_spans(const char* key, const nlohmann::json& sink, core::SettingsSource source,
                         std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_sink_uint(sink, key, out.sink.max_spans_per_window, diagnostics, source);
}

void read_sink_window_ns(const char* key, const nlohmann::json& sink, core::SettingsSource source,
                         std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_sink_uint(sink, key, out.sink.window_ns, diagnostics, source);
}

constexpr io::ReadField<Settings> document_fields[] = {
    {"core_mask", &read_core_mask},     {"worker_thread", &read_worker_thread},
    {"max_tracers", &read_max_tracers}, {"max_spans_per_window", &read_max_spans_per_window},
    {"window_ns", &read_window_ns},     {"worker_poll_interval_us", &read_poll_interval},
};

constexpr io::SinkReadField<Settings> sink_fields[] = {
    {"path", &read_sink_path},
    {"rotation", &read_sink_rotation},
    {"max_spans_per_window", &read_sink_max_spans},
    {"window_ns", &read_sink_window_ns},
};

// --- write hooks: the to_json slots, same table pattern (design D4). -------
void write_global_loaded(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.global_loaded;
}

void write_local_loaded(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.local_loaded;
}

void write_worker_thread(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.options.worker_thread;
}

void write_max_tracers(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.options.max_tracers;
}

void write_max_spans(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.options.max_spans_per_window;
}

void write_window_ns(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.options.window_ns;
}

void write_poll_interval(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.options.worker_poll_interval_us;
}

void write_sink(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = nlohmann::json{{"path", settings.sink.path},
                                 {"rotation", std::string(to_string(settings.sink.rotation))},
                                 {"max_spans_per_window", settings.sink.max_spans_per_window},
                                 {"window_ns", settings.sink.window_ns}};
}

constexpr io::WriteField<Settings> json_fields[] = {
    {"global_loaded", &write_global_loaded},           {"local_loaded", &write_local_loaded},
    {"worker_thread", &write_worker_thread},           {"max_tracers", &write_max_tracers},
    {"max_spans_per_window", &write_max_spans},        {"window_ns", &write_window_ns},
    {"worker_poll_interval_us", &write_poll_interval}, {"sink", &write_sink},
};

void merge_tracing_document(const nlohmann::json& document, core::SettingsSource source,
                            Settings& out) {
    if (!document.is_object()) {
        add_diagnostic(out.diagnostics, source, "", "document is not a JSON object");
        return;
    }
    io::read_fields(document, source, document_fields, out);
    io::read_sink_fields(document, source, sink_fields, out);
}

void apply_env_mask(const char* env_name, const char* field,
                    std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_env_core_mask(env_name, field, diagnostics, out.options.worker_thread);
}

void apply_env_max_tracers(const char* env_name, const char* field,
                           std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_env_range_u32(env_name, field, 1ULL, 64ULL, diagnostics, out.options.max_tracers,
                           "expected an integer in 1..64");
}

void apply_env_max_spans(const char* env_name, const char* field,
                         std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_env_plain_u64(env_name, field, diagnostics, out.options.max_spans_per_window);
}

void apply_env_window_ns(const char* env_name, const char* field,
                         std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_env_plain_u64(env_name, field, diagnostics, out.options.window_ns);
}

constexpr io::EnvField<Settings> env_fields[] = {
    {"NEXIGEN_TRACING_CORE_MASK", "core_mask", &apply_env_mask},
    {"NEXIGEN_TRACING_MAX_TRACERS", "max_tracers", &apply_env_max_tracers},
    {"NEXIGEN_TRACING_MAX_SPANS_PER_WINDOW", "max_spans_per_window", &apply_env_max_spans},
    {"NEXIGEN_TRACING_WINDOW_NS", "window_ns", &apply_env_window_ns},
};

void apply_tracing_environment(Settings& out) {
    io::apply_env_fields(env_fields, out);
}

} // namespace

void to_json(nlohmann::json& j, const Settings& settings) {
    nlohmann::json object = nlohmann::json::object();
    io::write_fields(object, json_fields, settings);
    io::write_diagnostics(object, settings.diagnostics);
    j = std::move(object);
}

core::Status load_tracing_settings(const core::SettingsPaths& paths, Settings& out) noexcept {
    return core::settings_io::load_settings_document(paths, out, &merge_tracing_document,
                                                     &apply_tracing_environment);
}

} // namespace nexigen::tracing
