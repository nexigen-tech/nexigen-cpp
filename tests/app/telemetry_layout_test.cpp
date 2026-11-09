// Split from app_test.cpp: nexigen::app telemetry layout resolution.

#include "app_test_support.hpp"

void test_telemetry_layout() {
    EnvGuard run_id_env{"NEXIGEN_RUN_ID"};
    EnvGuard run_dir_env{"NEXIGEN_EXEC_DIR"};
    EnvGuard instance_id_env{"NEXIGEN_INSTANCE_ID"};
    EnvGuard instance_dir_env{"NEXIGEN_INSTANCE_DIR"};
    run_id_env.clear();
    run_dir_env.clear();
    instance_id_env.clear();
    instance_dir_env.clear();

    // Unset environment yields an empty, valid identity.
    {
        const app::RunIdentity empty = app::load_run_identity();
        NX_CHECK(empty.run_id.empty());
        NX_CHECK(empty.run_dir.empty());
        NX_CHECK(empty.instance_id.empty());
        NX_CHECK(empty.instance_dir.empty());
        NX_CHECK(app::validate_run_identity(empty).has_value());
    }

    // Partial environment preserves only the supplied fields.
    instance_id_env.set("i42");
    {
        const app::RunIdentity partial = app::load_run_identity();
        NX_CHECK(partial.run_id.empty());
        NX_CHECK(partial.run_dir.empty());
        NX_CHECK(partial.instance_id == "i42");
        NX_CHECK(partial.instance_dir.empty());
    }

    // The instance directory is derived from a separately supplied app name.
    NX_CHECK(app::derive_instance_dir("/var/nexigen/runs/r1", "controller", "i42") ==
             "/var/nexigen/runs/r1/controller/i42");

    // A trailing slash on the run directory does not double the separator.
    NX_CHECK(app::derive_instance_dir("/var/nexigen/runs/r1/", "controller", "i42") ==
             "/var/nexigen/runs/r1/controller/i42");

    // An explicit instance directory is read as-is.
    instance_dir_env.set("/tmp/explicit-instance");
    {
        const app::RunIdentity explicit_identity = app::load_run_identity();
        NX_CHECK(explicit_identity.instance_dir == "/tmp/explicit-instance");
    }
    instance_dir_env.clear();

    // Validation: safe ids and absolute directories pass.
    const app::RunIdentity valid{"r1", "/var/nexigen/runs/r1", "i42",
                                 "/var/nexigen/runs/r1/controller/i42"};
    NX_CHECK(app::validate_run_identity(valid).has_value());

    // Unsafe ids and relative directories are rejected.
    {
        app::RunIdentity bad = valid;
        bad.instance_id = "i..42";
        NX_CHECK(!app::validate_run_identity(bad).has_value());
    }
    {
        app::RunIdentity bad = valid;
        bad.run_id = "..";
        NX_CHECK(!app::validate_run_identity(bad).has_value());
    }
    {
        app::RunIdentity bad = valid;
        bad.run_id = std::string(129, 'a');
        NX_CHECK(!app::validate_run_identity(bad).has_value());
    }
    {
        app::RunIdentity bad = valid;
        bad.run_dir = "relative/runs";
        NX_CHECK(!app::validate_run_identity(bad).has_value());
    }
    {
        app::RunIdentity bad = valid;
        bad.instance_dir = "relative/instance";
        NX_CHECK(!app::validate_run_identity(bad).has_value());
    }

    // Per-signal precedence: explicit path, instance fallback, then empty.
    {
        app::TelemetryPaths paths{};
        NX_CHECK(app::resolve_telemetry_paths(valid, "", "", "", paths).has_value());
        NX_CHECK(paths.metrics == "/var/nexigen/runs/r1/controller/i42/metrics.jsonl");
        NX_CHECK(paths.traces == "/var/nexigen/runs/r1/controller/i42/traces.jsonl");
        NX_CHECK(paths.chrono == "/var/nexigen/runs/r1/controller/i42/chrono.jsonl");

        NX_CHECK(app::resolve_telemetry_paths(valid, "/explicit/metrics.jsonl", "", "", paths)
                     .has_value());
        NX_CHECK(paths.metrics == "/explicit/metrics.jsonl");
        NX_CHECK(paths.traces == "/var/nexigen/runs/r1/controller/i42/traces.jsonl");
        NX_CHECK(paths.chrono == "/var/nexigen/runs/r1/controller/i42/chrono.jsonl");

        NX_CHECK(app::resolve_telemetry_paths(valid, "", "/explicit/traces.jsonl",
                                              "/explicit/chrono.jsonl", paths)
                     .has_value());
        NX_CHECK(paths.metrics == "/var/nexigen/runs/r1/controller/i42/metrics.jsonl");
        NX_CHECK(paths.traces == "/explicit/traces.jsonl");
        NX_CHECK(paths.chrono == "/explicit/chrono.jsonl");
    }

    // The settings overload reads each sink path.
    {
        metrics::Settings metrics{};
        tracing::Settings tracing{};
        chrono::Settings chrono{};
        metrics.sink.path = "/settings/metrics.jsonl";
        tracing.sink.path = "";
        chrono.sink.path = "/settings/chrono.jsonl";
        app::TelemetryPaths paths{};
        NX_CHECK(app::resolve_telemetry_paths(valid, metrics, tracing, chrono, paths).has_value());
        NX_CHECK(paths.metrics == "/settings/metrics.jsonl");
        NX_CHECK(paths.traces == "/var/nexigen/runs/r1/controller/i42/traces.jsonl");
        NX_CHECK(paths.chrono == "/settings/chrono.jsonl");
    }

    // Empty identity and no explicit paths yield empty paths.
    {
        app::TelemetryPaths paths{};
        NX_CHECK(app::resolve_telemetry_paths(app::RunIdentity{}, "", "", "", paths).has_value());
        NX_CHECK(paths.metrics.empty() && paths.traces.empty() && paths.chrono.empty());
    }

    // Invalid identity is rejected before output is touched.
    {
        app::RunIdentity bad = valid;
        bad.instance_id = "bad id";
        app::TelemetryPaths untouched{"keep-metrics", "keep-traces", "keep-chrono"};
        const core::Status status = app::resolve_telemetry_paths(bad, "/x", "", "", untouched);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
        NX_CHECK(untouched.metrics == "keep-metrics");
        NX_CHECK(untouched.traces == "keep-traces");
        NX_CHECK(untouched.chrono == "keep-chrono");
    }

    // Resolution is pure: a non-existent instance directory is not created.
    {
        const std::string missing = unique_path("telemetry_layout_missing") + "/controller/i1";
        app::RunIdentity absent{"r1", "/tmp", "i1", missing};
        app::TelemetryPaths paths{};
        NX_CHECK(app::resolve_telemetry_paths(absent, "", "", "", paths).has_value());
        NX_CHECK(paths.metrics == missing + "/metrics.jsonl");
        NX_CHECK(::access(missing.c_str(), F_OK) != 0);
    }

    // Serialization round-trip and formatter equality.
    {
        nlohmann::json json;
        app::to_json(json, valid);
        app::RunIdentity back{};
        app::from_json(json, back);
        NX_CHECK(back.run_id == valid.run_id && back.run_dir == valid.run_dir);
        NX_CHECK(back.instance_id == valid.instance_id);
        NX_CHECK(back.instance_dir == valid.instance_dir);
        NX_CHECK(std::format("{}", valid) == valid.to_string());
        NX_CHECK(app::to_string(valid) == valid.to_string());

        app::from_json(nlohmann::json::array(), back);
        NX_CHECK(back.run_id == valid.run_id);
        app::from_json(nlohmann::json{{"run_id", 5}, {"extra", true}}, back);
        NX_CHECK(back.run_id == valid.run_id);
        NX_CHECK(back.instance_dir == valid.instance_dir);

        app::TelemetryPaths paths{"/m", "/t", "/c"};
        nlohmann::json paths_json;
        app::to_json(paths_json, paths);
        app::TelemetryPaths paths_back{};
        app::from_json(paths_json, paths_back);
        NX_CHECK(paths_back.metrics == "/m" && paths_back.traces == "/t" &&
                 paths_back.chrono == "/c");
        NX_CHECK(std::format("{}", paths) == paths.to_string());

        app::from_json(nlohmann::json::array(), paths);
        NX_CHECK(paths.metrics == "/m");
        app::from_json(nlohmann::json{{"metrics", 1}, {"traces", false}}, paths);
        NX_CHECK(paths.metrics == "/m" && paths.traces == "/t");
    }
}

