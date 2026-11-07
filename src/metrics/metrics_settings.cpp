// nexigen::metrics — cold-path implementation of the global/local settings
// loader (change refactor-settings-into-subsystem-modules; spec
// metrics/metrics-settings).
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

// Shared settings-io helpers (change fix-p2-observability-and-writers):
// one implementation instead of three copies; imports stay inside the TU's
// anonymous namespace. (dedup-sink-plumbing) the per-key sink reads moved
// into the shared vocabulary as well.
using nexigen::core::settings_io::add_diagnostic;
using nexigen::core::settings_io::FileRead;
using nexigen::core::settings_io::find_sink;
using nexigen::core::settings_io::parse_u64;
using nexigen::core::settings_io::read_sink_bool;
using nexigen::core::settings_io::read_sink_string;
using nexigen::core::settings_io::read_text_file;
using nexigen::core::settings_io::read_uint;
using nexigen::core::settings_io::unsigned_value;

} // namespace

std::string Settings::to_string() const {
    std::string out = "Settings(global_loaded=" + std::string(global_loaded ? "true" : "false") +
                      ", local_loaded=" + std::string(local_loaded ? "true" : "false") +
                      ", options=" + nexigen::metrics::to_string(options) +
                      ", sink=" + nexigen::metrics::to_string(sink) + ", diagnostics=[";
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
    const MetricProviderConfig& options = settings.options;
    nlohmann::json diagnostics = nlohmann::json::array();
    for (const core::SettingsDiagnostic& diag : settings.diagnostics) {
        nlohmann::json entry;
        to_json(entry, diag);
        diagnostics.push_back(std::move(entry));
    }
    j = nlohmann::json{
        {"global_loaded", settings.global_loaded},
        {"local_loaded", settings.local_loaded},
        {"collect_interval_ns", options.collect_interval.count()},
        {"worker_thread", options.worker_thread},
        {"max_meters", options.max_meters},
        {"max_counters", options.max_counters},
        {"max_gauges", options.max_gauges},
        {"max_statistics", options.max_statistics},
        {"max_explicit_histograms", options.max_explicit_histograms},
        {"max_exponential_histograms", options.max_exponential_histograms},
        {"sink", nlohmann::json{{"path", settings.sink.path}, {"append", settings.sink.append}}},
        {"diagnostics", std::move(diagnostics)}};
}

namespace {

void merge_metrics_sink(const nlohmann::json& document, core::SettingsSource source,
                        Settings& out) {
    const nlohmann::json* const sink = find_sink(document, out.diagnostics, source);
    if (sink == nullptr) {
        return;
    }
    read_sink_string(*sink, "path", out.sink.path, out.diagnostics, source);
    read_sink_bool(*sink, "append", out.sink.append, out.diagnostics, source);
}

void merge_metrics_document(const nlohmann::json& document, core::SettingsSource source,
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
    std::uint64_t core_mask = 0;
    read_uint(document, "core_mask", std::numeric_limits<std::uint64_t>::max(), core_mask,
              out.diagnostics, source);
    if (document.contains("core_mask")) {
        out.options.worker_thread.set_cpus(core::CpuSet::from_core_mask(core_mask));
    }
    const auto worker = document.find("worker_thread");
    if (worker != document.end()) {
        core::ThreadSpec parsed = out.options.worker_thread;
        if (core::parse_thread_spec_json(*worker, parsed)) {
            out.options.worker_thread = parsed;
        } else {
            add_diagnostic(out.diagnostics, source, "worker_thread",
                           "expected valid name/cpus/policy/priority/mode fields");
        }
    }

    const auto read_u32 = [&](const char* key, std::uint32_t& target) {
        std::uint64_t value = target;
        const std::size_t diagnostics_before = out.diagnostics.size();
        read_uint(document, key, std::numeric_limits<std::uint32_t>::max(), value, out.diagnostics,
                  source);
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
    merge_metrics_sink(document, source, out);
}

[[nodiscard]] std::string_view env_value(const char* name) noexcept {
    const char* const value = std::getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

void apply_metrics_environment(Settings& out) {
    std::string_view value = env_value("NEXIGEN_METRICS_COLLECT_INTERVAL_NS");
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
    value = env_value("NEXIGEN_METRICS_CORE_MASK");
    if (!value.empty()) {
        std::uint64_t parsed = 0;
        if (parse_u64(value, parsed)) {
            out.options.worker_thread.set_cpus(core::CpuSet::from_core_mask(parsed));
        } else {
            add_diagnostic(out.diagnostics, core::SettingsSource::Environment, "core_mask",
                           "expected a non-negative integer mask");
        }
    }
}

} // namespace

core::Status load_metrics_settings(const core::SettingsPaths& paths, Settings& out) noexcept {
    out = Settings{};
    bool invalid = false;
    bool system_error = false;
    std::string text;

    FileRead read = read_text_file(paths.global_file, text);
    if (read == FileRead::Ok) {
        out.global_loaded = true;
        const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out.diagnostics, core::SettingsSource::Global, "", "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            merge_metrics_document(document, core::SettingsSource::Global, out);
            invalid = invalid || out.diagnostics.size() != before;
        }
    } else if (read == FileRead::Failed) {
        system_error = true;
        add_diagnostic(out.diagnostics, core::SettingsSource::Global, "",
                       "file exists but could not be read");
    }

    text.clear();
    read = read_text_file(paths.local_file, text);
    if (read == FileRead::Ok) {
        out.local_loaded = true;
        const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            invalid = true;
            add_diagnostic(out.diagnostics, core::SettingsSource::Local, "", "malformed JSON");
        } else {
            const std::size_t before = out.diagnostics.size();
            merge_metrics_document(document, core::SettingsSource::Local, out);
            invalid = invalid || out.diagnostics.size() != before;
        }
    } else if (read == FileRead::Failed) {
        system_error = true;
        add_diagnostic(out.diagnostics, core::SettingsSource::Local, "",
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

} // namespace nexigen::metrics
