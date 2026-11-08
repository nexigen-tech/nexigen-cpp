// Split from logging_test.cpp: nexigen::logging settings loaders and apply.

#include "logging_test_support.hpp"

namespace {

void test_settings_vocabulary() {
    NX_CHECK(to_string(core::SettingsSource::Global) == "global");
    NX_CHECK(to_string(core::SettingsSource::Local) == "local");
    NX_CHECK(to_string(core::SettingsSource::Environment) == "environment");
    NX_CHECK(to_string(core::SettingsSource::Computed) == "computed");
    core::SettingsSource source = core::SettingsSource::Computed;
    NX_CHECK(from_string("local", source));
    NX_CHECK(source == core::SettingsSource::Local);
    NX_CHECK(!from_string("bogus", source));
    NX_CHECK(source == core::SettingsSource::Local);
    nlohmann::json source_json;
    to_json(source_json, core::SettingsSource::Environment);
    NX_CHECK(source_json == "environment");
    from_json(nlohmann::json("global"), source);
    NX_CHECK(source == core::SettingsSource::Global);
    from_json(nlohmann::json(7), source);
    NX_CHECK(source == core::SettingsSource::Global);
    NX_CHECK(std::format("{}", core::SettingsSource::Computed) == "computed");

    const core::SettingsPaths paths{"/tmp/global.json", "/tmp/local.json"};
    const std::string paths_text = paths.to_string();
    NX_CHECK(paths_text.find("/tmp/global.json") != std::string::npos);
    NX_CHECK(paths_text.find("/tmp/local.json") != std::string::npos);
    NX_CHECK(to_string(paths) == paths_text);
    nlohmann::json paths_json;
    to_json(paths_json, paths);
    core::SettingsPaths paths_back{};
    from_json(paths_json, paths_back);
    NX_CHECK(paths_back.global_file == paths.global_file);
    NX_CHECK(paths_back.local_file == paths.local_file);
    NX_CHECK(std::format("{}", paths) == paths_text);

    const core::SettingsDiagnostic diag{core::SettingsSource::Local, "app_name",
                                        "expected at most 63 bytes"};
    const std::string diag_text = diag.to_string();
    NX_CHECK(diag_text.find("local") != std::string::npos);
    NX_CHECK(diag_text.find("app_name") != std::string::npos);
    NX_CHECK(to_string(diag) == diag_text);
    nlohmann::json diag_json;
    to_json(diag_json, diag);
    core::SettingsDiagnostic diag_back{};
    from_json(diag_json, diag_back);
    NX_CHECK(diag_back.source == core::SettingsSource::Local);
    NX_CHECK(diag_back.field == "app_name");
    NX_CHECK(diag_back.message == "expected at most 63 bytes");
    NX_CHECK(std::format("{}", diag) == diag_text);

    Settings settings{};
    settings.global_loaded = true;
    settings.ring_capacity_set = true;
    settings.diagnostics.push_back(diag);
    const std::string settings_text = settings.to_string();
    NX_CHECK(settings_text.find("global_loaded=true") != std::string::npos);
    NX_CHECK(settings_text.find("ring_capacity_set=true") != std::string::npos);
    NX_CHECK(to_string(settings) == settings_text);
    nlohmann::json settings_json;
    to_json(settings_json, settings);
    NX_CHECK(settings_json["global_loaded"] == true);
    NX_CHECK(settings_json["diagnostics"].size() == 1U);
    NX_CHECK(std::format("{}", settings) == settings_text);

    std::printf("settings_vocabulary: enum/JSON/formatter/no-from_json verified\n");
}

// --- loader (spec: explicit paths, precedence, local-only app_name, env,
// best-effort diagnostics, explicit presence flags).
void test_logging_settings_loader() {
    EnvGuard level_env{"NEXIGEN_LOG_LEVEL"};
    EnvGuard dir_env{"NEXIGEN_LOG_DIR"};
    EnvGuard size_env{"NEXIGEN_LOG_MAX_FILE_SIZE_BYTES"};
    EnvGuard affinity_env{"NEXIGEN_LOG_CPU_AFFINITY"};
    EnvGuard ring_env{"NEXIGEN_LOG_RING_CAPACITY"};
    EnvGuard threads_env{"NEXIGEN_LOG_MAX_PRODUCER_THREADS"};
    EnvGuard overflow_env{"NEXIGEN_LOG_OVERFLOW_POLICY"};
    EnvGuard message_env{"NEXIGEN_LOG_MESSAGE_CAPACITY"};
    level_env.clear();
    dir_env.clear();
    size_env.clear();
    affinity_env.clear();
    ring_env.clear();
    threads_env.clear();
    overflow_env.clear();
    message_env.clear();

    // Explicit paths only: both empty -> defaults, no diagnostics.
    {
        Settings out{};
        NX_CHECK(load_logging_settings(core::SettingsPaths{}, out).has_value());
        NX_CHECK(!out.global_loaded && !out.local_loaded);
        NX_CHECK(!out.ring_capacity_set && !out.max_threads_set);
        NX_CHECK(out.diagnostics.empty());
        NX_CHECK(out.options.log_root_directory == "./logs");
    }

    const std::string global_path = unique_path("settings_global");
    const std::string local_path = unique_path("settings_local");
    write_text(global_path, R"({"log_root_directory":"/tmp/opencode/global-logs",)"
                            R"("initial_log_level":"warning",)"
                            R"("max_file_size_bytes":1234,)"
                            R"("flush_cpu_affinity":[1,3]})");
    write_text(local_path, R"({"initial_log_level":"debug",)"
                           R"("spsc_ring_buffer_capacity":16,)"
                           R"("message_capacity":64})");

    {
        Settings out{};
        NX_CHECK(
            load_logging_settings(core::SettingsPaths{global_path, local_path}, out).has_value());
        NX_CHECK(out.global_loaded && out.local_loaded);
        NX_CHECK(out.options.log_root_directory == "/tmp/opencode/global-logs");
        NX_CHECK(out.options.initial_log_level == LogLevel::Debug);
        NX_CHECK(out.options.max_file_size_bytes == 1234ULL);
        NX_CHECK(out.options.worker_thread.cpus() ==
                 core::CpuSet::from_core_mask((1ULL << 1U) | (1ULL << 3U)));
        NX_CHECK(out.ring_capacity_set);
        NX_CHECK(!out.max_threads_set);
        NX_CHECK(out.message_capacity_set);
        NX_CHECK(out.options.message_capacity == 64U);
        NX_CHECK(out.diagnostics.empty());
    }

    // Environment overrides: highest priority; affinity grammar.
    level_env.set("error");
    affinity_env.set("0,2-3");
    message_env.set("256");
    {
        Settings out{};
        NX_CHECK(
            load_logging_settings(core::SettingsPaths{global_path, local_path}, out).has_value());
        NX_CHECK(out.options.initial_log_level == LogLevel::Error);
        NX_CHECK(out.options.worker_thread.cpus() ==
                 core::CpuSet::from_core_mask((1ULL << 0U) | (1ULL << 2U) | (1ULL << 3U)));
        NX_CHECK(out.options.message_capacity == 256U);
        NX_CHECK(out.message_capacity_set);
    }
    level_env.clear();
    message_env.clear();
    affinity_env.clear();

    // Missing local file: no diagnostic, global still applies.
    {
        Settings out{};
        NX_CHECK(load_logging_settings(
                     core::SettingsPaths{global_path, "/tmp/opencode/does-not-exist.json"}, out)
                     .has_value());
        NX_CHECK(out.global_loaded && !out.local_loaded);
        NX_CHECK(out.diagnostics.empty());
        NX_CHECK(out.options.initial_log_level == LogLevel::Warning);
    }

    // Malformed local JSON: diagnostic, global values retained, InvalidArgument.
    write_text(local_path, R"({"initial_log_level":)");
    {
        Settings out{};
        const Status status =
            load_logging_settings(core::SettingsPaths{global_path, local_path}, out);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == ErrorCode::InvalidArgument);
        NX_CHECK(out.options.initial_log_level == LogLevel::Warning);
        NX_CHECK(out.local_loaded);
        bool malformed_seen = false;
        for (const core::SettingsDiagnostic& diag : out.diagnostics) {
            if (diag.source == core::SettingsSource::Local &&
                diag.message.find("malformed") != std::string::npos) {
                malformed_seen = true;
            }
        }
        NX_CHECK(malformed_seen);
    }