void test_telemetry_layout_infrastructure() {
    EnvGuard run_id_env{"NEXIGEN_RUN_ID"};
    EnvGuard run_dir_env{"NEXIGEN_EXEC_DIR"};
    EnvGuard instance_id_env{"NEXIGEN_INSTANCE_ID"};
    EnvGuard instance_dir_env{"NEXIGEN_INSTANCE_DIR"};
    run_id_env.set("run-layout");
    run_dir_env.set("/tmp/opencode");
    instance_id_env.set("i1");

    const std::string instance_dir = unique_path("telemetry_instance");
    make_dir(instance_dir);
    instance_dir_env.set(instance_dir.c_str());

    const app::RunIdentity identity = app::load_run_identity();
    NX_CHECK(identity.instance_dir == instance_dir);

    app::Settings settings{};
    NX_CHECK(app::load_app_settings(app::AppSettingsPaths{}, settings).has_value());
    settings.logging.options.log_root_directory = instance_dir;
    settings.app_name = "layout-app";

    app::TelemetryPaths resolved{};
    NX_CHECK(app::resolve_telemetry_paths(identity, settings.metrics, settings.tracing,
                                          settings.chrono, resolved)
                 .has_value());
    NX_CHECK(resolved.metrics == instance_dir + "/metrics.jsonl");
    NX_CHECK(resolved.traces == instance_dir + "/traces.jsonl");
    NX_CHECK(resolved.chrono == instance_dir + "/chrono.jsonl");
    settings.metrics.sink.path = resolved.metrics;
    settings.tracing.sink.path = resolved.traces;
    settings.chrono.sink.path = resolved.chrono;

    auto infra = make_test_infra<ResolvedPathPolicy>();
    NX_CHECK(infra != nullptr);
    if (infra == nullptr) {
        return;
    }
    NX_CHECK(infra->initialize(settings).has_value());
    infra->logger().info("layout integration record");

    const auto meter = infra->meter("layout", "1.0");
    NX_CHECK(meter.has_value());
    if (meter.has_value()) {
        const auto counter = (*meter)
                                 ->create_counter<metrics::ConcurrencyPolicy::MultiThreaded,
                                                  metrics::AccessPolicy::Readable>("ticks");
        NX_CHECK(counter.has_value());
        if (counter.has_value()) {
            (*counter)->inc();
        }
    }
    NX_CHECK(infra->metrics().collect_now().has_value());
    NX_CHECK(infra->chrono().snapshot_now().has_value());

    const auto tracer = infra->tracer_for("layout.span");
    NX_CHECK(tracer.has_value());
    if (tracer.has_value()) {
        auto span = (*tracer)->start_span("work", tracing::SpanKind::Internal);
        static_cast<void>(span.set_status(tracing::SpanStatus::Ok));
        static_cast<void>(span.end());
    }
    NX_CHECK(infra->shutdown().has_value());

    const auto non_empty = [](const std::string& path) {
        struct stat st{};
        return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
    };
    NX_CHECK(non_empty(instance_dir + "/app.jsonl"));
    NX_CHECK(non_empty(resolved.metrics));
    NX_CHECK(non_empty(resolved.traces));
    NX_CHECK(non_empty(resolved.chrono));

    // Every artefact lives directly under the instance directory.
    DIR* const dir = ::opendir(instance_dir.c_str());
    NX_CHECK(dir != nullptr);
    if (dir != nullptr) {
        int entries = 0;
        while (const dirent* const entry = ::readdir(dir)) {
            const std::string_view name{entry->d_name};
            if (name == "." || name == "..") {
                continue;
            }
            ++entries;
            const bool known = name == "app.jsonl" || name == "metrics.jsonl" ||
                               name == "traces.jsonl" || name == "chrono.jsonl";
            NX_CHECK(known);
        }
        NX_CHECK(entries == 4);
        static_cast<void>(::closedir(dir));
    }

    static_cast<void>(std::remove((instance_dir + "/app.jsonl").c_str()));
    static_cast<void>(std::remove(resolved.metrics.c_str()));
    static_cast<void>(std::remove(resolved.traces.c_str()));
    static_cast<void>(std::remove(resolved.chrono.c_str()));
    static_cast<void>(::rmdir(instance_dir.c_str()));
}

void run_app_telemetry_layout_suites() {
    run_suite("telemetry_layout", test_telemetry_layout);
    run_suite("telemetry_infra", test_telemetry_layout_infrastructure);
}
