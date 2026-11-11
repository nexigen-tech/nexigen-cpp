// ex_counters_and_gauges — runnable nexigen::metrics example (design D1/D4/D5;
// spec metrics/examples "Counter and gauge example").
//
// Demonstrates the counter/gauge half of the metrics API end to end:
//   1. MetricProvider + get_meter factory hierarchy.
//   2. SingleThreaded vs MultiThreaded Counter and Gauge (int64_t + double).
//   3. AccessPolicy::Readable (application-side read()/snapshot()) vs
//      AccessPolicy::WriteOnly (hot-path worker updates, harvested only by
//      the provider/sink).
//   4. ResetPolicy::Cumulative vs ResetPolicy::ResetOnRead delta windows.
//   5. Cold-path to_string() diagnostics.
//
// Concurrency contract (design D4): collect_interval = 0, so no background
// harvester runs; workers quiesce before collect_now()/read() observe their
// instruments. Recording loops touch only the wait-free instrument paths —
// printing happens after the loops, on the cold path.
//
// The example self-verifies its invariants and exits non-zero on any mismatch.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/metrics/counter.hpp>
#include <nexigen/metrics/gauge.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace metrics = nexigen::metrics;
namespace core = nexigen::core;

namespace {

using metrics::AccessPolicy;
using metrics::ConcurrencyPolicy;
using metrics::ResetPolicy;
constexpr ConcurrencyPolicy ST = ConcurrencyPolicy::SingleThreaded;
constexpr ConcurrencyPolicy MT = ConcurrencyPolicy::MultiThreaded;
constexpr AccessPolicy Readable = AccessPolicy::Readable;
constexpr AccessPolicy WriteOnly = AccessPolicy::WriteOnly;
constexpr ResetPolicy Cumulative = ResetPolicy::Cumulative;
constexpr ResetPolicy ResetOnRead = ResetPolicy::ResetOnRead;

constexpr int worker_count = 4;
constexpr int per_worker = 250'000;
constexpr std::uint64_t expected_mt = static_cast<std::uint64_t>(worker_count) * per_worker;

int g_failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

/// Sink that prints harvested batches and remembers the records it saw, so
/// the example can verify what the provider harvested (design D5).
class Capture_sink {
public:
    [[nodiscard]] core::Status on_batch(const metrics::MetricBatchSnapshot& batch) noexcept {
        for (std::uint32_t i = 0; i < batch.record_count; ++i) {
            if (batch.records == nullptr) {
                break;
            }
            std::printf("  [harvest] %s\n", batch.records[i].to_string().c_str());
            seen_.push_back(batch.records[i]);
        }
        return {};
    }

    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// Harvested value of the named counter (zero when never harvested).
    [[nodiscard]] std::uint64_t counter_value(const char* name) const {
        for (const metrics::MetricRecord& record : seen_) {
            if (metrics::detail::view_name(record.identity.metric_name) == name) {
                return record.scalar.counter;
            }
        }
        return 0;
    }

private:
    std::vector<metrics::MetricRecord> seen_{};
};

// Compile-time access gating: a WriteOnly instrument has no read()/snapshot()
// reachable from application code (spec metrics/metric-concepts).
template <class T>
concept HasRead = requires(const T& instrument) { instrument.read(); };

static_assert(!HasRead<metrics::Counter<MT, WriteOnly, Cumulative>>,
              "WriteOnly counters must not expose read()");
static_assert(!HasRead<metrics::Gauge<std::int64_t, MT, WriteOnly, Cumulative>>,
              "WriteOnly gauges must not expose read()");

} // namespace

using CounterST = metrics::Counter<ST, Readable, Cumulative>;
using GaugeIntST = metrics::Gauge<std::int64_t, ST, Readable, Cumulative>;
using GaugeDoubleST = metrics::Gauge<double, ST, Readable, Cumulative>;
using CounterMT = metrics::Counter<MT, Readable, Cumulative>;
using GaugeIntMT = metrics::Gauge<std::int64_t, MT, Readable, Cumulative>;
using CounterWriteOnly = metrics::Counter<MT, WriteOnly, Cumulative>;
using CounterWindow = metrics::Counter<ST, Readable, ResetOnRead>;
using GaugeIntWindow = metrics::Gauge<std::int64_t, ST, Readable, ResetOnRead>;

