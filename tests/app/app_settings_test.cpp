// Split from app_test.cpp: nexigen::app settings loaders, sink wiring and full coverage.

#include "app_test_support.hpp"

#include <type_traits>

void test_settings_loaders() {
    EnvGuard interval_env{"NEXIGEN_METRICS_COLLECT_INTERVAL_NS"};
    EnvGuard metrics_mask_env{"NEXIGEN_METRICS_CORE_MASK"};
    EnvGuard trace_mask_env{"NEXIGEN_TRACING_CORE_MASK"};
    EnvGuard tracers_env{"NEXIGEN_TRACING_MAX_TRACERS"};
    EnvGuard spans_env{"NEXIGEN_TRACING_MAX_SPANS_PER_WINDOW"};
    EnvGuard window_env{"NEXIGEN_TRACING_WINDOW_NS"};
    interval_env.clear();
    metrics_mask_env.clear();
    trace_mask_env.clear();
    tracers_env.clear();
    spans_env.clear();
    window_env.clear();

    const std::string base = unique_path("app_settings");
    const std::string metrics_global = base + "_metrics_global.json";
    const std::string metrics_local = base + "_metrics_local.json";
    const std::string tracing_global = base + "_tracing_global.json";
    const std::string tracing_local = base + "_tracing_local.json";

    write_text(metrics_global, R"({"core_mask":1,"max_counters":32})");
    write_text(metrics_local, R"({"collect_interval_ns":50000000,"max_counters":128})");
    write_text(tracing_global, R"({"core_mask":2,"max_tracers":8,)"
                               R"("max_spans_per_window":100,"window_ns":1000})");
    write_text(tracing_local, R"({"max_tracers":16})");

    metrics::Settings metrics{};
    NX_CHECK(metrics::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());
    NX_CHECK(metrics.global_loaded && metrics.local_loaded);
    NX_CHECK(metrics.options.worker_thread.cpus() == core::CpuSet::from_core_mask(1ULL));
    NX_CHECK(metrics.options.max_counters == 128U);
    NX_CHECK(metrics.options.collect_interval == std::chrono::nanoseconds{50'000'000});

    metrics_mask_env.set("4");
    NX_CHECK(metrics::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());
    NX_CHECK(metrics.options.worker_thread.cpus() == core::CpuSet::from_core_mask(4ULL));
    metrics_mask_env.clear();

    write_text(metrics_local, "{");
    NX_CHECK(!metrics::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());
    NX_CHECK(metrics.options.worker_thread.cpus() ==
             core::CpuSet::from_core_mask(1ULL)); // global survived
    NX_CHECK(!metrics.diagnostics.empty());

    write_text(metrics_local, R"({"max_counters":"many"})");
    NX_CHECK(!metrics::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());

    tracing::Settings tracing{};
    NX_CHECK(tracing::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
    NX_CHECK(tracing.options.worker_thread.cpus() == core::CpuSet::from_core_mask(2ULL));
    NX_CHECK(tracing.options.max_tracers == 16U);
    NX_CHECK(tracing.options.max_spans_per_window == 100ULL);
    NX_CHECK(tracing.options.window_ns == 1000ULL);

    tracers_env.set("32");
    NX_CHECK(tracing::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
    NX_CHECK(tracing.options.max_tracers == 32U);
    tracers_env.clear();

    write_text(tracing_local, R"({"max_tracers":100})");
    NX_CHECK(!tracing::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
    NX_CHECK(tracing.options.max_tracers == 8U); // global survived

    // Sink-vocabulary text pins (dedup-sink-plumbing): the messages of the
    // shared settings_io sink reads stay byte-identical, exercised through
    // both module loaders.
    write_text(metrics_local, R"({"sink":{"path":1,"append":"x"}})");
    {
        metrics::Settings probe{};
        NX_CHECK(
            !metrics::load_metrics_settings({metrics_global, metrics_local}, probe).has_value());
        NX_CHECK(probe.diagnostics.size() == 2U);
        NX_CHECK(probe.diagnostics[0].field == "sink.path");
        NX_CHECK(probe.diagnostics[0].message == "expected a string");
        NX_CHECK(probe.diagnostics[1].field == "sink.append");
        NX_CHECK(probe.diagnostics[1].message == "expected a boolean");
    }
    write_text(metrics_local, R"({"sink":7})");
    {
        metrics::Settings probe{};
        NX_CHECK(
            !metrics::load_metrics_settings({metrics_global, metrics_local}, probe).has_value());
        NX_CHECK(probe.diagnostics.size() == 1U);
        NX_CHECK(probe.diagnostics[0].field == "sink");
        NX_CHECK(probe.diagnostics[0].message == "expected a JSON object");
    }
    write_text(tracing_local, R"({"sink":{"path":[],"max_spans_per_window":"x"}})");
    {
        tracing::Settings probe{};
        NX_CHECK(
            !tracing::load_tracing_settings({tracing_global, tracing_local}, probe).has_value());
        NX_CHECK(probe.diagnostics.size() == 2U);
        NX_CHECK(probe.diagnostics[0].field == "sink.path");
        NX_CHECK(probe.diagnostics[0].message == "expected a string");
        NX_CHECK(probe.diagnostics[1].field == "sink.max_spans_per_window");
        NX_CHECK(probe.diagnostics[1].message == "expected a non-negative integer");
    }
    write_text(tracing_local,
               R"({"max_tracers":16})"); // restore the fixture the later probes assume

    // Unreadable supplied path (a directory) -> SystemError.
    NX_CHECK(!tracing::load_tracing_settings({"/tmp/opencode", ""}, tracing).has_value());
    NX_CHECK(tracing.diagnostics.back().source == core::SettingsSource::Global);

    // Aggregation: clean logging (empty paths) + one unreadable metrics pair.
    write_text(metrics_local, R"({"core_mask":1})");
    app::Settings app_settings{};
    app::AppSettingsPaths paths{};
    paths.metrics.global_file = "/tmp/opencode";
    NX_CHECK(!app::load_app_settings(paths, app_settings).has_value());
    NX_CHECK(app_settings.tracing.options.max_tracers == 16U); // defaults still populated

    paths.metrics.global_file = metrics_global;
    paths.metrics.local_file = "";
    write_text(tracing_local, R"({"max_tracers":16})");
    paths.tracing = {tracing_global, tracing_local};
    NX_CHECK(app::load_app_settings(paths, app_settings).has_value());

    const std::string text = app_settings.metrics.to_string();
    NX_CHECK(text.find("worker_thread=") != std::string::npos);
    NX_CHECK(std::format("{}", app_settings.metrics) == text);
    nlohmann::json json;
    metrics::to_json(json, app_settings.metrics);
    NX_CHECK(json["worker_thread"]["cpus"][0] == 0U);
    NX_CHECK(app_settings.tracing.to_string().find("max_tracers") != std::string::npos);

    // Malformed JSON and non-object documents on both subsystems.
    write_text(metrics_global, "{");
    {
        metrics::Settings metrics_invalid{};
        NX_CHECK(
            !metrics::load_metrics_settings({metrics_global, ""}, metrics_invalid).has_value());
    }
    write_text(tracing_global, "[]");
    {
        tracing::Settings tracing_invalid{};
        NX_CHECK(
            !tracing::load_tracing_settings({tracing_global, ""}, tracing_invalid).has_value());
        NX_CHECK(!tracing_invalid.diagnostics.empty());
    }
    write_text(tracing_global, "{");
    {
        tracing::Settings tracing_invalid{};
        NX_CHECK(
            !tracing::load_tracing_settings({tracing_global, ""}, tracing_invalid).has_value());
    }

    // Serialization with non-empty diagnostics (loop branches).
    {
        metrics::Settings metrics_serialized{};
        metrics_serialized.global_loaded = true;
        metrics_serialized.diagnostics.push_back({core::SettingsSource::Local, "core_mask", "bad"});
        NX_CHECK(metrics_serialized.to_string().find("bad") != std::string::npos);
        nlohmann::json serialized_json;
        metrics::to_json(serialized_json, metrics_serialized);
        NX_CHECK(serialized_json["diagnostics"].size() == 1U);
        tracing::Settings tracing_serialized{};
        tracing_serialized.local_loaded = true;
        tracing_serialized.diagnostics.push_back(
            {core::SettingsSource::Global, "max_tracers", "bad"});
        NX_CHECK(tracing_serialized.to_string().find("bad") != std::string::npos);
        tracing::to_json(serialized_json, tracing_serialized);
        NX_CHECK(serialized_json["diagnostics"].size() == 1U);
    }

    // Malformed local documents on both subsystems.
    {
        write_text(metrics_global, R"({"core_mask":5})");
        write_text(metrics_local, "{");
        metrics::Settings metrics_local_bad{};
        NX_CHECK(!metrics::load_metrics_settings({metrics_global, metrics_local}, metrics_local_bad)
                      .has_value());
        write_text(tracing_global, R"({"max_tracers":2})");
        write_text(tracing_local, "{");
        tracing::Settings tracing_local_bad{};
        NX_CHECK(!tracing::load_tracing_settings({tracing_global, tracing_local}, tracing_local_bad)
                      .has_value());
    }

    // Aggregate InvalidArgument when subsystems load but contain diagnostics.
    {
        write_text(metrics_local, R"({"core_mask":"x"})");
        write_text(tracing_local, R"({"max_tracers":0})");
        app::AppSettingsPaths aggregate_paths{};
        aggregate_paths.metrics = {metrics_global, metrics_local};
        aggregate_paths.tracing = {tracing_global, tracing_local};
        app::Settings out{};
        const core::Status status = app::load_app_settings(aggregate_paths, out);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
    }

    static_cast<void>(std::remove(metrics_global.c_str()));
    static_cast<void>(std::remove(metrics_local.c_str()));
    static_cast<void>(std::remove(tracing_global.c_str()));
    static_cast<void>(std::remove(tracing_local.c_str()));
}

void test_settings_sink() {
    EnvGuard interval_env{"NEXIGEN_METRICS_COLLECT_INTERVAL_NS"};
    EnvGuard metrics_mask_env{"NEXIGEN_METRICS_CORE_MASK"};
    EnvGuard trace_mask_env{"NEXIGEN_TRACING_CORE_MASK"};
    EnvGuard tracers_env{"NEXIGEN_TRACING_MAX_TRACERS"};
    EnvGuard spans_env{"NEXIGEN_TRACING_MAX_SPANS_PER_WINDOW"};
    EnvGuard window_env{"NEXIGEN_TRACING_WINDOW_NS"};
    interval_env.clear();
    metrics_mask_env.clear();
    trace_mask_env.clear();
    tracers_env.clear();
    spans_env.clear();
    window_env.clear();

    const std::string base = unique_path("app_settings_sink");
    const std::string metrics_global = base + "_mg.json";
    const std::string metrics_local = base + "_ml.json";
    const std::string tracing_global = base + "_tg.json";
    const std::string tracing_local = base + "_tl.json";

    // Local overrides global; absent local fields inherit global values.
    write_text(metrics_global, R"({"sink":{"path":"/tmp/global-metrics.jsonl","append":true}})");
    write_text(metrics_local, R"({"sink":{"path":"/tmp/local-metrics.jsonl","append":false}})");
    write_text(tracing_global, R"({"sink":{"path":"/tmp/global-traces.jsonl","rotation":"Hourly",)"
                               R"("max_spans_per_window":10,"window_ns":100}})");
    write_text(tracing_local, R"({"sink":{"path":"/tmp/local-traces.jsonl"}})");
    {
        metrics::Settings metrics{};
        tracing::Settings tracing{};
        NX_CHECK(
            metrics::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());
        NX_CHECK(metrics.sink.path == "/tmp/local-metrics.jsonl");
        NX_CHECK(!metrics.sink.append);
        NX_CHECK(
            tracing::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
        NX_CHECK(tracing.sink.path == "/tmp/local-traces.jsonl");
        NX_CHECK(tracing.sink.rotation == tracing::RotationPolicy::Hourly);
        NX_CHECK(tracing.sink.max_spans_per_window == 10ULL);
        NX_CHECK(tracing.sink.window_ns == 100ULL);

        // Provider environment overrides never touch the sink.
        interval_env.set("50000000");
        trace_mask_env.set("3");
        NX_CHECK(
            metrics::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());
        NX_CHECK(
            tracing::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
        NX_CHECK(metrics.sink.path == "/tmp/local-metrics.jsonl");
        NX_CHECK(tracing.sink.path == "/tmp/local-traces.jsonl");
        NX_CHECK(tracing.sink.rotation == tracing::RotationPolicy::Hourly);
        interval_env.clear();
        trace_mask_env.clear();
    }

    // Serialization carries both sink objects and the formatter matches.
    {
        metrics::Settings metrics{};
        tracing::Settings tracing{};
        NX_CHECK(
            metrics::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());
        NX_CHECK(
            tracing::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
        NX_CHECK(metrics.to_string().find("sink=JsonFileMetricSinkConfig") != std::string::npos);
        NX_CHECK(tracing.to_string().find("sink=JsonFileTraceSinkConfig") != std::string::npos);
        NX_CHECK(std::format("{}", metrics) == metrics.to_string());
        NX_CHECK(std::format("{}", tracing) == tracing.to_string());

        nlohmann::json json;
        metrics::to_json(json, metrics);
        NX_CHECK(json["sink"]["path"] == "/tmp/local-metrics.jsonl");
        NX_CHECK(json["sink"]["append"] == false);
        tracing::to_json(json, tracing);
        NX_CHECK(json["sink"]["path"] == "/tmp/local-traces.jsonl");
        NX_CHECK(json["sink"]["rotation"] == "Hourly");
        NX_CHECK(json["sink"]["max_spans_per_window"] == 10U);
        NX_CHECK(json["sink"]["window_ns"] == 100U);

        // Settings carries both sink objects through its members.
        app::Settings aggregate{};
        aggregate.metrics = metrics;
        aggregate.tracing = tracing;
        app::to_json(json, aggregate);
        NX_CHECK(json["metrics"]["sink"]["path"] == "/tmp/local-metrics.jsonl");
        NX_CHECK(json["tracing"]["sink"]["path"] == "/tmp/local-traces.jsonl");
        NX_CHECK(json["tracing"]["sink"]["rotation"] == "Hourly");
    }

    // Absent sink blocks leave the sink unconfigured.
    write_text(metrics_global, "{}");
    write_text(metrics_local, "{}");
    write_text(tracing_global, "{}");
    write_text(tracing_local, "{}");
    {
        metrics::Settings metrics{};
        tracing::Settings tracing{};
        NX_CHECK(
            metrics::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());
        NX_CHECK(
            tracing::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
        NX_CHECK(metrics.sink.path.empty());
        NX_CHECK(metrics.sink.append);
        NX_CHECK(tracing.sink.path.empty());
        NX_CHECK(tracing.sink.rotation == tracing::RotationPolicy::None);
        NX_CHECK(tracing.sink.max_spans_per_window == 0ULL);
        NX_CHECK(tracing.sink.window_ns == 0ULL);
    }

    // An empty path is accepted as "unconfigured", not a diagnostic.
    write_text(metrics_global, R"({"sink":{"path":""}})");
    {
        metrics::Settings metrics{};
        NX_CHECK(metrics::load_metrics_settings({metrics_global, ""}, metrics).has_value());
        NX_CHECK(metrics.sink.path.empty());
        NX_CHECK(metrics.diagnostics.empty());
    }

    // Invalid rotation keeps the previous policy and reports sink.rotation.
    write_text(tracing_global, R"({"sink":{"path":"/tmp/g.jsonl","rotation":"Hourly"}})");
    write_text(tracing_local, R"({"sink":{"path":"/tmp/l.jsonl","rotation":"Sometimes"}})");
    {
        tracing::Settings tracing{};
        NX_CHECK(
            !tracing::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
        NX_CHECK(tracing.sink.path == "/tmp/l.jsonl");
        NX_CHECK(tracing.sink.rotation == tracing::RotationPolicy::Hourly);
        NX_CHECK(tracing.diagnostics.size() == 1U);
        NX_CHECK(tracing.diagnostics[0].field == "sink.rotation");
    }

    // A non-object sink keeps the whole previous sink and reports "sink".
    write_text(tracing_local, R"({"sink":"not-an-object"})");
    {
        tracing::Settings tracing{};
        NX_CHECK(
            !tracing::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
        NX_CHECK(tracing.sink.path == "/tmp/g.jsonl");
        NX_CHECK(tracing.sink.rotation == tracing::RotationPolicy::Hourly);
        NX_CHECK(tracing.diagnostics[0].field == "sink");
    }

    // A non-object metrics sink reports "sink" and still populates provider fields.
    write_text(metrics_global, "{}");
    write_text(metrics_local, R"({"collect_interval_ns":42,"sink":"not-an-object"})");
    {
        metrics::Settings metrics{};
        const core::Status status =
            metrics::load_metrics_settings({metrics_global, metrics_local}, metrics);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
        NX_CHECK(metrics.options.collect_interval == std::chrono::nanoseconds{42});
        NX_CHECK(metrics.diagnostics.size() == 1U);
        NX_CHECK(metrics.diagnostics[0].field == "sink");
    }

    // Wrong field types keep the previous value and name the field.
    write_text(tracing_local, R"({"sink":{"max_spans_per_window":"x","window_ns":false}})");
    {
        tracing::Settings tracing{};
        NX_CHECK(
            !tracing::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
        NX_CHECK(tracing.sink.max_spans_per_window == 0ULL);
        NX_CHECK(tracing.sink.window_ns == 0ULL);
        NX_CHECK(tracing.diagnostics.size() == 2U);
        NX_CHECK(tracing.diagnostics[0].field == "sink.max_spans_per_window");
        NX_CHECK(tracing.diagnostics[1].field == "sink.window_ns");
    }

    static_cast<void>(std::remove(metrics_global.c_str()));
    static_cast<void>(std::remove(metrics_local.c_str()));
    static_cast<void>(std::remove(tracing_global.c_str()));
    static_cast<void>(std::remove(tracing_local.c_str()));
}

void test_app_settings_full_coverage() {
    EnvGuard interval_env{"NEXIGEN_METRICS_COLLECT_INTERVAL_NS"};
    EnvGuard metrics_mask_env{"NEXIGEN_METRICS_CORE_MASK"};
    EnvGuard trace_mask_env{"NEXIGEN_TRACING_CORE_MASK"};
    EnvGuard tracers_env{"NEXIGEN_TRACING_MAX_TRACERS"};
    EnvGuard spans_env{"NEXIGEN_TRACING_MAX_SPANS_PER_WINDOW"};
    EnvGuard window_env{"NEXIGEN_TRACING_WINDOW_NS"};
    interval_env.clear();
    metrics_mask_env.clear();
    trace_mask_env.clear();
    tracers_env.clear();
    spans_env.clear();
    window_env.clear();

    const std::string base = unique_path("app_settings_full");
    const std::string metrics_global = base + "_mg.json";
    const std::string metrics_local = base + "_ml.json";
    const std::string tracing_global = base + "_tg.json";
    const std::string tracing_local = base + "_tl.json";

    write_text(metrics_global, R"({"collect_interval_ns":123456,"core_mask":3,)"
                               R"("max_meters":1,"max_counters":2,"max_gauges":3,)"
                               R"("max_statistics":4,"max_explicit_histograms":5,)"
                               R"("max_exponential_histograms":6})");
    write_text(tracing_global, R"({"core_mask":7,"max_tracers":64,)"
                               R"("max_spans_per_window":77,"window_ns":88})");
    {
        metrics::Settings metrics{};
        NX_CHECK(metrics::load_metrics_settings({metrics_global, ""}, metrics).has_value());
        NX_CHECK(metrics.options.collect_interval == std::chrono::nanoseconds{123456});
        NX_CHECK(metrics.options.worker_thread.cpus() == core::CpuSet::from_core_mask(3ULL));
        NX_CHECK(metrics.options.max_meters == 1U);
        NX_CHECK(metrics.options.max_counters == 2U);
        NX_CHECK(metrics.options.max_gauges == 3U);
        NX_CHECK(metrics.options.max_statistics == 4U);
        NX_CHECK(metrics.options.max_explicit_histograms == 5U);
        NX_CHECK(metrics.options.max_exponential_histograms == 6U);
        tracing::Settings tracing{};
        NX_CHECK(tracing::load_tracing_settings({tracing_global, ""}, tracing).has_value());
        NX_CHECK(tracing.options.worker_thread.cpus() == core::CpuSet::from_core_mask(7ULL));
        NX_CHECK(tracing.options.max_tracers == 64U);
        NX_CHECK(tracing.options.max_spans_per_window == 77ULL);
        NX_CHECK(tracing.options.window_ns == 88ULL);
    }

    // Every metrics key wrong type -> diagnostics, values retained.
    write_text(metrics_local, R"({"collect_interval_ns":"x","core_mask":"x","max_meters":"x",)"
                              R"("max_counters":"x","max_gauges":"x","max_statistics":"x",)"
                              R"("max_explicit_histograms":"x","max_exponential_histograms":"x"})");
    {
        metrics::Settings metrics{};
        NX_CHECK(
            !metrics::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());
        NX_CHECK(metrics.diagnostics.size() >= 8U);
        NX_CHECK(metrics.options.collect_interval == std::chrono::nanoseconds{123456});
    }

    // Tracing invalid values: type errors, max_tracers 0/65, overflow.
    write_text(tracing_local, R"({"core_mask":"x","max_tracers":65,)"
                              R"("max_spans_per_window":"x","window_ns":"x"})");
    {
        tracing::Settings tracing{};
        NX_CHECK(
            !tracing::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
        NX_CHECK(tracing.diagnostics.size() >= 4U);
    }
    write_text(tracing_local, R"({"max_tracers":0})");
    {
        tracing::Settings tracing{};
        NX_CHECK(
            !tracing::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
    }
    write_text(metrics_local, R"({"collect_interval_ns":18446744073709551615})");
    {
        metrics::Settings metrics{};
        NX_CHECK(
            !metrics::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());
    }

    // Unreadable paths on both subsystems.
    {
        metrics::Settings metrics{};
        const core::Status status = metrics::load_metrics_settings({"/tmp/opencode", ""}, metrics);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::SystemError);
        const core::Status local_metrics =
            metrics::load_metrics_settings({metrics_global, "/tmp/opencode"}, metrics);
        NX_CHECK(!local_metrics.has_value());
        NX_CHECK(local_metrics.error() == core::ErrorCode::SystemError);
        tracing::Settings tracing{};
        const core::Status tracing_status =
            tracing::load_tracing_settings({"", "/tmp/opencode"}, tracing);
        NX_CHECK(!tracing_status.has_value());
        NX_CHECK(tracing_status.error() == core::ErrorCode::SystemError);
        const core::Status local_tracing =
            tracing::load_tracing_settings({tracing_global, "/tmp/opencode"}, tracing);
        NX_CHECK(!local_tracing.has_value());
        NX_CHECK(local_tracing.error() == core::ErrorCode::SystemError);
    }

    // Environment: every variable, valid and invalid.
    interval_env.set("50000000");
    metrics_mask_env.set("9");
    trace_mask_env.set("10");
    tracers_env.set("32");
    spans_env.set("100");
    window_env.set("200");
    {
        metrics::Settings metrics{};
        tracing::Settings tracing{};
        NX_CHECK(metrics::load_metrics_settings({"", ""}, metrics).has_value());
        NX_CHECK(tracing::load_tracing_settings({"", ""}, tracing).has_value());
        NX_CHECK(metrics.options.collect_interval == std::chrono::nanoseconds{50'000'000});
        NX_CHECK(metrics.options.worker_thread.cpus() == core::CpuSet::from_core_mask(9ULL));
        NX_CHECK(tracing.options.worker_thread.cpus() == core::CpuSet::from_core_mask(10ULL));
        NX_CHECK(tracing.options.max_tracers == 32U);
        NX_CHECK(tracing.options.max_spans_per_window == 100ULL);
        NX_CHECK(tracing.options.window_ns == 200ULL);
    }
    interval_env.set("x");
    metrics_mask_env.set("x");
    trace_mask_env.set("x");
    tracers_env.set("65");
    spans_env.set("x");
    window_env.set("x");
    {
        metrics::Settings metrics{};
        tracing::Settings tracing{};
        NX_CHECK(!metrics::load_metrics_settings({"", ""}, metrics).has_value());
        NX_CHECK(!tracing::load_tracing_settings({"", ""}, tracing).has_value());
        NX_CHECK(metrics.diagnostics.size() >= 2U);
        NX_CHECK(tracing.diagnostics.size() >= 4U);
    }
    interval_env.clear();
    metrics_mask_env.clear();
    trace_mask_env.clear();
    tracers_env.clear();
    spans_env.clear();
    window_env.clear();

    // Serialization of all four aggregate types.
    app::Settings aggregate{};
    aggregate.metrics = metrics::Settings{};
    aggregate.tracing = tracing::Settings{};
    aggregate.logging = logging::Settings{};
    const std::string metrics_text = aggregate.metrics.to_string();
    NX_CHECK(metrics_text.find("Settings") != std::string::npos);
    NX_CHECK(std::format("{}", aggregate.metrics) == metrics_text);
    const std::string tracing_text = aggregate.tracing.to_string();
    NX_CHECK(tracing_text.find("Settings") != std::string::npos);
    NX_CHECK(std::format("{}", aggregate.tracing) == tracing_text);
    const app::AppSettingsPaths path_texts{
        {"g-log", "l-log"}, {"g-met", "l-met"}, {"g-tra", "l-tra"}};
    NX_CHECK(path_texts.to_string().find("g-met") != std::string::npos);
    NX_CHECK(std::format("{}", path_texts).find("g-tra") != std::string::npos);
    NX_CHECK(aggregate.to_string().find("Settings") != std::string::npos);
    NX_CHECK(std::format("{}", aggregate).find("Settings") != std::string::npos);
    NX_CHECK(aggregate.signal_thread.set_name("sig").has_value());
    aggregate.signal_thread.set_cpus(nexigen::core::CpuSet::from_core_mask(1ULL));
    NX_CHECK(aggregate.to_string().find("signal_thread") != std::string::npos);
    nlohmann::json json;
    app::to_json(json, aggregate);
    NX_CHECK(json.contains("logging") && json.contains("metrics") && json.contains("tracing"));
    NX_CHECK(json["signal_thread"]["name"] == "sig");
    NX_CHECK(json["signal_thread"]["cpus"][0] == 0U);
    app::to_json(json, path_texts);
    NX_CHECK(json["metrics"]["global_file"] == "g-met");

    // Aggregate worst status: clean logging + unreadable metrics.
    app::AppSettingsPaths worst{};
    worst.metrics.global_file = "/tmp/opencode";
    app::Settings loaded{};
    const core::Status worst_status = app::load_app_settings(worst, loaded);
    NX_CHECK(!worst_status.has_value());
    NX_CHECK(worst_status.error() == core::ErrorCode::SystemError);

    static_cast<void>(std::remove(metrics_global.c_str()));
    static_cast<void>(std::remove(metrics_local.c_str()));
    static_cast<void>(std::remove(tracing_global.c_str()));
    static_cast<void>(std::remove(tracing_local.c_str()));
}

void test_settings_compatibility() {
    static_assert(std::is_aggregate_v<app::Settings>);
    // Legacy positional initialization (pre-signal_thread member order) still
    // compiles because the new field is appended last.
    [[maybe_unused]] app::Settings positional{
        std::string{"legacy"}, nexigen::logging::Settings{}, nexigen::metrics::Settings{},
        nexigen::tracing::Settings{}, nexigen::chrono::Settings{}};
    NX_CHECK(app::Settings::create("svc").app_name == "svc");
}

void run_app_settings_suites() {
    run_suite("settings", test_settings_loaders);
    run_suite("settings_sink", test_settings_sink);
    run_suite("settings_full", test_app_settings_full_coverage);
    run_suite("settings_compat", test_settings_compatibility);
}
