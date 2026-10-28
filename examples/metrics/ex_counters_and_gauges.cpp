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

int main() {
    Capture_sink capture;
    metrics::MetricProvider_config config{};
    config.collect_interval = std::chrono::nanoseconds{0}; // no background thread
    metrics::MetricProvider provider{config, capture};
    expect(provider.startup_status().has_value(), "provider startup");

    const core::Result<metrics::Meter*> meter = provider.get_meter("engine", "1.2.0");
    expect(meter.has_value(), "get_meter succeeds");
    if (!meter) {
        return 1;
    }

    // --- 1. Single-threaded accumulation: exact, plain-storage hot path.
    std::printf("== SingleThreaded accumulation ==\n");
    const auto st_counter = (*meter)->create_counter<ST, Readable, Cumulative>("requests");
    const auto st_int_gauge = (*meter)->create_gauge<std::int64_t, ST, Readable, Cumulative>("temperature_c");
    const auto st_double_gauge = (*meter)->create_gauge<double, ST, Readable, Cumulative>("pressure_bar");
    expect(st_counter.has_value() && st_int_gauge.has_value() && st_double_gauge.has_value(),
           "single-threaded instruments created");
    if (!st_counter || !st_int_gauge || !st_double_gauge) {
        return 1;
    }

    for (int i = 0; i < 3; ++i) {
        (*st_counter)->inc();
    }
    (*st_counter)->add(7);
    (*st_int_gauge)->set(10);
    (*st_int_gauge)->add(5);
    (*st_int_gauge)->sub(3);
    (*st_double_gauge)->set(1.25);
    (*st_double_gauge)->add(0.75);
    (*st_double_gauge)->sub(0.5);

    expect((*st_counter)->read() == 10U, "ST counter accumulates exactly (3 inc + add 7 = 10)");
    expect((*st_int_gauge)->read() == 12, "ST int64 gauge is exact (set 10, +5, -3 = 12)");
    expect((*st_double_gauge)->read() == 1.5, "ST double gauge is exact (1.25 + 0.75 - 0.5 = 1.5)");

    // --- 2. Multi-threaded accumulation: wait-free per-thread shards.
    std::printf("== MultiThreaded accumulation ==\n");
    const auto mt_counter = (*meter)->create_counter<MT, Readable, Cumulative>("events");
    const auto mt_gauge = (*meter)->create_gauge<std::int64_t, MT, Readable, Cumulative>("queue_depth");
    expect(mt_counter.has_value() && mt_gauge.has_value(), "multi-threaded instruments created");
    if (!mt_counter || !mt_gauge) {
        return 1;
    }

    {
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);
        for (int w = 0; w < worker_count; ++w) {
            workers.emplace_back([&] {
                for (int i = 0; i < per_worker; ++i) {
                    (*mt_counter)->inc();
                    (*mt_gauge)->add(1);
                }
            });
        }
    } // all workers joined here

    expect((*mt_counter)->read() == expected_mt, "MT counter is exact across workers");
    expect((*mt_gauge)->read() == static_cast<std::int64_t>(expected_mt),
           "MT gauge add() is exact across workers");

    // --- 3. Readable vs WriteOnly.
    std::printf("== Readable vs WriteOnly ==\n");
    const auto readable = (*meter)->create_counter<MT, Readable, Cumulative>("visible");
    const auto write_only = (*meter)->create_counter<MT, WriteOnly, Cumulative>("hot_events");
    expect(readable.has_value() && write_only.has_value(), "access-policy instruments created");
    if (!readable || !write_only) {
        return 1;
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

    // Readable: application code reads and snapshots directly.
    const metrics::MetricRecord visible_record = (*readable)->snapshot();
    std::printf("  [readable] read()=%llu snapshot=%s\n",
                static_cast<unsigned long long>((*readable)->read()),
                visible_record.to_string().c_str());
    expect(visible_record.scalar.counter == expected_mt, "readable snapshot carries the total");

    // WriteOnly: only the provider harvest observes the value.
    const core::Status collected = provider.collect_now();
    expect(collected.has_value(), "collect_now succeeds");
    expect(capture.counter_value("hot_events") == expected_mt,
           "write-only value appears only in harvest output");

    // --- 4. Cumulative vs ResetOnRead windows.
    std::printf("== Cumulative vs ResetOnRead ==\n");
    const auto cumulative = (*meter)->create_counter<ST, Readable, Cumulative>("all_time");
    const auto windowed = (*meter)->create_counter<ST, Readable, ResetOnRead>("per_poll");
    const auto windowed_gauge = (*meter)->create_gauge<std::int64_t, ST, Readable, ResetOnRead>("net_flow");
    expect(cumulative.has_value() && windowed.has_value() && windowed_gauge.has_value(),
           "window instruments created");
    if (!cumulative || !windowed || !windowed_gauge) {
        return 1;
    }

    (*cumulative)->add(5);
    (*windowed)->add(5);
    (*windowed_gauge)->add(5);
    const std::uint64_t c1 = (*cumulative)->read();
    const std::uint64_t w1 = (*windowed)->read();
    const std::int64_t g1 = (*windowed_gauge)->read();
    std::printf("  poll 1: cumulative=%llu windowed=%llu gauge=%lld\n",
                static_cast<unsigned long long>(c1), static_cast<unsigned long long>(w1),
                static_cast<long long>(g1));
    const std::uint64_t c2 = (*cumulative)->read();
    const std::uint64_t w2 = (*windowed)->read();
    const std::int64_t g2 = (*windowed_gauge)->read();
    std::printf("  poll 2: cumulative=%llu windowed=%llu gauge=%lld\n",
                static_cast<unsigned long long>(c2), static_cast<unsigned long long>(w2),
                static_cast<long long>(g2));
    (*windowed)->add(3);
    (*windowed_gauge)->add(3);
    const std::uint64_t c3 = (*cumulative)->read();
    const std::uint64_t w3 = (*windowed)->read();
    const std::int64_t g3 = (*windowed_gauge)->read();
    std::printf("  poll 3: cumulative=%llu windowed=%llu gauge=%lld\n",
                static_cast<unsigned long long>(c3), static_cast<unsigned long long>(w3),
                static_cast<long long>(g3));

    // Cumulative persists across polls; ResetOnRead reports 5, 0, then 3.
    expect(c1 == 5U && w1 == 5U && g1 == 5, "poll 1 reports the first window (5)");
    expect(c2 == 5U && w2 == 0U && g2 == 0, "poll 2: cumulative persists, windows reset to 0");
    expect(c3 == 5U && w3 == 3U && g3 == 3, "poll 3 reports only the new delta (3)");
    expect((*cumulative)->read() == 5U, "cumulative totals persist across polls");

    // --- 5. Cold-path diagnostics.
    std::printf("== Diagnostics ==\n");
    const std::string text = (*st_counter)->to_string();
    std::printf("  %s\n", text.c_str());
    std::printf("  %s\n", (*st_int_gauge)->to_string().c_str());
    expect(!text.empty(), "to_string() renders");
    expect(text.find("Cumulative") != std::string::npos, "diagnostics name the policy");

    std::printf("%s: %s\n", "ex_counters_and_gauges", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
