// nexigen::metrics — cold-path implementation of the global/local settings
// loader (change refactor-settings-into-subsystem-modules; spec
// metrics/metrics-settings; refactor-settings-table-parsing declares the
// fields in tables over the shared core::settings_io drivers).
//
// Everything in this unit is initialization-time code: file I/O, JSON,
// std::string and diagnostics are explicitly allowed here and never reach a
// producer or collection hot path. nlohmann/json headers trigger
// -Wnull-dereference under -O3 (pre-existing upstream issue), so the pragma
// guards this cold unit only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>

#include <nexigen/core/settings_io.hpp>
#include <nexigen/metrics/json_file_metric_sink.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/metrics_settings.hpp>
#pragma GCC diagnostic pop

#include <cerrno>
#include <charconv>
#include <chrono>
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

namespace nexigen::metrics {
namespace {

namespace io = nexigen::core::settings_io;
using nexigen::core::settings_io::add_diagnostic;
using nexigen::core::settings_io::FileRead;
using nexigen::core::settings_io::parse_u64;
using nexigen::core::settings_io::read_text_file;

} // namespace

std::string Settings::to_string() const {
    return nexigen::core::settings_io::format_settings_summary(
        global_loaded, local_loaded, nexigen::metrics::to_string(options),
        nexigen::metrics::to_string(sink), diagnostics);
}

namespace {

// --- field hooks (refactor-settings-table-parsing) --------------------------
// One line per field: the key arrives from the table, the MEANING lives once
// in core::settings_io. Table order = the exact sequence
// merge_metrics_document used to read fields.

void read_collect_interval(const char* key, const nlohmann::json& document,
                           core::SettingsSource source,
                           std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_duration_ns(document, key, source, diagnostics, out.options.collect_interval);
}

void read_core_mask(const char* key, const nlohmann::json& document, core::SettingsSource source,
                    std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_core_mask_into(document, key, source, diagnostics, out.options.worker_thread);
}

void read_worker_thread(const char* key, const nlohmann::json& document,
                        core::SettingsSource source,
                        std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_worker_spec(document, key, source, diagnostics, out.options.worker_thread);
}

void read_max_meters(const char* key, const nlohmann::json& document, core::SettingsSource source,
                     std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_bounded_u32(document, key, source, diagnostics, out.options.max_meters);
}

void read_max_counters(const char* key, const nlohmann::json& document, core::SettingsSource source,
                       std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_bounded_u32(document, key, source, diagnostics, out.options.max_counters);
}

void read_max_gauges(const char* key, const nlohmann::json& document, core::SettingsSource source,
                     std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_bounded_u32(document, key, source, diagnostics, out.options.max_gauges);
}

void read_max_statistics(const char* key, const nlohmann::json& document,
                         core::SettingsSource source,
                         std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_bounded_u32(document, key, source, diagnostics, out.options.max_statistics);
}

void read_max_explicit_histograms(const char* key, const nlohmann::json& document,
                                  core::SettingsSource source,
                                  std::vector<core::SettingsDiagnostic>& diagnostics,
                                  Settings& out) {
    io::read_bounded_u32(document, key, source, diagnostics, out.options.max_explicit_histograms);
}

void read_max_exponential_histograms(const char* key, const nlohmann::json& document,
                                     core::SettingsSource source,
                                     std::vector<core::SettingsDiagnostic>& diagnostics,
                                     Settings& out) {
    io::read_bounded_u32(document, key, source, diagnostics,
                         out.options.max_exponential_histograms);
}

void read_sink_path(const char* key, const nlohmann::json& sink, core::SettingsSource source,
                    std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_sink_string(sink, key, out.sink.path, diagnostics, source);
}

void read_sink_append(const char* key, const nlohmann::json& sink, core::SettingsSource source,
                      std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_sink_bool(sink, key, out.sink.append, diagnostics, source);
}

constexpr io::ReadField<Settings> document_fields[] = {
    {"collect_interval_ns", &read_collect_interval},
    {"core_mask", &read_core_mask},
    {"worker_thread", &read_worker_thread},
    {"max_meters", &read_max_meters},
    {"max_counters", &read_max_counters},
    {"max_gauges", &read_max_gauges},
    {"max_statistics", &read_max_statistics},
    {"max_explicit_histograms", &read_max_explicit_histograms},
    {"max_exponential_histograms", &read_max_exponential_histograms},
};

constexpr io::SinkReadField<Settings> sink_fields[] = {
    {"path", &read_sink_path},
    {"append", &read_sink_append},
};

// --- write hooks: the to_json slots, same table pattern (design D4). -------
void write_global_loaded(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.global_loaded;
}

void write_local_loaded(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.local_loaded;
}

void write_collect_interval(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.options.collect_interval.count();
}

void write_worker_thread(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.options.worker_thread;
}

void write_max_meters(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.options.max_meters;
}

void write_max_counters(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.options.max_counters;
}

void write_max_gauges(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.options.max_gauges;
}

void write_max_statistics(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = settings.options.max_statistics;
}

void write_max_explicit_histograms(nlohmann::json& object, const char* key,
                                   const Settings& settings) {
    object[key] = settings.options.max_explicit_histograms;
}

void write_max_exponential_histograms(nlohmann::json& object, const char* key,
                                      const Settings& settings) {
    object[key] = settings.options.max_exponential_histograms;
}

void write_sink(nlohmann::json& object, const char* key, const Settings& settings) {
    object[key] = nlohmann::json{{"path", settings.sink.path}, {"append", settings.sink.append}};
}

constexpr io::WriteField<Settings> json_fields[] = {
    {"global_loaded", &write_global_loaded},
    {"local_loaded", &write_local_loaded},
    {"collect_interval_ns", &write_collect_interval},
    {"worker_thread", &write_worker_thread},
    {"max_meters", &write_max_meters},
    {"max_counters", &write_max_counters},
    {"max_gauges", &write_max_gauges},
    {"max_statistics", &write_max_statistics},
    {"max_explicit_histograms", &write_max_explicit_histograms},
    {"max_exponential_histograms", &write_max_exponential_histograms},
    {"sink", &write_sink},
};

void merge_metrics_document(const nlohmann::json& document, core::SettingsSource source,
                            Settings& out) {
    if (!document.is_object()) {
        add_diagnostic(out.diagnostics, source, "", "document is not a JSON object");
        return;
    }
    io::read_fields(document, source, document_fields, out);
    io::read_sink_fields(document, source, sink_fields, out);
}

void apply_env_interval(const char* env_name, const char* field,
                        std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_env_duration_ns(env_name, field, diagnostics, out.options.collect_interval);
}

void apply_env_mask(const char* env_name, const char* field,
                    std::vector<core::SettingsDiagnostic>& diagnostics, Settings& out) {
    io::read_env_core_mask(env_name, field, diagnostics, out.options.worker_thread);
}

constexpr io::EnvField<Settings> env_fields[] = {
    {"NEXIGEN_METRICS_COLLECT_INTERVAL_NS", "collect_interval_ns", &apply_env_interval},
    {"NEXIGEN_METRICS_CORE_MASK", "core_mask", &apply_env_mask},
};

void apply_metrics_environment(Settings& out) {
    io::apply_env_fields(env_fields, out);
}

} // namespace

void to_json(nlohmann::json& j, const Settings& settings) {
    nlohmann::json object = nlohmann::json::object();
    io::write_fields(object, json_fields, settings);
    io::write_diagnostics(object, settings.diagnostics);
    j = std::move(object);
}

core::Status load_metrics_settings(const core::SettingsPaths& paths, Settings& out) noexcept {
    return core::settings_io::load_settings_document(paths, out, &merge_metrics_document,
                                                     &apply_metrics_environment);
}

} // namespace nexigen::metrics
