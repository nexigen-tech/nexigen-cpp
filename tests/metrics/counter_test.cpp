// Split from metrics_test.cpp: nexigen::metrics Counter.

#include "metrics_test_support.hpp"

namespace {

void test_counter() {
    metrics::NullMetricSink sink;
    metrics::MetricProvider provider{metrics::MetricProviderConfig{}, sink};
    const Result<metrics::Meter*> meter = provider.get_meter("engine", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter) {
        return;
    }

    // Single-threaded accumulation.
    const auto counter = (*meter)->create_counter<ST, Readable, Cumulative>("hits");
    NX_CHECK(counter.has_value());
    if (!counter) {
        return;
    }
    (*counter)->inc();
    (*counter)->inc();
    (*counter)->add(7);
    NX_CHECK((*counter)->read() == 9U);
    // Cumulative totals persist across reads.
    NX_CHECK((*counter)->read() == 9U);

    // The snapshot carries the identity, the kind and the value.
    const metrics::MetricRecord record = (*counter)->snapshot();
    NX_CHECK(record.kind == metrics::MetricKind::Counter);
    NX_CHECK(record.scalar.counter == 9U);
    NX_CHECK(metrics::detail::view_name(record.identity.metric_name) == "hits");
    NX_CHECK(metrics::detail::view_name(record.identity.meter_name) == "engine");
    NX_CHECK(record.concurrency == ST);
    NX_CHECK(record.reset == Cumulative);

    // Reset-on-read reports window deltas.
    const auto windowed = (*meter)->create_counter<ST, Readable, ResetOnRead>("windowed");
    NX_CHECK(windowed.has_value());
    if (!windowed) {
        return;
    }
    (*windowed)->add(5);
    NX_CHECK((*windowed)->read() == 5U);
    NX_CHECK((*windowed)->read() == 0U);
    (*windowed)->add(3);
    NX_CHECK((*windowed)->read() == 3U);

    // Multi-threaded: 32 threads x 1M increments are exact.
    const auto sharded = (*meter)->create_counter<MT, Readable, Cumulative>("sharded");
    NX_CHECK(sharded.has_value());
    if (!sharded) {
        return;
    }
    std::vector<std::jthread> workers;
    workers.reserve(32);
    for (int t = 0; t < 32; ++t) {
        workers.emplace_back([&sharded] {
            for (int i = 0; i < 1'000'000; ++i) {
                (*sharded)->inc();
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    NX_CHECK((*sharded)->read() == 32'000'000U);

    // Beyond the 32-slot pool the totals stay exact (shared fallback shard).
    const auto overflow = (*meter)->create_counter<MT, Readable, Cumulative>("overflow");
    NX_CHECK(overflow.has_value());
    if (!overflow) {
        return;
    }
    std::vector<std::jthread> many;
    many.reserve(40);
    for (int t = 0; t < 40; ++t) {
        many.emplace_back([&overflow] {
            for (int i = 0; i < 10'000; ++i) {
                (*overflow)->inc();
            }
        });
    }
    for (auto& worker : many) {
        worker.join();
    }
    NX_CHECK((*overflow)->read() == 400'000U);

    // Recording allocates nothing.
    const std::size_t arena_before = g_alloc_used;
    const std::size_t heap_before = heap_in_use();
    for (int i = 0; i < 10'000; ++i) {
        (*counter)->inc();
    }
    NX_CHECK(g_alloc_used == arena_before);
    NX_CHECK(heap_in_use() == heap_before);

    // The serialization surface (mandate 9/10/11).
    const std::string text = (*counter)->to_string();
    NX_CHECK(!text.empty());
    NX_CHECK(metrics::to_string(**counter) == text);
    NX_CHECK(std::format("{}", **counter) == text);
    nlohmann::json j = **counter;
    NX_CHECK(j.is_object());
    NX_CHECK(j.contains("value"));
}

// --- gauges (spec metrics/counter-gauge).

} // namespace

void suite_counter() {
    run_suite("counter", test_counter);
}
