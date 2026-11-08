// Split from logging_test.cpp: nexigen::logging worker thread settings and idle tuning.

#include "logging_test_support.hpp"

namespace {

void test_logger_required_thread_failure() {
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    char base[512];
    std::snprintf(base, sizeof base, "/tmp/opencode/nexigen-required-%d", ::getpid());

    static FileLogger<128, 128> file_logger{};
    FileLoggerConfig cfg{};
    cfg.base_dir = base;
    cfg.rotation.kind = RotationSchedule::None;
    NX_CHECK(cfg.worker_thread.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
    cfg.worker_thread.set_apply_mode(core::ThreadApplyMode::Required);
    nexigen::core::fault_injection::set_fault_hook(logger_fail_scheduler);
    const Status file_failed = file_logger.initialize(cfg);
    nexigen::core::fault_injection::set_fault_hook(nullptr);
    NX_CHECK(!file_failed.has_value());
    NX_CHECK(file_failed.error() == ErrorCode::SystemError);
    NX_CHECK(!file_logger.writer_started());
    NX_CHECK(file_logger.flush().error() == ErrorCode::NotInitialized);

    // The same object is safely reusable after the joined/reset failure.
    cfg.worker_thread.clear_scheduler();
    cfg.worker_thread.set_apply_mode(core::ThreadApplyMode::BestEffort);
    NX_CHECK(file_logger.initialize(cfg).has_value());
    for (int i = 0; i < 5000 && !file_logger.writer_started(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NX_CHECK(file_logger.writer_started());
    file_logger.info("after required rollback");
    NX_CHECK(file_logger.flush().has_value());

    // Best effort keeps the worker alive and exposes the partial application.
    static FileLogger<128, 128> partial_logger{};
    FileLoggerConfig partial_cfg = cfg;
    partial_cfg.file_name = "partial.jsonl";
    NX_CHECK(partial_cfg.worker_thread.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
    partial_cfg.worker_thread.set_apply_mode(core::ThreadApplyMode::BestEffort);
    nexigen::core::fault_injection::set_fault_hook(logger_fail_scheduler);
    NX_CHECK(partial_logger.initialize(partial_cfg).has_value());
    for (int i = 0; i < 5000 && !partial_logger.writer_started(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    nexigen::core::fault_injection::set_fault_hook(nullptr);
    NX_CHECK(partial_logger.writer_started());
    NX_CHECK(partial_logger.writer_start_code() == ErrorCode::SystemError);
    NX_CHECK(partial_logger.writer_thread_report().failed() ==
             core::ThreadApplyReport::scheduler_mask);
    NX_CHECK(partial_logger.flush().has_value());

    static ConsoleLogger<32, 128> console_logger{};
    ConsoleLoggerConfig console_cfg{};
    NX_CHECK(console_cfg.worker_thread.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
    console_cfg.worker_thread.set_apply_mode(core::ThreadApplyMode::Required);
    nexigen::core::fault_injection::set_fault_hook(logger_fail_scheduler);
    const Status console_failed = console_logger.initialize(console_cfg);
    nexigen::core::fault_injection::set_fault_hook(nullptr);
    NX_CHECK(!console_failed.has_value());
    NX_CHECK(console_failed.error() == ErrorCode::SystemError);
    NX_CHECK(!console_logger.writer_started());
    NX_CHECK(console_logger.flush().error() == ErrorCode::NotInitialized);

    console_cfg.worker_thread.clear_scheduler();
    console_cfg.worker_thread.set_apply_mode(core::ThreadApplyMode::BestEffort);
    NX_CHECK(console_logger.initialize(console_cfg).has_value());
    for (int i = 0; i < 5000 && !console_logger.writer_started(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NX_CHECK(console_logger.writer_started());
    NX_CHECK(console_logger.flush().has_value());
#else
    std::printf("logger_required_thread_failure: fault injection disabled; skipped\n");
#endif
}

void test_worker_idle_tuning() {
    char base[512];
    std::snprintf(base, sizeof base, "/tmp/opencode/nexigen-idle-%d", ::getpid());

    // Default (1 ms ceiling + coalescing slack): well below the former
    // 200 us poll (~2000 wake-ups per 500 ms).
    {
        static FileLogger<128, 128> logger{};
        FileLoggerConfig cfg{};
        cfg.base_dir = base;
        cfg.rotation.kind = RotationSchedule::None;
        const long wakeups = idle_wakeups(logger, cfg, 500);
        std::printf("worker_idle_tuning: default poll -> %ld wake-ups / 500 ms\n", wakeups);
        NX_CHECK(wakeups >= 0 && wakeups < 1200);
    }

    // Raised ceiling: ~30 wake-ups/s expected; flush() still completes and
    // the record is durable (flush is answered at the next wake-up).
    {
        static FileLogger<128, 128> logger{};
        FileLoggerConfig cfg{};
        cfg.base_dir = base;
        cfg.file_name = "tuned.jsonl";
        cfg.rotation.kind = RotationSchedule::None;
        cfg.worker_poll_interval_us = 20'000U;
        const long wakeups = idle_wakeups(logger, cfg, 500);
        std::printf("worker_idle_tuning: 20 ms poll -> %ld wake-ups / 500 ms\n", wakeups);
        NX_CHECK(wakeups >= 0 && wakeups < 120);

        logger.info("tuned record {}", 7);
        const auto t0 = std::chrono::steady_clock::now();
        NX_CHECK(logger.flush().has_value());
        const auto waited = std::chrono::steady_clock::now() - t0;
        NX_CHECK(waited < std::chrono::seconds(2));
        NX_CHECK(logger.dropped().total() == 0ULL);
    }

    // Out-of-range ceilings are clamped, never rejected or hung.
    {
        static FileLogger<128, 128> logger{};
        FileLoggerConfig cfg{};
        cfg.base_dir = base;
        cfg.file_name = "clamped.jsonl";
        cfg.rotation.kind = RotationSchedule::None;
        cfg.worker_poll_interval_us = 0U;
        NX_CHECK(logger.initialize(cfg).has_value());
        logger.info("clamped record");
        NX_CHECK(logger.flush().has_value());
    }
}

void test_logging_worker_thread_settings() {
    const std::string global_path = unique_path("worker_thread_global");
    const std::string local_path = unique_path("worker_thread_local");
    EnvGuard affinity_env{"NEXIGEN_LOG_CPU_AFFINITY"};
    affinity_env.clear();

    // Legacy file affinity reaches FileLogger but does not implicitly pin Console.
    write_text(global_path, R"({"flush_cpu_affinity":[1,3]})");
    write_text(local_path, "{}");
    {
        Settings out{};
        NX_CHECK(load_logging_settings({global_path, local_path}, out).has_value());
        FileLoggerConfig file_cfg{};
        ConsoleLoggerConfig console_cfg{};
        NX_CHECK(apply_to(out, file_cfg).has_value());
        NX_CHECK(apply_to(out, console_cfg).has_value());
        NX_CHECK(file_cfg.worker_thread.cpus().contains(1U));
        NX_CHECK(file_cfg.worker_thread.cpus().contains(3U));
        NX_CHECK(console_cfg.worker_thread.cpus().empty());
    }

    // Canonical local fields override legacy placement while inheriting the
    // global scheduler/name/mode. No FIFO/RR operation is performed here.
    write_text(global_path, R"({"flush_cpu_affinity":[1,3],)"
                            R"("worker_thread":{"name":"file-worker","policy":"rr",)"
                            R"("priority":40,"mode":"required"}})");
    write_text(local_path, R"({"worker_thread":{"cpus":[64,65]}})");
    Settings merged{};
    NX_CHECK(load_logging_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(merged.options.worker_thread.name() == "file-worker");
    NX_CHECK(merged.options.worker_thread.policy() == core::SchedulingPolicy::RR);
    NX_CHECK(merged.options.worker_thread.priority() == 40);
    NX_CHECK(merged.options.worker_thread.apply_mode() == core::ThreadApplyMode::Required);
    NX_CHECK(merged.options.worker_thread.cpus().count() == 2U);
    NX_CHECK(merged.options.worker_thread.cpus().contains(64U));
    NX_CHECK(merged.options.worker_thread.cpus().contains(65U));
    NX_CHECK(!merged.options.worker_thread.cpus().contains(1U));

    FileLoggerConfig file_cfg{};
    ConsoleLoggerConfig console_cfg{};
    NX_CHECK(apply_to(merged, file_cfg).has_value());
    NX_CHECK(apply_to(merged, console_cfg).has_value());
    NX_CHECK(file_cfg.worker_thread == merged.options.worker_thread);
    NX_CHECK(console_cfg.worker_thread == merged.options.worker_thread);

    // Canonical empty CPUs explicitly clear the lower-priority pin.
    write_text(local_path, R"({"worker_thread":{"cpus":[]}})");
    NX_CHECK(load_logging_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(merged.options.worker_thread.cpus().empty());
    NX_CHECK(merged.options.worker_thread.policy() == core::SchedulingPolicy::RR);

    // The legacy environment override has higher source priority and changes
    // only CPUs, preserving the remaining canonical attributes.
    affinity_env.set("2,70");
    NX_CHECK(load_logging_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(merged.options.worker_thread.cpus().contains(2U));
    NX_CHECK(merged.options.worker_thread.cpus().contains(70U));
    NX_CHECK(merged.options.worker_thread.cpus().count() == 2U);
    NX_CHECK(merged.options.worker_thread.name() == "file-worker");
    affinity_env.clear();

    // Duplicate IDs and invalid scheduling are diagnostics, never truncation.
    write_text(local_path, R"({"worker_thread":{"cpus":[64,64]}})");
    NX_CHECK(!load_logging_settings({global_path, local_path}, merged).has_value());
    write_text(local_path, R"({"worker_thread":{"policy":"fifo","priority":0}})");
    NX_CHECK(!load_logging_settings({global_path, local_path}, merged).has_value());

    static_cast<void>(std::remove(global_path.c_str()));
    static_cast<void>(std::remove(local_path.c_str()));
}

} // namespace

void suite_logger_required_thread_failure() {
    run_suite("logger_required_thread_failure", test_logger_required_thread_failure);
}

void suite_worker_idle_tuning() {
    run_suite("worker_idle_tuning", test_worker_idle_tuning);
}

void suite_logging_worker_thread_settings() {
    run_suite("logging_worker_thread_settings", test_logging_worker_thread_settings);
}
