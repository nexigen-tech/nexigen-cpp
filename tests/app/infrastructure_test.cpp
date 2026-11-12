// Split from app_test.cpp: nexigen::app Infrastructure lifecycle and signal handling.

#include "app_test_support.hpp"

void test_infrastructure_lifecycle() {
    auto app = make_test_infra<TestPolicy>();
    NX_CHECK(app != nullptr);
    if (!app) {
        return;
    }

    NX_CHECK(!app->meter("sim", "1.0").has_value());
    NX_CHECK(app->meter("sim", "1.0").error() == core::ErrorCode::NotInitialized);
    NX_CHECK(!app->tracer_for("sim").has_value());
    NX_CHECK(app->tracer_for("sim").error() == core::ErrorCode::NotInitialized);

    NX_CHECK(app->initialize(app::Settings{}).has_value());
    NX_CHECK(app->initialize(app::Settings{}).has_value()); // idempotent

    g_log_lines.clear();
    app->logger().info("lifecycle record");
    NX_CHECK(g_log_lines.size() == 1U);

    NX_CHECK(app->chrono().snapshot_now().has_value());

    const auto meter = app->meter("sim", "1.0");
    NX_CHECK(meter.has_value());
    const auto counter = (*meter)
                             ->create_counter<metrics::ConcurrencyPolicy::SingleThreaded,
                                              metrics::AccessPolicy::Readable>("ticks");
    NX_CHECK(counter.has_value());
    (*counter)->inc();
    (*counter)->inc();
    NX_CHECK((*counter)->read() == 2ULL);

    const auto tracer = app->tracer_for("sim.control");
    NX_CHECK(tracer.has_value());
    {
        auto span = (*tracer)->start_span("tick", tracing::SpanKind::Internal);
        NX_CHECK(span.set_attribute("n", 1));
    }

    int ticks = 0;
    const app::InfrastructureStopReason reason = app->run([&] {
        ++ticks;
        if (ticks == 3) {
            app->request_stop(app::InfrastructureStopReason::Requested);
        }
    });
    NX_CHECK(reason == app::InfrastructureStopReason::Requested);
    NX_CHECK(ticks == 3);

    NX_CHECK(app->shutdown().has_value());
    NX_CHECK(app->shutdown().has_value()); // idempotent
    int logged = 0;
    for (const std::string& line : g_log_lines) {
        if (line.find("shutdown reason: requested") != std::string::npos) {
            ++logged;
        }
    }
    NX_CHECK(logged == 1);
    NX_CHECK(!app->meter("x", "1").has_value());
}

void test_infrastructure_failures() {
    auto bool_app = make_test_infra<TestPolicy>();
    NX_CHECK(bool_app != nullptr);
    if (bool_app) {
        NX_CHECK(bool_app->initialize(app::Settings{}).has_value());
        NX_CHECK(bool_app->run([] { return false; }) == app::InfrastructureStopReason::Requested);
        NX_CHECK(bool_app->shutdown().has_value());
    }

    auto fail_logger = make_test_infra<FailLoggerPolicy>();
    NX_CHECK(fail_logger != nullptr);
    if (fail_logger) {
        NX_CHECK(!fail_logger->initialize(app::Settings{}).has_value());
        NX_CHECK(fail_logger->shutdown().has_value());
    }
    auto fail_metrics = make_test_infra<FailMetricsPolicy>();
    NX_CHECK(fail_metrics != nullptr);
    if (fail_metrics) {
        NX_CHECK(!fail_metrics->initialize(app::Settings{}).has_value());
        NX_CHECK(fail_metrics->shutdown().has_value());
    }
    auto fail_trace = make_test_infra<FailTracePolicy>();
    NX_CHECK(fail_trace != nullptr);
    if (fail_trace) {
        NX_CHECK(!fail_trace->initialize(app::Settings{}).has_value());
        NX_CHECK(fail_trace->shutdown().has_value());
    }
    auto fail_chrono = make_test_infra<FailChronoPolicy>();
    NX_CHECK(fail_chrono != nullptr);
    if (fail_chrono) {
        NX_CHECK(!fail_chrono->initialize(app::Settings{}).has_value());
        NX_CHECK(fail_chrono->shutdown().has_value());
    }
    auto flush_fail = make_test_infra<FlushFailPolicy>();
    NX_CHECK(flush_fail != nullptr);
    if (flush_fail) {
        NX_CHECK(flush_fail->initialize(app::Settings{}).has_value());
        const core::Status status = flush_fail->shutdown();
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::SystemError);
    }
}

