// Split from logging_test.cpp: nexigen::logging enum and config vocabularies.

#include "logging_test_support.hpp"

namespace {

// --- enum vocabulary suites (change add-serialization-and-wait-free-logging,
// tasks 2.2/2.3): LogLevel + RotationSchedule from_string / to_json /
// from_json / std::formatter. Cold-path JSON allocations live within the arena.

void test_enum_vocab() {
    // to_string <-> from_string round-trips (all enumerators).
    const LogLevel levels[] = {LogLevel::Verbose, LogLevel::Debug, LogLevel::Info,
                               LogLevel::Warning, LogLevel::Error, LogLevel::Fatal,
                               LogLevel::Off};
    for (const LogLevel lv : levels) {
        LogLevel parsed = LogLevel::Off;
        NX_CHECK(from_string(to_string(lv), parsed));
        NX_CHECK(parsed == lv);
        nlohmann::json j;
        to_json(j, lv);
        NX_CHECK(j.is_string());
        NX_CHECK(j == std::string(to_string(lv)));
        LogLevel back = LogLevel::Verbose;
        from_json(j, back);
        NX_CHECK(back == lv);
    }
    static_assert(to_string(LogLevel::Warning) == "warning"); // pinned spellings
    static_assert([] {
        LogLevel l{};
        return from_string("info", l) && l == LogLevel::Info;
    }());

    // Hostile level inputs: non-string JSON types and unknown names leave the
    // fallback (no throw, no terminate under JSON_NOEXCEPTION).
    for (const nlohmann::json& hostile :
         {nlohmann::json(3), nlohmann::json::array(), nlohmann::json(), nlohmann::json("WARN"),
          nlohmann::json("Unknown")}) {
        LogLevel keep = LogLevel::Info;
        from_json(hostile, keep);
        NX_CHECK(keep == LogLevel::Info);
    }
    LogLevel untouched = LogLevel::Error;
    NX_CHECK(!from_string("warning ", untouched)); // exact spelling only
    NX_CHECK(untouched == LogLevel::Error);

    // std::formatter<LogLevel>: name via "{}", fill/align via string_view rules.
    NX_CHECK(std::format("{}", LogLevel::Warning) == "warning");
    NX_CHECK(std::format("[{:*>8}]", LogLevel::Info) == "[****info]");
    static_assert(std::formattable<const LogLevel&, char>);

    // RotationSchedule vocabulary (task 2.3) — RotationSchedule alias.
    const RotationSchedule kinds[] = {RotationSchedule::None, RotationSchedule::Hourly,
                                      RotationSchedule::Daily, RotationSchedule::Custom};
    for (const RotationSchedule k : kinds) {
        RotationSchedule parsed = RotationSchedule::None;
        NX_CHECK(from_string(to_string(k), parsed));
        NX_CHECK(parsed == k);
        nlohmann::json j;
        to_json(j, k);
        NX_CHECK(j.is_string());
        RotationSchedule back = RotationSchedule::Custom;
        from_json(j, back);
        NX_CHECK(back == k);
    }
    NX_CHECK(std::format("rotate={}", RotationSchedule::Daily) == "rotate=daily");
    NX_CHECK(std::format("[{:*>8}]", RotationSchedule::Hourly) == "[**hourly]");
    RotationSchedule keep = RotationSchedule::Hourly;
    from_json(nlohmann::json("weekly"), keep);
    NX_CHECK(keep == RotationSchedule::Hourly);
    from_json(nlohmann::json(7), keep);
    NX_CHECK(keep == RotationSchedule::Hourly);

    std::printf("enum_vocab: levels x7 + schedules x4 round-tripped, hostile inputs "
                "silent, formatters verified\n");
}

// --- config vocabulary suite (task 3.3): DroppedStats / ThrottleConfig /
// RotationConfig / FileLoggerConfig / ConsoleLogger::Config to_string.
void test_config_strings() {
    const ThrottleConfig throttle{
        .enabled = true, .max_records_per_window = 1000, .window_ns = 1'000'000'000ULL};
    std::string s = throttle.to_string();
    NX_CHECK(s.find("enabled=true") != std::string::npos);
    NX_CHECK(s.find("max_records_per_window=1000") != std::string::npos);
    NX_CHECK(s.find("window_ns=1000000000") != std::string::npos);
    NX_CHECK(to_string(throttle) == s);

    const RotationConfig rotation{.kind = RotationSchedule::Custom, .custom_ns = 42ULL};
    s = rotation.to_string();
    NX_CHECK(s.find("custom") != std::string::npos);
    NX_CHECK(s.find("custom_ns=42") != std::string::npos);
    NX_CHECK(to_string(rotation) == s);

    // Populated FileLoggerConfig renders every field (Cyrillic app included).
    FileLoggerConfig cfg{
        .app_name = "ХилСимулятор",
        .base_dir = "/var/log/nexigen",
        .file_name = "sim.jsonl",
        .initial_level = LogLevel::Debug,
        .throttle = throttle,
        .rotation = rotation,
        .worker_thread = core::ThreadSpec{},
        .max_file_size_bytes = 8192ULL,
    };
    NX_CHECK(cfg.worker_thread.set_name("file-worker").has_value());
    cfg.worker_thread.set_cpus(core::CpuSet::from_core_mask(1ULL << 6U));
    NX_CHECK(cfg.worker_thread.set_scheduler(core::SchedulingPolicy::RR, 40).has_value());
    cfg.worker_thread.set_apply_mode(core::ThreadApplyMode::Required);
    s = cfg.to_string();
    NX_CHECK(s.find("ХилСимулятор") != std::string::npos);
    NX_CHECK(s.find("/var/log/nexigen") != std::string::npos);
    NX_CHECK(s.find("sim.jsonl") != std::string::npos);
    NX_CHECK(s.find("debug") != std::string::npos);
    NX_CHECK(s.find("max_records_per_window=1000") != std::string::npos);
    NX_CHECK(s.find("max_file_size_bytes=8192") != std::string::npos);
    NX_CHECK(s.find("custom_ns=42") != std::string::npos);
    NX_CHECK(s.find("worker_thread=") != std::string::npos);
    NX_CHECK(s.find("file-worker") != std::string::npos);
    NX_CHECK(s.find("policy=rr") != std::string::npos);
    NX_CHECK(to_string(cfg) == s);
    NX_CHECK(std::format("{}", cfg) == s);
    FileLoggerConfig cfg_back{};
    from_json(nlohmann::json(cfg), cfg_back);
    NX_CHECK(cfg_back.app_name == cfg.app_name);
    NX_CHECK(cfg_back.base_dir == cfg.base_dir);
    NX_CHECK(cfg_back.file_name == cfg.file_name);
    NX_CHECK(cfg_back.worker_thread == cfg.worker_thread);
    NX_CHECK(cfg_back.rotation.custom_ns == cfg.rotation.custom_ns);
    NX_CHECK(cfg_back.throttle.max_records_per_window == cfg.throttle.max_records_per_window);

    const DroppedStats stats{.rate_limited = 7, .buffer_full = 9};
    s = stats.to_string();
    NX_CHECK(s.find("rate_limited=7") != std::string::npos);
    NX_CHECK(s.find("buffer_full=9") != std::string::npos);
    NX_CHECK(s.find("total=16") != std::string::npos);
    NX_CHECK(to_string(stats) == s);

    ConsoleLogger<>::Config console_cfg{.initial_level = LogLevel::Warning};
    console_cfg.worker_thread.set_cpus(core::CpuSet::from_core_mask(1ULL << 6U));
    NX_CHECK(console_cfg.worker_thread.set_name("console-worker").has_value());
    s = console_cfg.to_string();
    NX_CHECK(s.find("warning") != std::string::npos);
    NX_CHECK(s.find("console-worker") != std::string::npos);
    NX_CHECK(to_string(console_cfg) == s); // ADL friend form
    NX_CHECK(std::format("{}", console_cfg) == s);
    ConsoleLogger<>::Config console_back{};
    from_json(nlohmann::json(console_cfg), console_back);
    NX_CHECK(console_back.initial_level == console_cfg.initial_level);
    NX_CHECK(console_back.worker_thread == console_cfg.worker_thread);

    std::printf("config_strings: throttle/rotation/filelogger/dropped/console-config "
                "dumps render every field\n");
}

} // namespace

void suite_enum_vocab() {
    run_suite("enum_vocab", test_enum_vocab);
}

void suite_config_strings() {
    run_suite("config_strings", test_config_strings);
}