    // Wrong type: diagnostic names the field, previous value survives.
    write_text(local_path, R"({"max_file_size_bytes":"huge"})");
    {
        Settings out{};
        const Status status =
            load_logging_settings(core::SettingsPaths{global_path, local_path}, out);
        NX_CHECK(!status.has_value());
        NX_CHECK(out.options.max_file_size_bytes == 1234ULL);
        bool field_seen = false;
        for (const core::SettingsDiagnostic& diag : out.diagnostics) {
            if (diag.field == "max_file_size_bytes" && diag.source == core::SettingsSource::Local) {
                field_seen = true;
            }
        }
        NX_CHECK(field_seen);
    }

    // Invalid enum keeps the previous valid value.
    write_text(local_path, R"({"initial_log_level":"chatty"})");
    {
        Settings out{};
        NX_CHECK(
            !load_logging_settings(core::SettingsPaths{global_path, local_path}, out).has_value());
        NX_CHECK(out.options.initial_log_level == LogLevel::Warning);
    }

    // Explicit capacity flags are set even when validation fails.
    {
        write_text(local_path, R"({"spsc_ring_buffer_capacity":0,)"
                               R"("max_producer_threads":"many"})");
        Settings out{};
        NX_CHECK(!load_logging_settings(core::SettingsPaths{"", local_path}, out).has_value());
        NX_CHECK(out.ring_capacity_set);
        NX_CHECK(out.max_threads_set);
    }