/// (1) Single-threaded accumulation: exact, plain-storage hot path. The two
/// diagnostics strings are captured here so the last step can print them.
bool step_single_threaded(metrics::Meter& meter, std::string& counter_text,
                          std::string& gauge_text) {
    std::printf("== SingleThreaded accumulation ==\n");
    const auto counter = meter.create_counter<ST, Readable, Cumulative>("requests");
    const auto int_gauge =
        meter.create_gauge<std::int64_t, ST, Readable, Cumulative>("temperature_c");
    const auto double_gauge = meter.create_gauge<double, ST, Readable, Cumulative>("pressure_bar");
    expect(counter.has_value() && int_gauge.has_value() && double_gauge.has_value(),
           "single-threaded instruments created");
    if (!counter || !int_gauge || !double_gauge) {
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        (*counter)->inc();
    }
    (*counter)->add(7);
    (*int_gauge)->set(10);
    (*int_gauge)->add(5);
    (*int_gauge)->sub(3);
    (*double_gauge)->set(1.25);
    (*double_gauge)->add(0.75);
    (*double_gauge)->sub(0.5);

    expect((*counter)->read() == 10U, "ST counter accumulates exactly (3 inc + add 7 = 10)");
    expect((*int_gauge)->read() == 12, "ST int64 gauge is exact (set 10, +5, -3 = 12)");
    expect((*double_gauge)->read() == 1.5, "ST double gauge is exact (1.25 + 0.75 - 0.5 = 1.5)");
    counter_text = (*counter)->to_string();
    gauge_text = (*int_gauge)->to_string();
    return true;
}

/// (2) Multi-threaded accumulation: wait-free per-thread shards.
bool step_multi_threaded(metrics::Meter& meter) {
    std::printf("== MultiThreaded accumulation ==\n");
    const auto counter = meter.create_counter<MT, Readable, Cumulative>("events");
    const auto gauge = meter.create_gauge<std::int64_t, MT, Readable, Cumulative>("queue_depth");
    expect(counter.has_value() && gauge.has_value(), "multi-threaded instruments created");
    if (!counter || !gauge) {
        return false;
    }

    {
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);
        for (int w = 0; w < worker_count; ++w) {
            workers.emplace_back([&] {
                for (int i = 0; i < per_worker; ++i) {
                    (*counter)->inc();
                    (*gauge)->add(1);
                }
            });
        }
    } // all workers joined here

    expect((*counter)->read() == expected_mt, "MT counter is exact across workers");
    expect((*gauge)->read() == static_cast<std::int64_t>(expected_mt),
           "MT gauge add() is exact across workers");
    return true;
}

/// (3) Readable vs WriteOnly: only the provider harvest observes a WriteOnly
/// instrument, Readable code reads and snapshots directly.
bool step_access_policies(metrics::Meter& meter, metrics::MetricProvider& provider,
                          Capture_sink& capture) {
    std::printf("== Readable vs WriteOnly ==\n");
    const auto readable = meter.create_counter<MT, Readable, Cumulative>("visible");
    const auto write_only = meter.create_counter<MT, WriteOnly, Cumulative>("hot_events");
    expect(readable.has_value() && write_only.has_value(), "access-policy instruments created");
    if (!readable || !write_only) {
        return false;
    }

    {
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);
        for (int w = 0; w < worker_count; ++w) {
            workers.emplace_back([&] {
                for (int i = 0; i < per_worker; ++i) {
                    (*readable)->inc();
                    (*write_only)->inc(); // hot-path write; no read() exists
                }
            });
        }
    }

    const metrics::MetricRecord visible_record = (*readable)->snapshot();
    std::printf("  [readable] read()=%llu snapshot=%s\n",
                static_cast<unsigned long long>((*readable)->read()),
                visible_record.to_string().c_str());
    expect(visible_record.scalar.counter == expected_mt, "readable snapshot carries the total");

    const core::Status collected = provider.collect_now();
    expect(collected.has_value(), "collect_now succeeds");
    expect(capture.counter_value("hot_events") == expected_mt,
           "write-only value appears only in harvest output");
    return true;
}