void test_infrastructure_required_thread_rollback() {
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    NX_CHECK(::sched_getaffinity(0, sizeof(allowed), &allowed) == 0);
    int first_cpu = -1;
    for (int cpu = 0; cpu < 64 && first_cpu < 0; ++cpu) {
        if (CPU_ISSET(static_cast<std::size_t>(cpu), &allowed)) {
            first_cpu = cpu;
        }
    }
    NX_CHECK(first_cpu >= 0);

    auto settings_for = [&](int failed_worker) {
        app::Settings settings{};
        settings.app_name = "rt-test";
        settings.logging.options.log_root_directory = "/tmp/opencode/app-rt-logs";
        auto configure = [&](core::ThreadSpec& spec) {
            spec.set_cpus(core::CpuSet::from_core_mask(std::uint64_t{1} << first_cpu));
            spec.set_apply_mode(core::ThreadApplyMode::Required);
        };
        if (failed_worker == 0) {
            configure(settings.logging.options.worker_thread);
        } else if (failed_worker == 1) {
            settings.chrono.options.collect_interval = std::chrono::nanoseconds{1'000'000};
            configure(settings.chrono.options.worker_thread);
        } else if (failed_worker == 2) {
            settings.metrics.options.collect_interval = std::chrono::nanoseconds{1'000'000};
            configure(settings.metrics.options.worker_thread);
        } else {
            configure(settings.tracing.options.worker_thread);
        }
        return settings;
    };

    for (int failed_worker = 0; failed_worker < 4; ++failed_worker) {
        auto infra = make_test_infra<RtPolicy>();
        NX_CHECK(infra != nullptr);
        if (!infra) {
            continue;
        }
        app::Settings settings = settings_for(failed_worker);
        auto fail_affinity = [](nexigen::core::fault_injection::FaultPoint point) noexcept {
            return point == nexigen::core::fault_injection::FaultPoint::Thread_set_affinity;
        };
        nexigen::core::fault_injection::set_fault_hook(fail_affinity);
        const core::Status failed = infra->initialize(settings);
        nexigen::core::fault_injection::set_fault_hook(nullptr);
        NX_CHECK(!failed.has_value());
        NX_CHECK(failed.error() == core::ErrorCode::SystemError);
        NX_CHECK(infra->shutdown().has_value()); // rollback is idempotent

        // Signal ownership and component state were released, so the same
        // settings succeed once the injected error is removed.
        NX_CHECK(infra->initialize(settings).has_value());
        NX_CHECK(infra->shutdown().has_value());
    }

    // Explicit best effort does not roll back partial startup.
    {
        auto infra = make_test_infra<RtPolicy>();
        NX_CHECK(infra != nullptr);
        if (infra) {
            app::Settings settings{};
            settings.app_name = "best-effort-test";
            settings.logging.options.log_root_directory = "/tmp/opencode/app-rt-logs";
            settings.logging.options.worker_thread.set_cpus(
                core::CpuSet::from_core_mask(std::uint64_t{1} << first_cpu));
            auto fail_affinity = [](nexigen::core::fault_injection::FaultPoint point) noexcept {
                return point == nexigen::core::fault_injection::FaultPoint::Thread_set_affinity;
            };
            nexigen::core::fault_injection::set_fault_hook(fail_affinity);
            NX_CHECK(infra->initialize(settings).has_value());
            nexigen::core::fault_injection::set_fault_hook(nullptr);
            NX_CHECK(infra->shutdown().has_value());
        }
    }
#else
    std::printf("infrastructure_required_thread_rollback: fault injection disabled; skipped\n");
#endif
}