    // Unreadable supplied path (a directory) -> SystemError.
    {
        Settings out{};
        const Status status = load_logging_settings(core::SettingsPaths{"/tmp/opencode", ""}, out);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == ErrorCode::SystemError);
        NX_CHECK(!out.diagnostics.empty());
    }

    // Invalid environment affinity is a diagnostic, not a crash.
    affinity_env.set("3-1");
    {
        Settings out{};
        NX_CHECK(!load_logging_settings(core::SettingsPaths{}, out).has_value());
        NX_CHECK(out.diagnostics.back().source == core::SettingsSource::Environment);
    }
    affinity_env.clear();

    // Valid values for the remaining environment overrides.
    dir_env.set("/tmp/env-valid");
    size_env.set("123");
    ring_env.set("32");
    threads_env.set("4");
    overflow_env.set("overwrite_oldest");
    {
        Settings out{};
        NX_CHECK(load_logging_settings({global_path, ""}, out).has_value());
        NX_CHECK(out.options.log_root_directory == "/tmp/env-valid");
        NX_CHECK(out.options.max_file_size_bytes == 123ULL);
        NX_CHECK(out.options.spsc_ring_buffer_capacity == 32U);
        NX_CHECK(out.options.max_producer_threads == 4U);
        NX_CHECK(out.options.overflow_policy == OverflowPolicy::OverwriteOldest);
    }
    dir_env.clear();
    size_env.clear();
    ring_env.clear();
    threads_env.clear();
    overflow_env.clear();

    // Diagnostics rendering with multiple entries (separator branch).
    {
        Settings out{};
        out.diagnostics.push_back({core::SettingsSource::Local, "a", "first"});
        out.diagnostics.push_back({core::SettingsSource::Local, "b", "second"});
        NX_CHECK(out.to_string().find("; ") != std::string::npos);
        nlohmann::json json;
        to_json(json, out);
        NX_CHECK(json["diagnostics"].size() == 2U);
    }

    // Hostile readers for paths/diagnostics.
    {
        core::SettingsPaths keep{"g", "l"};
        from_json(nlohmann::json(42), keep);
        NX_CHECK(keep.global_file == "g");
        from_json(nlohmann::json{{"global_file", 7}}, keep);
        NX_CHECK(keep.global_file == "g");
        core::SettingsDiagnostic diag_keep{core::SettingsSource::Local, "f", "m"};
        from_json(nlohmann::json{{"source", 7}, {"field", 7}, {"message", 7}}, diag_keep);
        NX_CHECK(diag_keep.field == "f");
    }

    // apply_to rejects an oversized root directory.
    {
        Settings oversized{};
        oversized.options.log_root_directory = std::string(512, 'x');
        FileLoggerConfig config{};
        NX_CHECK(!apply_to(oversized, config).has_value());
    }

    static_cast<void>(std::remove(global_path.c_str()));
    static_cast<void>(std::remove(local_path.c_str()));

    std::printf("logging_settings_loader: explicit paths, merge, env, diagnostics verified\n");
}

