// Split from logging_test.cpp: nexigen::logging JSON formatting and config deserialization.

#include "logging_test_support.hpp"

namespace {

void test_json() {
    alignas(64) static char scratch[detail::json_scratch_capacity];

    // Byte-exact golden line (schema AND field order pinned, basename only).
    const std::size_t len =
        detail::format_json_line(1'784'850'120'321'841'133LL, LogLevel::Warning, "hil", 1'772'393U,
                                 "/home/dev/src/app.cpp", 42U, "void drive()", "ok", scratch);
    const std::string_view golden =
        "{\"timestamp\":\"2026-07-23T23:42:00.321841133Z\",\"level\":\"warning\","
        "\"app\":\"hil\",\"thread_id\":1772393,"
        "\"message\":\"[app.cpp:42] (void drive()) ok\"}\n";
    NX_CHECK(len == golden.size());
    NX_CHECK(std::memcmp(scratch, golden.data(), golden.size()) == 0);

    // Escapes: quote, backslash, short control forms, \u00XX for the rest.
    const char evil[] = "q\"bs\\ tab\tnl\nct\x01\x1fx";
    const std::size_t elen = detail::format_json_line(0LL, LogLevel::Error, "app", 7U, "f.cpp", 1U,
                                                      "main", evil, scratch);
    NX_CHECK(elen > 0);
    scratch[elen] = '\0';
    const std::string_view line(scratch, elen);
    NX_CHECK(line.find("q\\\"bs\\\\") != std::string_view::npos);
    NX_CHECK(line.find("tab\\t") != std::string_view::npos);
    NX_CHECK(line.find("nl\\n") != std::string_view::npos);
    NX_CHECK(line.find("ct\\u0001\\u001f") != std::string_view::npos);
    NX_CHECK(line.find('x') != std::string_view::npos);

    // Cyrillic UTF-8 passes through as raw bytes (no \u rewriting).
    const std::string_view ru = "Привет, мир";
    const std::size_t rlen =
        detail::format_json_line(0LL, LogLevel::Info, "app", 1U, "t.cpp", 2U, "main", ru, scratch);
    NX_CHECK(rlen > 0);
    scratch[rlen] = '\0';
    NX_CHECK(std::strstr(scratch, "Привет, мир") != nullptr);

    // Overflow guard: a monstrous function name cannot half-write a line.
    alignas(64) static char huge_fn[6000];
    static_assert(sizeof huge_fn > 0);
    std::memset(huge_fn, 'x', sizeof huge_fn - 1);
    huge_fn[sizeof huge_fn - 1] = '\0';
    const std::size_t over =
        detail::format_json_line(0LL, LogLevel::Info, "app", 1U, "t.cpp", 2U,
                                 std::string_view(huge_fn, sizeof huge_fn - 1), "m", scratch);
    NX_CHECK(over == 0);

    // FixedWriter unit: rejects writes that would exceed the small buffer.
    char small_[64];
    detail::FixedWriter w(small_, sizeof small_);
    NX_CHECK(w.append_sv(std::string_view(small_, 0))); // empty ok
    NX_CHECK(!w.append_sv(std::string_view(huge_fn, 100)));
    NX_CHECK(!w.ok());
    NX_CHECK(w.size() == 0); // nothing partial-written
}

// --- affinity suite (task 5.3 / spec logging/file-logger core affinity).

// --- JSON field-reader rejections (change refactor-logging-json-deserialization):
// malformed per-field inputs are silently ignored or abort the whole object
// without touching the caller's configuration, matching the JSON_NOEXCEPTION
// contract.
void test_logging_json_rejections() {
    // SpamProtectionConfig: wrong-shape / non-object leaves every field alone.
    {
        SpamProtectionConfig cfg{};
        cfg.enable_repeat_suppression = false;
        cfg.repeat_suppression_window_ms = 7U;
        cfg.max_messages_per_sec_per_shard = 13U;
        cfg.bypass_min_level = LogLevel::Error;
        from_json(nlohmann::json(42), cfg);
        NX_CHECK(cfg.enable_repeat_suppression == false);
        NX_CHECK(cfg.repeat_suppression_window_ms == 7U);
        NX_CHECK(cfg.max_messages_per_sec_per_shard == 13U);
        NX_CHECK(cfg.bypass_min_level == LogLevel::Error);
        from_json(nlohmann::json{{"enable_repeat_suppression", 1},
                                 {"repeat_suppression_window_ms", "x"},
                                 {"max_messages_per_sec_per_shard", nlohmann::json::array()},
                                 {"bypass_min_level", false}},
                  cfg);
        NX_CHECK(cfg.enable_repeat_suppression == false);
        NX_CHECK(cfg.repeat_suppression_window_ms == 7U);
        NX_CHECK(cfg.max_messages_per_sec_per_shard == 13U);
        NX_CHECK(cfg.bypass_min_level == LogLevel::Error);
        // Valid fields still apply independently.
        from_json(
            nlohmann::json{{"enable_repeat_suppression", true}, {"bypass_min_level", "fatal"}},
            cfg);
        NX_CHECK(cfg.enable_repeat_suppression == true);
        NX_CHECK(cfg.bypass_min_level == LogLevel::Fatal);
    }

    // LoggerOptions: non-object and wrong-typed keys leave the object untouched.
    {
        LoggerOptions cfg{};
        cfg.log_root_directory = "/sentinel";
        cfg.initial_log_level = LogLevel::Error;
        cfg.max_file_size_bytes = 123ULL;
        cfg.spsc_ring_buffer_capacity = 7U;
        cfg.message_capacity = 9U;
        cfg.max_producer_threads = 11U;
        cfg.overflow_policy = OverflowPolicy::OverwriteOldest;
        cfg.worker_poll_interval_us = 17U;
        cfg.sync_flush_on_critical = false;
        from_json(nlohmann::json::array(), cfg);
        NX_CHECK(cfg.log_root_directory == "/sentinel");
        from_json(nlohmann::json{{"log_root_directory", 5},
                                 {"initial_log_level", true},
                                 {"max_file_size_bytes", "x"},
                                 {"spsc_ring_buffer_capacity", "x"},
                                 {"message_capacity", "x"},
                                 {"max_producer_threads", "x"},
                                 {"overflow_policy", 3},
                                 {"core_mask", "x"},
                                 {"flush_cpu_affinity", "x"},
                                 {"worker_poll_interval_us", "x"},
                                 {"sync_flush_on_critical", "no"},
                                 {"spam_protection", "no"}},
                  cfg);
        NX_CHECK(cfg.log_root_directory == "/sentinel");
        NX_CHECK(cfg.initial_log_level == LogLevel::Error);
        NX_CHECK(cfg.max_file_size_bytes == 123ULL);
        NX_CHECK(cfg.spsc_ring_buffer_capacity == 7U);
        NX_CHECK(cfg.message_capacity == 9U);
        NX_CHECK(cfg.max_producer_threads == 11U);
        NX_CHECK(cfg.overflow_policy == OverflowPolicy::OverwriteOldest);
        NX_CHECK(cfg.worker_poll_interval_us == 17U);
        NX_CHECK(cfg.sync_flush_on_critical == false);
        // Valid pieces still apply.
        from_json(
            nlohmann::json{{"log_root_directory", "/updated"}, {"initial_log_level", "warning"}},
            cfg);
        NX_CHECK(cfg.log_root_directory == "/updated");
        NX_CHECK(cfg.initial_log_level == LogLevel::Warning);
    }

    // FileLoggerConfig: any malformed known field aborts and leaves `out` unchanged.
    {
        FileLoggerConfig cfg{};
        cfg.app_name = "sentinel-app";
        cfg.base_dir = "sentinel-base";
        cfg.file_name = "sentinel.jsonl";
        cfg.initial_level = LogLevel::Warning;
        cfg.throttle.enabled = true;
        cfg.throttle.max_records_per_window = 5ULL;
        cfg.throttle.window_ns = 99ULL;
        cfg.rotation.kind = RotationSchedule::Daily;
        cfg.rotation.custom_ns = 77ULL;
        cfg.max_file_size_bytes = 555ULL;
        cfg.worker_poll_interval_us = 88U;
        NX_CHECK(cfg.worker_thread.set_name("sentinel-worker").has_value());

        const auto unchanged = cfg;

        // Top-level string fields with wrong types.
        from_json(nlohmann::json{{"app_name", 7}}, cfg);
        NX_CHECK(cfg.app_name == unchanged.app_name);
        from_json(nlohmann::json{{"base_dir", true}}, cfg);
        NX_CHECK(cfg.base_dir == unchanged.base_dir);
        from_json(nlohmann::json{{"file_name", nullptr}}, cfg);
        NX_CHECK(cfg.file_name == unchanged.file_name);

        // Invalid enum name.
        from_json(nlohmann::json{{"initial_level", "chatty"}}, cfg);
        NX_CHECK(cfg.initial_level == unchanged.initial_level);

        // Throttle: non-object or bad inner type.
        from_json(nlohmann::json{{"throttle", "yes"}}, cfg);
        NX_CHECK(cfg.throttle.enabled == unchanged.throttle.enabled);
        from_json(nlohmann::json{{"throttle", {{"enabled", "yes"}}}}, cfg);
        NX_CHECK(cfg.throttle.enabled == unchanged.throttle.enabled);

        // Rotation: non-object or bad kind.
        from_json(nlohmann::json{{"rotation", 1}}, cfg);
        NX_CHECK(cfg.rotation.kind == unchanged.rotation.kind);
        from_json(nlohmann::json{{"rotation", {{"kind", 7}}}}, cfg);
        NX_CHECK(cfg.rotation.kind == unchanged.rotation.kind);

        // Worker thread parse failure.
        from_json(nlohmann::json{{"worker_thread", {{"policy", 7}}}}, cfg);
        NX_CHECK(cfg.worker_thread.name() == unchanged.worker_thread.name());

        // Unsigned fields with wrong type or range overflow.
        from_json(nlohmann::json{{"max_file_size_bytes", "x"}}, cfg);
        NX_CHECK(cfg.max_file_size_bytes == unchanged.max_file_size_bytes);
        from_json(nlohmann::json{{"worker_poll_interval_us", 0x1'0000'0000ULL}}, cfg);
        NX_CHECK(cfg.worker_poll_interval_us == unchanged.worker_poll_interval_us);

        // Non-object document is ignored.
        from_json(nlohmann::json(3), cfg);
        NX_CHECK(cfg.app_name == unchanged.app_name);

        // A fully valid document actually updates.
        from_json(nlohmann::json{{"app_name", "real"},
                                 {"base_dir", "/real"},
                                 {"file_name", "real.jsonl"},
                                 {"initial_level", "debug"},
                                 {"max_file_size_bytes", 1024ULL},
                                 {"worker_poll_interval_us", 2000ULL}},
                  cfg);
        NX_CHECK(cfg.app_name == "real");
        NX_CHECK(cfg.base_dir == "/real");
        NX_CHECK(cfg.file_name == "real.jsonl");
        NX_CHECK(cfg.initial_level == LogLevel::Debug);
        NX_CHECK(cfg.max_file_size_bytes == 1024ULL);
        NX_CHECK(cfg.worker_poll_interval_us == 2000U);
    }

    // ConsoleLoggerConfig: malformed fields abort without touching `out`.
    {
        ConsoleLoggerConfig cfg{};
        cfg.initial_level = LogLevel::Warning;
        cfg.worker_poll_interval_us = 55U;
        NX_CHECK(cfg.worker_thread.cpus().empty());

        from_json(nlohmann::json{{"initial_level", 7}}, cfg);
        NX_CHECK(cfg.initial_level == LogLevel::Warning);
        from_json(nlohmann::json{{"worker_thread", {{"cpus", "bad"}}}}, cfg);
        NX_CHECK(cfg.worker_thread.cpus().empty());
        from_json(nlohmann::json{{"worker_poll_interval_us", 0x1'0000'0000ULL}}, cfg);
        NX_CHECK(cfg.worker_poll_interval_us == 55U);

        from_json(nlohmann::json{{"initial_level", "error"}, {"worker_poll_interval_us", 123ULL}},
                  cfg);
        NX_CHECK(cfg.initial_level == LogLevel::Error);
        NX_CHECK(cfg.worker_poll_interval_us == 123U);
    }

    std::printf("logging_json_rejections: malformed from_json inputs rejected cleanly\n");
}

} // namespace

void suite_json() {
    run_suite("json", test_json);
}

void suite_logging_json_rejections() {
    run_suite("logging_json_rejections", test_logging_json_rejections);
}