void test_infrastructure_signals() {
    auto int_app = make_test_infra<TestPolicy>();
    NX_CHECK(int_app != nullptr);
    if (!int_app) {
        return;
    }
    NX_CHECK(int_app->initialize(app::Settings{}).has_value());
    g_log_lines.clear();
    std::atomic<bool> running{false};
    std::jthread raiser{[&running] {
        while (!running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        static_cast<void>(::kill(::getpid(), SIGINT));
    }};
    const app::InfrastructureStopReason reason = int_app->run([&running] {
        running.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    });
    NX_CHECK(reason == app::InfrastructureStopReason::Sigint);
    NX_CHECK(int_app->shutdown().has_value());
    raiser.join();
    int logged = 0;
    for (const std::string& line : g_log_lines) {
        if (line.find("shutdown reason: sigint (2)") != std::string::npos) {
            ++logged;
        }
    }
    NX_CHECK(logged == 1);

    auto term_app = make_test_infra<TestPolicy>();
    NX_CHECK(term_app != nullptr);
    if (!term_app) {
        return;
    }
    NX_CHECK(term_app->initialize(app::Settings{}).has_value());
    std::atomic<bool> term_running{false};
    std::jthread term_raiser{[&term_running] {
        while (!term_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        static_cast<void>(::kill(::getpid(), SIGTERM));
    }};
    const app::InfrastructureStopReason term_reason = term_app->run([&term_running] {
        term_running.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    });
    NX_CHECK(term_reason == app::InfrastructureStopReason::Sigterm);
    NX_CHECK(term_app->shutdown().has_value());
    term_raiser.join();
}

void test_infrastructure_tick_allocation() {
    auto infra = make_test_infra<TestPolicy>();
    NX_CHECK(infra != nullptr);
    if (!infra) {
        return;
    }
    NX_CHECK(infra->initialize(app::Settings{}).has_value());
    int ticks = 0;
    [[maybe_unused]] const std::size_t before = ::mallinfo2().uordblks;
    const app::InfrastructureStopReason reason = infra->run([&] {
        ++ticks;
        if (ticks == 1000) {
            infra->request_stop(app::InfrastructureStopReason::Requested);
        }
    });
    [[maybe_unused]] const std::size_t after = ::mallinfo2().uordblks;
    NX_CHECK(reason == app::InfrastructureStopReason::Requested);
#ifndef NX_SANITIZER_BUILD
    NX_CHECK(after == before);
#endif
    NX_CHECK(infra->shutdown().has_value());
}

void test_watcher_empty_spec_inherits() {
    sigset_t previous{};
    sigset_t blocked{};
    ::sigemptyset(&previous);
    ::sigemptyset(&blocked);
    ::sigaddset(&blocked, SIGUSR2);
    NX_CHECK(::pthread_sigmask(SIG_BLOCK, &blocked, &previous) == 0);

    struct WatchTarget {
        void request_stop(app::InfrastructureStopReason) const noexcept {}
    };
    std::uint64_t inherited = 0U;
    NX_CHECK(core::affinity::self_affinity(inherited).has_value());
    app::detail::SignalWatcher watcher{};
    const core::Status started = watcher.start<WatchTarget>(nullptr, blocked, core::ThreadSpec{});
    NX_CHECK(started.has_value());
    if (started) {
        for (int i = 0; i < 2000 && !watcher.thread_ready(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(watcher.thread_ready());
        NX_CHECK(watcher.thread_report().requested() == 0U);
        NX_CHECK(watcher.thread_mask() == inherited);
        watcher.stop();
    }
    NX_CHECK(::pthread_sigmask(SIG_SETMASK, &previous, nullptr) == 0);
}

void test_watcher_required_failure() {
    sigset_t previous{};
    sigset_t blocked{};
    ::sigemptyset(&previous);
    ::sigemptyset(&blocked);
    ::sigaddset(&blocked, SIGUSR2);
    NX_CHECK(::pthread_sigmask(SIG_BLOCK, &blocked, &previous) == 0);

    struct WatchTarget {
        void request_stop(app::InfrastructureStopReason) const noexcept {}
    };
    core::ThreadSpec failing_spec{};
    NX_CHECK(failing_spec.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
    failing_spec.set_apply_mode(core::ThreadApplyMode::Required);
    if (!core::probe_thread_spec(failing_spec).has_value()) {
        const auto count_fds = [] {
            std::size_t count = 0U;
            for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
                static_cast<void>(entry);
                ++count;
            }
            return count;
        };
        const std::size_t fds_before = count_fds();
        app::detail::SignalWatcher failing_watcher{};
        const core::Status failed =
            failing_watcher.start<WatchTarget>(nullptr, blocked, failing_spec);
        NX_CHECK(!failed.has_value());
        NX_CHECK(!failing_watcher.active());
        NX_CHECK(count_fds() == fds_before);
    }
    NX_CHECK(::pthread_sigmask(SIG_SETMASK, &previous, nullptr) == 0);
}

void test_infrastructure_conflict() {
    // SignalWatcher double-start guard (analysis #22; change
    // fix-p2-observability-and-writers): a second start on the same
    // watcher returns Conflict and installs no extra signalfd — the
    // previous implementation leaked an fd and a thread from every retry
    // (reachable through the detail API, not through Infrastructure).
    {
        sigset_t previous{};
        sigset_t blocked{};
        ::sigemptyset(&previous);
        ::sigemptyset(&blocked);
        ::sigaddset(&blocked, SIGUSR2);
        NX_CHECK(::pthread_sigmask(SIG_BLOCK, &blocked, &previous) == 0);
        std::size_t fds_with_watcher = 0;
        app::detail::SignalWatcher watcher_probe{};
        // The watcher never fires (SIGUSR2 stays blocked, nothing raises
        // it); the target stub only answers the type requirement.
        struct WatchTarget {
            void request_stop(app::InfrastructureStopReason) const noexcept {}
        };
        core::ThreadSpec watcher_spec{};
        NX_CHECK(watcher_spec.set_name("sig-watch").has_value());
        const core::Status started =
            watcher_probe.start<WatchTarget>(nullptr, blocked, watcher_spec);
        NX_CHECK(started.has_value());
        if (started) {
            for (int i = 0; i < 2000 && !watcher_probe.thread_ready(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            NX_CHECK(watcher_probe.thread_ready());
            NX_CHECK((watcher_probe.thread_report().applied() &
                      core::ThreadApplyReport::name_mask) != 0U);
            for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
                static_cast<void>(entry);
                ++fds_with_watcher;
            }
            NX_CHECK(fds_with_watcher >= 1U);
            const core::Status second_start =
                watcher_probe.start<WatchTarget>(nullptr, blocked, core::ThreadSpec{});
            NX_CHECK(!second_start.has_value());
            NX_CHECK(second_start.error() == core::ErrorCode::Conflict);
            std::size_t fds_after_conflict = 0;
            for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
                static_cast<void>(entry);
                ++fds_after_conflict;
            }
            NX_CHECK(fds_after_conflict == fds_with_watcher);
            NX_CHECK(watcher_probe.active());
            watcher_probe.stop();
        }
        NX_CHECK(::pthread_sigmask(SIG_SETMASK, &previous, nullptr) == 0);
    }
    auto first = make_test_infra<TestPolicy>();
    auto second = make_test_infra<TestPolicy>();
    NX_CHECK(first != nullptr && second != nullptr);
    if (!first || !second) {
        return;
    }
    NX_CHECK(first->initialize(app::Settings{}).has_value());
    const core::Status conflict = second->initialize(app::Settings{});
    NX_CHECK(!conflict.has_value());
    NX_CHECK(conflict.error() == core::ErrorCode::Conflict);
    NX_CHECK(first->shutdown().has_value());
    NX_CHECK(second->initialize(app::Settings{}).has_value());
    NX_CHECK(second->shutdown().has_value());

    // A failed initialization releases ownership for the next infrastructure.
    auto failing = make_test_infra<FailLoggerPolicy>();
    NX_CHECK(failing != nullptr);
    if (failing) {
        NX_CHECK(!failing->initialize(app::Settings{}).has_value());
        auto after_failure = make_test_infra<TestPolicy>();
        NX_CHECK(after_failure != nullptr);
        if (after_failure) {
            NX_CHECK(after_failure->initialize(app::Settings{}).has_value());
            NX_CHECK(after_failure->shutdown().has_value());
        }
    }

    // Different policies still conflict on the process-global guard.
    auto trace_first = make_test_infra<TestPolicy>();
    auto trace_second = make_test_infra<FailTracePolicy>();
    NX_CHECK(trace_first != nullptr && trace_second != nullptr);
    if (trace_first && trace_second) {
        NX_CHECK(trace_first->initialize(app::Settings{}).has_value());
        const core::Status policy_conflict = trace_second->initialize(app::Settings{});
        NX_CHECK(!policy_conflict.has_value());
        NX_CHECK(policy_conflict.error() == core::ErrorCode::Conflict);
        NX_CHECK(trace_first->shutdown().has_value());
        NX_CHECK(trace_second->shutdown().has_value());
    }
}

void test_infrastructure_stop_and_mask() {
    {
        auto first = make_test_infra<TestPolicy>();
        NX_CHECK(first != nullptr);
        if (first) {
            NX_CHECK(first->initialize(app::Settings{}).has_value());
            first->request_stop(app::InfrastructureStopReason::None);
            NX_CHECK(!first->stop_requested());
            first->request_stop(app::InfrastructureStopReason::Requested);
            first->request_stop(app::InfrastructureStopReason::Sigint);
            NX_CHECK(first->stop_reason() == app::InfrastructureStopReason::Requested);
            NX_CHECK(first->shutdown().has_value());
        }
    }
    {
        auto second = make_test_infra<TestPolicy>();
        NX_CHECK(second != nullptr);
        if (second) {
            NX_CHECK(second->initialize(app::Settings{}).has_value());
            second->request_stop(app::InfrastructureStopReason::Sigint);
            second->request_stop(app::InfrastructureStopReason::Requested);
            NX_CHECK(second->stop_reason() == app::InfrastructureStopReason::Sigint);
            NX_CHECK(second->shutdown().has_value());
        }
    }

    auto masked = make_test_infra<TestPolicy>();
    NX_CHECK(masked != nullptr);
    if (!masked) {
        return;
    }
    NX_CHECK(masked->initialize(app::Settings{}).has_value());
    std::atomic<bool> both_blocked{false};
    std::jthread probe{[&] {
        sigset_t current;
        sigemptyset(&current);
        if (::pthread_sigmask(SIG_BLOCK, nullptr, &current) == 0) {
            both_blocked.store(::sigismember(&current, SIGINT) == 1 &&
                                   ::sigismember(&current, SIGTERM) == 1,
                               std::memory_order_release);
        }
    }};
    probe.join();
    NX_CHECK(both_blocked.load(std::memory_order_acquire));
    NX_CHECK(masked->shutdown().has_value());
}

void run_app_infrastructure_suites() {
    run_suite("lifecycle", test_infrastructure_lifecycle);
    run_suite("failures", test_infrastructure_failures);
    run_suite("required_thread_rollback", test_infrastructure_required_thread_rollback);
    run_suite("conflict", test_infrastructure_conflict);
    run_suite("watcher_empty_spec", test_watcher_empty_spec_inherits);
    run_suite("watcher_required_failure", test_watcher_required_failure);
    run_suite("stop_and_mask", test_infrastructure_stop_and_mask);
    run_suite("signals", test_infrastructure_signals);
    run_suite("tick_allocation", test_infrastructure_tick_allocation);
}