// --- apply/capacity (spec: merged settings apply, explicit limits only).
void test_logging_settings_apply() {
    Settings settings{};
    settings.options.initial_log_level = LogLevel::Warning;
    settings.options.log_root_directory = "/var/log/hil";
    settings.options.max_file_size_bytes = 4096ULL;
    settings.options.worker_thread.set_cpus(
        core::CpuSet::from_core_mask((1ULL << 1U) | (1ULL << 3U)));
    settings.options.spam_protection.max_messages_per_sec_per_shard = 500U;
    settings.options.worker_poll_interval_us = 5000U;

    FileLoggerConfig cfg{};
    NX_CHECK(apply_to(settings, cfg).has_value());
    NX_CHECK(cfg.worker_poll_interval_us == 5000U);
    NX_CHECK(cfg.base_dir == "/var/log/hil");
    NX_CHECK(cfg.initial_level == LogLevel::Warning);
    NX_CHECK(cfg.max_file_size_bytes == 4096ULL);
    NX_CHECK(cfg.worker_thread.cpus() == core::CpuSet::from_core_mask((1ULL << 1U) | (1ULL << 3U)));
    NX_CHECK(cfg.throttle.enabled);
    NX_CHECK(cfg.throttle.max_records_per_window == 500ULL);
    NX_CHECK(cfg.throttle.window_ns == 1'000'000'000ULL);

    settings.options.worker_thread.set_cpus(core::CpuSet{});
    NX_CHECK(apply_to(settings, cfg).has_value());
    NX_CHECK(cfg.worker_thread.cpus().empty());

    ConsoleLogger<>::Config console_cfg{};
    NX_CHECK(apply_to(settings, console_cfg).has_value());
    NX_CHECK(console_cfg.initial_level == LogLevel::Warning);
    NX_CHECK(console_cfg.worker_poll_interval_us == 5000U);

    // Capacity check: explicit overflow rejected, defaults ignored.
    settings.ring_capacity_set = true;
    settings.options.spsc_ring_buffer_capacity = 1024U;
    std::vector<core::SettingsDiagnostic> diagnostics;
    const Status too_big = check_capacity<128, 32, 128>(settings, &diagnostics);
    NX_CHECK(!too_big.has_value());
    NX_CHECK(too_big.error() == ErrorCode::InvalidArgument);
    NX_CHECK(diagnostics.size() == 1U);
    NX_CHECK(diagnostics.front().source == core::SettingsSource::Computed);
    NX_CHECK(diagnostics.front().message.find("1024") != std::string::npos);
    NX_CHECK(diagnostics.front().message.find("128") != std::string::npos);

    settings.ring_capacity_set = false;
    NX_CHECK((check_capacity<128, 32, 128>(settings, &diagnostics).has_value()));
    settings.ring_capacity_set = true;
    settings.options.spsc_ring_buffer_capacity = 128U;
    settings.max_threads_set = true;
    settings.options.max_producer_threads = 32U;
    NX_CHECK((check_capacity<128, 32, 128>(settings, &diagnostics).has_value()));

    // Explicit message capacity above the compile-time bound is rejected.
    settings.message_capacity_set = true;
    settings.options.message_capacity = 512U;
    diagnostics.clear();
    const Status msg_too_big = check_capacity<128, 32, 128>(settings, &diagnostics);
    NX_CHECK(!msg_too_big.has_value());
    NX_CHECK(msg_too_big.error() == ErrorCode::InvalidArgument);
    NX_CHECK(diagnostics.size() == 1U);
    NX_CHECK(diagnostics.front().field == "message_capacity");
    NX_CHECK(diagnostics.front().message.find("512") != std::string::npos);
    NX_CHECK(diagnostics.front().message.find("128") != std::string::npos);

    // Default (not explicitly set) message capacity is never checked.
    settings.message_capacity_set = false;
    settings.options.message_capacity = 512U;
    diagnostics.clear();
    NX_CHECK((check_capacity<128, 32, 128>(settings, &diagnostics).has_value()));
    NX_CHECK(diagnostics.empty());
    settings.options.message_capacity = 128U;

    // initialize(Settings) runs the check before touching the worker.
    {
        static FileLogger<64, 128, 1> too_small{};
        Settings oversized{};
        oversized.ring_capacity_set = true;
        oversized.options.spsc_ring_buffer_capacity = 128U;
        const Status status = too_small.initialize(oversized);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == ErrorCode::InvalidArgument);
    }

    // Integration: initialize from merged settings, log and flush.
    {
        static FileLogger<64, 128, 1> from_settings{};
        const std::string dir = unique_path("settings_apply");
        static_cast<void>(::mkdir(dir.c_str(), 0755));
        Settings loaded{};
        loaded.options.log_root_directory = dir;
        loaded.options.initial_log_level = LogLevel::Info;
        loaded.options.max_file_size_bytes = 0ULL;
        NX_CHECK(from_settings.initialize(loaded).has_value());
        from_settings.info("initialized from merged settings");
        NX_CHECK(from_settings.flush().has_value());
        NX_CHECK(from_settings.level() == LogLevel::Info);
    }

    std::printf("logging_settings_apply: field mapping, capacity, initialize verified\n");
}

