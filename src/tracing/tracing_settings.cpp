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

// Shared settings-io helpers (change fix-p2-observability-and-writers):
// one implementation instead of three copies. Imports stay inside the TU's
// anonymous namespace — no unqualified leakage. (dedup-sink-plumbing) the
// per-key sink reads moved into the shared vocabulary as well.
using nexigen::core::settings_io::add_diagnostic;
using nexigen::core::settings_io::FileRead;
using nexigen::core::settings_io::find_sink;
using nexigen::core::settings_io::parse_u64;
using nexigen::core::settings_io::read_sink_string;
using nexigen::core::settings_io::read_sink_uint;
using nexigen::core::settings_io::read_text_file;
using nexigen::core::settings_io::read_uint;
using nexigen::core::settings_io::unsigned_value;

} // namespace

std::string Settings::to_string() const {
    std::string out = "Settings(global_loaded=" + std::string(global_loaded ? "true" : "false") +
                      ", local_loaded=" + std::string(local_loaded ? "true" : "false") +
                      ", options=" + nexigen::tracing::to_string(options) +
                      ", sink=" + nexigen::tracing::to_string(sink) + ", diagnostics=[";
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
    j = nlohmann::json{
        {"global_loaded", settings.global_loaded},
        {"local_loaded", settings.local_loaded},
        {"worker_thread", options.worker_thread},
        {"max_tracers", options.max_tracers},
        {"max_spans_per_window", options.max_spans_per_window},
        {"window_ns", options.window_ns},
        {"worker_poll_interval_us", options.worker_poll_interval_us},
        {"sink", nlohmann::json{{"path", settings.sink.path},
                                {"rotation", std::string(to_string(settings.sink.rotation))},
                                {"max_spans_per_window", settings.sink.max_spans_per_window},
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
            const auto* const name = rotation_it->get_ptr<const nlohmann::json::string_t*>();
            RotationPolicy parsed = out.sink.rotation;
            if (name != nullptr && from_string(std::string_view(*name), parsed)) {
                out.sink.rotation = parsed;
            } else {
                add_diagnostic(out.diagnostics, source, "sink.rotation",
                               "expected None, Hourly or Daily");
            }
        }
    }
    read_sink_uint(*sink, "max_spans_per_window", out.sink.max_spans_per_window, out.diagnostics,
                   source);
    read_sink_uint(*sink, "window_ns", out.sink.window_ns, out.diagnostics, source);
}

void merge_tracing_document(const nlohmann::json& document, core::SettingsSource source,
                            Settings& out) {
    if (!document.is_object()) {
        add_diagnostic(out.diagnostics, source, "", "document is not a JSON object");
        return;
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

    std::uint64_t max_tracers = out.options.max_tracers;
    const std::size_t before_tracers = out.diagnostics.size();
    read_uint(document, "max_tracers", 64ULL, max_tracers, out.diagnostics, source);
    if (out.diagnostics.size() == before_tracers && max_tracers > 0ULL) {
        out.options.max_tracers = static_cast<std::uint32_t>(max_tracers);
    } else if (out.diagnostics.size() == before_tracers) {
        add_diagnostic(out.diagnostics, source, "max_tracers", "expected 1..64");
    }

    std::uint64_t spans = out.options.max_spans_per_window;
    read_uint(document, "max_spans_per_window", std::numeric_limits<std::uint64_t>::max(), spans,
              out.diagnostics, source);
    out.options.max_spans_per_window = spans;

    std::uint64_t window = out.options.window_ns;
    read_uint(document, "window_ns", std::numeric_limits<std::uint64_t>::max(), window,
              out.diagnostics, source);
    out.options.window_ns = window;

    std::uint64_t poll = out.options.worker_poll_interval_us;
    read_uint(document, "worker_poll_interval_us", std::numeric_limits<std::uint32_t>::max(), poll,
              out.diagnostics, source);
    out.options.worker_poll_interval_us = static_cast<std::uint32_t>(poll);
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
            out.options.worker_thread.set_cpus(core::CpuSet::from_core_mask(parsed));
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

core::Status load_tracing_settings(const core::SettingsPaths& paths, Settings& out) noexcept {
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
            merge_tracing_document(document, core::SettingsSource::Global, out);
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
