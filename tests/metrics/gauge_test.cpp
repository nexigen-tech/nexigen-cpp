// Split from metrics_test.cpp: nexigen::metrics Gauge.

#include "metrics_test_support.hpp"

namespace {

void test_gauge() {
    metrics::NullMetricSink sink;
    metrics::MetricProvider provider{metrics::MetricProviderConfig{}, sink};
    const Result<metrics::Meter*> meter = provider.get_meter("engine", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter) {
        return;
    }

    // Single-threaded int64 and double gauges.
    const auto int_gauge = (*meter)->create_gauge<std::int64_t, ST, Readable, Cumulative>("level");
    NX_CHECK(int_gauge.has_value());
    if (!int_gauge) {
        return;
    }
    (*int_gauge)->set(10);
    (*int_gauge)->add(5);
    (*int_gauge)->sub(3);
    NX_CHECK((*int_gauge)->read() == 12);

    const auto double_gauge = (*meter)->create_gauge<double, ST, Readable, Cumulative>("ratio");
    NX_CHECK(double_gauge.has_value());
    if (!double_gauge) {
        return;
    }
    (*double_gauge)->set(1.5);
    (*double_gauge)->add(0.25);
    (*double_gauge)->sub(0.75);
    NX_CHECK((*double_gauge)->read() == 1.0);

    // set() moves the aggregate to the target value.
    const auto sharded = (*meter)->create_gauge<std::int64_t, MT, Readable, Cumulative>("sharded");
    NX_CHECK(sharded.has_value());
    if (!sharded) {
        return;
    }
    (*sharded)->set(42);
    NX_CHECK((*sharded)->read() == 42);

    // Concurrent adds stay exact (32-thread additive exactness).
    std::vector<std::jthread> workers;
    workers.reserve(32);
    for (int t = 0; t < 32; ++t) {
        workers.emplace_back([&sharded] {
            for (int i = 0; i < 100'000; ++i) {
                (*sharded)->add(1);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    NX_CHECK((*sharded)->read() == 42 + 3'200'000);

    // set() under racing adds (analysis #19 rework): the reassigned target
    // lands exactly for every set that completes after adds stop, and a
    // settled final set is exact — the former observe+add delta produced
    // stale-scaled targets under this race (TSan verdict in 8.3).
    {
        std::atomic<bool> stop{false};
        std::vector<std::jthread> racers;
        racers.reserve(4);
        (*sharded)->set(1000);
        for (int t = 0; t < 4; ++t) {
            racers.emplace_back([&] {
                while (!stop.load(std::memory_order_relaxed)) {
                    (*sharded)->add(1);
                }
            });
        }
        for (int i = 0; i < 2'000; ++i) {
            (*sharded)->set(static_cast<std::int64_t>(1'000'000 + i));
        }
        stop.store(true, std::memory_order_release);
        auto joins = std::move(racers);
        for (auto& racer : joins) {
            racer.join();
        }
        // The final set lands exactly (no adds racing it anymore).
        (*sharded)->set(777);
        NX_CHECK((*sharded)->read() == 777);
    }

    // Reset-on-read reports the window activity.
    const auto windowed =
        (*meter)->create_gauge<std::int64_t, ST, Readable, ResetOnRead>("windowed");
    NX_CHECK(windowed.has_value());
    if (!windowed) {
        return;
    }
    (*windowed)->add(5);
    NX_CHECK((*windowed)->read() == 5);
    (*windowed)->add(3);
    NX_CHECK((*windowed)->read() == 3);

    // The serialization surface.
    const std::string text = (*int_gauge)->to_string();
    NX_CHECK(!text.empty());
    NX_CHECK(metrics::to_string(**int_gauge) == text);
    NX_CHECK(std::format("{}", **int_gauge) == text);
    nlohmann::json j = **int_gauge;
    NX_CHECK(j.is_object());
}

// --- state gauge (spec metrics/state-gauge; change add-state-gauge-metric).

} // namespace

void suite_gauge() {
    run_suite("gauge", test_gauge);
}