// --- extended settings coverage (change improve-test-coverage):
// every valid/invalid field branch, affinity grammar, env failures,
// unreadable files and serialization.
void test_logging_settings_full_coverage() {
    EnvGuard level_env{"NEXIGEN_LOG_LEVEL"};
    EnvGuard dir_env{"NEXIGEN_LOG_DIR"};
    EnvGuard size_env{"NEXIGEN_LOG_MAX_FILE_SIZE_BYTES"};
    EnvGuard affinity_env{"NEXIGEN_LOG_CPU_AFFINITY"};
    EnvGuard ring_env{"NEXIGEN_LOG_RING_CAPACITY"};
    EnvGuard threads_env{"NEXIGEN_LOG_MAX_PRODUCER_THREADS"};
    EnvGuard overflow_env{"NEXIGEN_LOG_OVERFLOW_POLICY"};
    level_env.clear();
    dir_env.clear();
    size_env.clear();
    affinity_env.clear();
    ring_env.clear();
    threads_env.clear();
    overflow_env.clear();

    const std::string global_path = unique_path("full_settings_global");
    const std::string local_path = unique_path("full_settings_local");

    // All fields valid, including the nested spam config.
    write_text(local_path,
               R"({"log_root_directory":"/tmp/full",)"
               R"("initial_log_level":"warning","max_file_size_bytes":42,)"
               R"("spsc_ring_buffer_capacity":64,"max_producer_threads":8,)"
               R"("overflow_policy":"overwrite_oldest","flush_cpu_affinity":[0,63],)"
               R"("worker_poll_interval_us":250,"sync_flush_on_critical":false,)"
               R"("spam_protection":{"enable_repeat_suppression":false,)"
               R"("repeat_suppression_window_ms":10,"repeat_suppression_max_count":20,)"
               R"("max_messages_per_sec_per_shard":30,"max_bytes_per_sec_per_shard":40,)"
               R"("max_total_bytes_per_sec":50,"bypass_min_level":"error"}})");
    {
        Settings out{};
        NX_CHECK(load_logging_settings({global_path, local_path}, out).has_value());
        NX_CHECK(out.options.log_root_directory == "/tmp/full");
        NX_CHECK(out.options.initial_log_level == LogLevel::Warning);
        NX_CHECK(out.options.max_file_size_bytes == 42ULL);
        NX_CHECK(out.options.spsc_ring_buffer_capacity == 64U);
        NX_CHECK(out.options.max_producer_threads == 8U);
        NX_CHECK(out.options.overflow_policy == OverflowPolicy::OverwriteOldest);
        NX_CHECK(out.options.worker_thread.cpus() ==
                 core::CpuSet::from_core_mask((1ULL << 0U) | (1ULL << 63U)));
        NX_CHECK(out.options.worker_poll_interval_us == 250U);
        NX_CHECK(out.options.sync_flush_on_critical == false);
        NX_CHECK(out.options.spam_protection.enable_repeat_suppression == false);
        NX_CHECK(out.options.spam_protection.repeat_suppression_window_ms == 10U);
        NX_CHECK(out.options.spam_protection.bypass_min_level == LogLevel::Error);
        nlohmann::json json;
        to_json(json, out);
        NX_CHECK(json["diagnostics"].is_array());
    }

    // Every field invalid: one diagnostic per field, values retained.
    write_text(local_path, R"({"log_root_directory":5,)"
                           R"("initial_log_level":7,"max_file_size_bytes":"x",)"
                           R"("spsc_ring_buffer_capacity":0,"max_producer_threads":4294967296,)"
                           R"("overflow_policy":7,"flush_cpu_affinity":"x",)"
                           R"("worker_poll_interval_us":"x","sync_flush_on_critical":1,)"
                           R"("spam_protection":7})");
    {
        Settings out{};
        NX_CHECK(!load_logging_settings({"", local_path}, out).has_value());
        NX_CHECK(out.diagnostics.size() >= 9U);
        NX_CHECK(out.ring_capacity_set);
        NX_CHECK(out.max_threads_set);
    }

    // Nested spam fields invalid + affinity id out of range (removed
    // quota keys from change fix-p2-observability-and-writers no longer
    // produce diagnostics — they are not part of the surface).
    write_text(local_path, R"({"flush_cpu_affinity":[1,1024],"spam_protection":{)"
                           R"("enable_repeat_suppression":1,"repeat_suppression_window_ms":"x",)"
                           R"("max_messages_per_sec_per_shard":"x",)"
                           R"("bypass_min_level":"chatty"}})");
    {
        Settings out{};
        NX_CHECK(!load_logging_settings({"", local_path}, out).has_value());
        NX_CHECK(out.diagnostics.size() >= 5U);
    }

    // Non-object document and malformed JSON (global and local).
    write_text(global_path, R"([])");
    {
        Settings out{};
        NX_CHECK(!load_logging_settings({global_path, ""}, out).has_value());
        NX_CHECK(!out.diagnostics.empty());
    }
    write_text(global_path, "{");
    {
        Settings out{};
        NX_CHECK(!load_logging_settings({global_path, ""}, out).has_value());
    }

    // A directory path is unreadable on both sources.
    {
        Settings out{};
        const Status global_status = load_logging_settings({"/tmp/opencode", ""}, out);
        NX_CHECK(!global_status.has_value());
        NX_CHECK(global_status.error() == ErrorCode::SystemError);
        const Status local_status = load_logging_settings({"", "/tmp/opencode"}, out);
        NX_CHECK(!local_status.has_value());
        NX_CHECK(local_status.error() == ErrorCode::SystemError);
    }

    // Environment failures for every variable.
    write_text(global_path, R"({"log_root_directory":"/tmp/env",)"
                            R"("initial_log_level":"info"})");
    level_env.set("chatty");
    dir_env.set(std::string(512, 'd').c_str());
    size_env.set("x");
    ring_env.set("0");
    threads_env.set("0");
    overflow_env.set("x");
    {
        Settings out{};
        NX_CHECK(!load_logging_settings({global_path, ""}, out).has_value());
        NX_CHECK(out.diagnostics.size() >= 6U);
    }
    level_env.clear();
    dir_env.clear();
    size_env.clear();
    ring_env.clear();
    threads_env.clear();
    overflow_env.clear();

    // Affinity grammar: invalid tokens fail, valid list expands.
    const std::array<std::string_view, 8> invalid_affinity{"",    ",",    "1-",   "-2",
                                                           "3-1", "1024", "1,,2", " 1"};
    for (const std::string_view token : invalid_affinity) {
        affinity_env.set(std::string(token).c_str());
        Settings out{};
        const Status status = load_logging_settings({global_path, ""}, out);
        if (token.empty()) {
            NX_CHECK(status.has_value()); // empty env is ignored
        } else {
            NX_CHECK(!status.has_value());
        }
    }
    affinity_env.set("0-63");
    {
        Settings out{};
        NX_CHECK(load_logging_settings({global_path, ""}, out).has_value());
        NX_CHECK(out.options.worker_thread.cpus().count() == 64U);
        NX_CHECK(out.options.worker_thread.cpus().contains(0U));
        NX_CHECK(out.options.worker_thread.cpus().contains(63U));
    }
    affinity_env.clear();

    static_cast<void>(std::remove(global_path.c_str()));
    static_cast<void>(std::remove(local_path.c_str()));
}

} // namespace

void suite_settings_vocabulary() {
    run_suite("settings_vocabulary", test_settings_vocabulary);
}

void suite_logging_settings_loader() {
    run_suite("logging_settings_loader", test_logging_settings_loader);
}

void suite_logging_settings_apply() {
    run_suite("logging_settings_apply", test_logging_settings_apply);
}

void suite_settings_full_coverage() {
    run_suite("settings_full_coverage", test_logging_settings_full_coverage);
}