/// (4) Cumulative vs ResetOnRead windows across successive polls.
bool step_reset_policies(metrics::Meter& meter) {
    std::printf("== Cumulative vs ResetOnRead ==\n");
    const auto cumulative = meter.create_counter<ST, Readable, Cumulative>("all_time");
    const auto windowed = meter.create_counter<ST, Readable, ResetOnRead>("per_poll");
    const auto gauge = meter.create_gauge<std::int64_t, ST, Readable, ResetOnRead>("net_flow");
    expect(cumulative.has_value() && windowed.has_value() && gauge.has_value(),
           "window instruments created");
    if (!cumulative || !windowed || !gauge) {
        return false;
    }

    (*cumulative)->add(5);
    (*windowed)->add(5);
    (*gauge)->add(5);
    const std::uint64_t c1 = (*cumulative)->read();
    const std::uint64_t w1 = (*windowed)->read();
    const std::int64_t g1 = (*gauge)->read();
    std::printf("  poll 1: cumulative=%llu windowed=%llu gauge=%lld\n",
                static_cast<unsigned long long>(c1), static_cast<unsigned long long>(w1),
                static_cast<long long>(g1));
    const std::uint64_t c2 = (*cumulative)->read();
    const std::uint64_t w2 = (*windowed)->read();
    const std::int64_t g2 = (*gauge)->read();
    std::printf("  poll 2: cumulative=%llu windowed=%llu gauge=%lld\n",
                static_cast<unsigned long long>(c2), static_cast<unsigned long long>(w2),
                static_cast<long long>(g2));
    (*windowed)->add(3);
    (*gauge)->add(3);
    const std::uint64_t c3 = (*cumulative)->read();
    const std::uint64_t w3 = (*windowed)->read();
    const std::int64_t g3 = (*gauge)->read();
    std::printf("  poll 3: cumulative=%llu windowed=%llu gauge=%lld\n",
                static_cast<unsigned long long>(c3), static_cast<unsigned long long>(w3),
                static_cast<long long>(g3));

    // Cumulative persists across polls; ResetOnRead reports 5, 0, then 3.
    expect(c1 == 5U && w1 == 5U && g1 == 5, "poll 1 reports the first window (5)");
    expect(c2 == 5U && w2 == 0U && g2 == 0, "poll 2: cumulative persists, windows reset to 0");
    expect(c3 == 5U && w3 == 3U && g3 == 3, "poll 3 reports only the new delta (3)");
    expect((*cumulative)->read() == 5U, "cumulative totals persist across polls");
    return true;
}

/// (5) Cold-path diagnostics: the strings captured in step (1) still render.
void step_diagnostics(const std::string& counter_text, const std::string& gauge_text) {
    std::printf("== Diagnostics ==\n");
    std::printf("  %s\n", counter_text.c_str());
    std::printf("  %s\n", gauge_text.c_str());
    expect(!counter_text.empty(), "to_string() renders");
    expect(counter_text.find("Cumulative") != std::string::npos, "diagnostics name the policy");
}

int main() {
    // (0) Setup: no background thread; one sink that remembers harvested
    // records so the WriteOnly scenario can be checked.
    Capture_sink capture;
    metrics::MetricProviderConfig config{};
    config.collect_interval = std::chrono::nanoseconds{0}; // no background thread
    metrics::MetricProvider provider{config, capture};
    expect(provider.startup_status().has_value(), "provider startup");

    const core::Result<metrics::Meter*> meter = provider.get_meter("engine", "1.2.0");
    expect(meter.has_value(), "get_meter succeeds");
    if (!meter) {
        return 1;
    }

    std::string counter_text;
    std::string gauge_text;
    if (!step_single_threaded(**meter, counter_text, gauge_text)) {
        return 1;
    }
    if (!step_multi_threaded(**meter)) {
        return 1;
    }
    if (!step_access_policies(**meter, provider, capture)) {
        return 1;
    }
    if (!step_reset_policies(**meter)) {
        return 1;
    }
    step_diagnostics(counter_text, gauge_text);

    std::printf("%s: %s\n", "ex_counters_and_gauges", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
