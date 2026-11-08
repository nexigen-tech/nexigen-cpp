// Split from metrics_test.cpp: nexigen::metrics Meter registry and slot budgets.

#include "metrics_test_support.hpp"

namespace {

void test_registry() {
    metrics::NullMetricSink sink;
    metrics::MetricProviderConfig config{};
    config.max_counters = 2;
    config.max_meters = 2;
    metrics::MetricProvider provider{config, sink};

    NX_CHECK(provider.startup_status().has_value());

    // Meter creation and identity.
    const Result<metrics::Meter*> meter = provider.get_meter("engine", "1.2.0");
    NX_CHECK(meter.has_value());
    if (!meter) {
        return;
    }
    NX_CHECK(metrics::detail::view_name((*meter)->identity().meter_name) == "engine");
    NX_CHECK(metrics::detail::view_name((*meter)->identity().meter_version) == "1.2.0");

    // The same name+version returns the same handle; a different version is
    // a conflict.
    const Result<metrics::Meter*> again = provider.get_meter("engine", "1.2.0");
    NX_CHECK(again.has_value() && (*again) == *meter);
    const Result<metrics::Meter*> conflict = provider.get_meter("engine", "2.0.0");
    NX_CHECK(!conflict.has_value());
    NX_CHECK(conflict.error() == ErrorCode::InvalidArgument);

    // Invalid names are rejected.
    const Result<metrics::Meter*> empty = provider.get_meter("", "1.0");
    NX_CHECK(!empty.has_value() && empty.error() == ErrorCode::InvalidArgument);
    const Result<metrics::Meter*> too_long = provider.get_meter(std::string(64, 'x'), "1.0");
    NX_CHECK(!too_long.has_value() && too_long.error() == ErrorCode::InvalidArgument);

    // Instrument creation through the meter.
    const Result<
        metrics::Counter<metrics::ConcurrencyPolicy::SingleThreaded,
                         metrics::AccessPolicy::Readable, metrics::ResetPolicy::Cumulative>*>
        counter = (*meter)->create_counter("hits");
    NX_CHECK(counter.has_value());
    if (counter) {
        (*counter)->inc();
        NX_CHECK((*counter)->read() == 1U);
    }

    // Duplicate names inside one meter are rejected (across kinds too).
    const Result<metrics::Gauge<std::int64_t, metrics::ConcurrencyPolicy::SingleThreaded,
                                metrics::AccessPolicy::Readable, metrics::ResetPolicy::Cumulative>*>
        duplicate = (*meter)->create_gauge("hits");
    NX_CHECK(!duplicate.has_value());
    NX_CHECK(duplicate.error() == ErrorCode::InvalidArgument);

    // Capacity exhaustion is reported, not fatal.
    const Result<
        metrics::Counter<metrics::ConcurrencyPolicy::SingleThreaded,
                         metrics::AccessPolicy::Readable, metrics::ResetPolicy::Cumulative>*>
        second = (*meter)->create_counter("hits2");
    NX_CHECK(second.has_value());
    const Result<
        metrics::Counter<metrics::ConcurrencyPolicy::SingleThreaded,
                         metrics::AccessPolicy::Readable, metrics::ResetPolicy::Cumulative>*>
        third = (*meter)->create_counter("hits3");
    NX_CHECK(!third.has_value());
    NX_CHECK(third.error() == ErrorCode::BufferFull);

    // Storage is cache-line aligned: the registry base and the slot
    // addresses are multiples of 64 bytes.
    const auto address = reinterpret_cast<std::uintptr_t>(*counter);
    NX_CHECK((address % 64U) == 0U);

    // The provider renders diagnostics.
    NX_CHECK(!provider.to_string().empty());
    NX_CHECK(metrics::to_string(config) == config.to_string());
}

// --- counters (spec metrics/counter-gauge).

void test_thread_slots_affinity() {
    constexpr int probe_threads = 33;
    std::vector<int> counter_slots(probe_threads, -1);
    std::vector<int> gauge_slots(probe_threads, -1);
    {
        std::vector<std::jthread> probes;
        probes.reserve(probe_threads);
        for (int t = 0; t < probe_threads; ++t) {
            probes.emplace_back([t, &counter_slots, &gauge_slots] {
                counter_slots[static_cast<std::size_t>(t)] =
                    static_cast<int>(metrics::detail::thread_slot(metrics::MetricKind::Counter));
                gauge_slots[static_cast<std::size_t>(t)] =
                    static_cast<int>(metrics::detail::thread_slot(metrics::MetricKind::Gauge));
            });
        }
    } // joined
    // 32 exclusive slots below the pool size; exactly one thread lands on the
    // shared fallback marker. Slots are assigned in first-call order, which is
    // NOT thread-spawn order — the assertions are order-independent.
    for (int kind_pass = 0; kind_pass < 2; ++kind_pass) {
        const std::vector<int>& slots = (kind_pass == 0) ? counter_slots : gauge_slots;
        std::vector<bool> seen(32, false);
        int fallback_count = 0;
        for (int t = 0; t < probe_threads; ++t) {
            const int slot = slots[static_cast<std::size_t>(t)];
            NX_CHECK(slot >= 0 && slot <= 32);
            if (slot == 32) {
                ++fallback_count;
            } else {
                seen[static_cast<std::size_t>(slot)] = true;
            }
        }
        NX_CHECK(fallback_count == 1);
        NX_CHECK(std::all_of(seen.begin(), seen.end(), [](bool v) { return v; }));
    }

    // Affinity round-trip on a probe thread (spec metrics/metric-provider
    // "CPU core affinity").
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    NX_CHECK(::sched_getaffinity(0, sizeof(allowed), &allowed) == 0);
    int first_cpu = -1;
    int denied_cpu = -1;
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
    std::atomic<int> affinity_result{0};
    std::thread probe([&] {
        const std::uint64_t mask = std::uint64_t{1} << first_cpu;
        const Status pinned = metrics::detail::pin_self(mask);
        std::uint64_t read_back = 0;
        const Status queried = metrics::detail::self_affinity(read_back);
        affinity_result.store(
            (pinned.has_value() && queried.has_value() && read_back == mask) ? 1 : -1);
    });
    probe.join();
    NX_CHECK(affinity_result.load() == 1);
    NX_CHECK(!metrics::detail::pin_self(0U).has_value()); // zero mask: InvalidArgument
    if (denied_cpu >= 0) {
        const Status denied = metrics::detail::pin_self(std::uint64_t{1} << denied_cpu);
        NX_CHECK(!denied.has_value());
        NX_CHECK(denied.error() == ErrorCode::SystemError);
    }
}

// Factory-only types must not be deserializable: the concept keeps the
// from_json call in the immediate context so a missing overload is a
// substitution failure rather than a hard error.

void test_slot_exhaustion() {
    metrics::NullMetricSink sink;
    metrics::MetricProvider provider{metrics::MetricProviderConfig{}, sink};
    const Result<metrics::Meter*> meter = provider.get_meter("exhaust", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter) {
        return;
    }
    const auto stats = (*meter)->create_statistics<MT, Readable, Cumulative>("dropped");
    NX_CHECK(stats.has_value());
    if (!stats) {
        return;
    }
    constexpr int threads = 33;
    constexpr int per_thread = 100;
    {
        std::vector<std::jthread> pool;
        pool.reserve(threads);
        for (int t = 0; t < threads; ++t) {
            pool.emplace_back([&stats] {
                for (int i = 0; i < per_thread; ++i) {
                    (*stats)->record(1.0);
                }
            });
        }
    } // joined
    const metrics::StatisticsSnapshot snapshot = (*stats)->read();
    // Every thread beyond the exclusive pool is dropped and counted; the
    // accepted (here: none) moments stay exact.
    NX_CHECK(snapshot.dropped_samples == static_cast<std::uint64_t>(threads * per_thread));
    NX_CHECK(snapshot.count == 0U);
    NX_CHECK(snapshot.min == 0.0 && snapshot.max == 0.0 && snapshot.mean == 0.0);
}

// --- Registry: zero-allocation creation and harvest, contiguous traversal
// (spec metrics/metric-provider "Pre-allocated aligned metric registry";
// task 10.1).
void test_registry_zero_alloc() {
    RecordingSink recorder;
    metrics::MetricProviderConfig config{};
    config.max_meters = 2;
    config.max_counters = 8;
    config.max_gauges = 4;
    config.max_statistics = 2;
    config.max_explicit_histograms = 2;
    config.max_exponential_histograms = 1;
    metrics::MetricProvider provider{config, recorder};
    const Result<metrics::Meter*> meter = provider.get_meter("alloc", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter) {
        return;
    }

    // Armed arena: instrument creation must not allocate.
    const std::size_t armed = g_alloc_used;
    const std::size_t heap_armed = heap_in_use();
    for (int i = 0; i < 8; ++i) {
        const auto created =
            (*meter)->create_counter<MT, Readable, Cumulative>("c" + std::to_string(i));
        NX_CHECK(created.has_value());
        if (created) {
            (*created)->add(static_cast<std::uint64_t>(i + 1));
        }
    }
    const auto gauge = (*meter)->create_gauge<double, MT, Readable, Cumulative>("g");
    const auto stats = (*meter)->create_statistics<MT, Readable, Cumulative>("s");
    const auto hist = (*meter)->create_explicit_histogram<MT, Readable, Cumulative>(
        "h", std::span<const double>{std::array<double, 2>{1.0, 2.0}});
    const auto expo = (*meter)->create_exponential_histogram<MT, Readable, Cumulative>("x", 8);
    NX_CHECK(gauge.has_value() && stats.has_value() && hist.has_value() && expo.has_value());
    NX_CHECK(g_alloc_used == armed);
    NX_CHECK(heap_in_use() == heap_armed);

    // Harvesting is allocation-free and contiguous traversal visits every
    // registered metric in one batch.
    const Status collected = provider.collect_now();
    NX_CHECK(collected.has_value());
    NX_CHECK(recorder.batches() == 1U);
    NX_CHECK(recorder.records() == 12U); // 8 counters + one instrument per other kind
    NX_CHECK(g_alloc_used == armed);
    NX_CHECK(heap_in_use() == heap_armed);

    // Repeated harvests stay allocation-free.
    for (int i = 0; i < 8; ++i) {
        static_cast<void>(provider.collect_now());
    }
    NX_CHECK(g_alloc_used == armed);
    NX_CHECK(heap_in_use() == heap_armed);
}

// --- Provider configuration and lifecycle (spec metrics/metric-provider
// "Provider configuration serialization" + liveness; task 10.3).

void test_registration_race() {
    metrics::NullMetricSink sink;
    metrics::MetricProviderConfig config{};
    config.collect_interval = std::chrono::nanoseconds{1'000'000}; // 1 ms
    config.max_counters = 32;
    metrics::MetricProvider provider{config, sink};
    NX_CHECK(provider.startup_status().has_value());
    const Result<metrics::Meter*> meter = provider.get_meter("reg", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter) {
        return;
    }

    // Create every instrument kind while the background harvester runs.
    using CounterType = metrics::Counter<MT, Readable, Cumulative>;
    std::vector<CounterType*> counters;
    std::uint64_t expected = 0;
    for (int i = 0; i < 16; ++i) {
        const Result<CounterType*> counter =
            (*meter)->create_counter<MT, Readable, Cumulative>("c" + std::to_string(i));
        NX_CHECK(counter.has_value());
        if (counter) {
            (*counter)->add(static_cast<std::uint64_t>(i));
            expected += static_cast<std::uint64_t>(i);
            counters.push_back(*counter);
        }
        std::this_thread::sleep_for(std::chrono::microseconds{200});
    }
    const auto gauge = (*meter)->create_gauge<std::int64_t, MT, Readable, Cumulative>("g");
    const auto stats = (*meter)->create_statistics<MT, Readable, Cumulative>("s");
    const auto explicit_hist = (*meter)->create_explicit_histogram<MT, Readable, Cumulative>(
        "e", std::span<const double>{std::array<double, 3>{1.0, 2.0, 4.0}});
    const auto exponential_hist =
        (*meter)->create_exponential_histogram<MT, Readable, Cumulative>("x", 8);
    NX_CHECK(gauge.has_value() && stats.has_value() && explicit_hist.has_value() &&
             exponential_hist.has_value());
    if (!gauge || !stats || !explicit_hist || !exponential_hist) {
        return;
    }
    (*gauge)->add(7);
    (*stats)->record(4.0);
    (*explicit_hist)->record(1.5);
    (*exponential_hist)->record(3.0);

    // Let the harvester sweep the registry repeatedly, then verify totals.
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    std::uint64_t total = 0;
    for (const CounterType* counter : counters) {
        total += counter->read();
    }
    NX_CHECK(total == expected);
    NX_CHECK((*gauge)->read() == 7);
    NX_CHECK((*stats)->read().count == 1U);
    NX_CHECK((*explicit_hist)->read().count == 1U);
    NX_CHECK((*exponential_hist)->read().count == 1U);
}

// --- extended sink/writer coverage (change improve-test-coverage):
// every MetricKind, escaping classes, numeric boundaries, batch edge cases.

} // namespace

void suite_registry() {
    run_suite("registry", test_registry);
}

void suite_thread_slots_affinity() {
    run_suite("thread_slots_affinity", test_thread_slots_affinity);
}

void suite_slot_exhaustion() {
    run_suite("slot_exhaustion", test_slot_exhaustion);
}

void suite_registry_zero_alloc() {
    run_suite("registry_zero_alloc", test_registry_zero_alloc);
}

void suite_registration_race() {
    run_suite("registration_race", test_registration_race);
}
