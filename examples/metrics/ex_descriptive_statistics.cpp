// ex_descriptive_statistics — runnable nexigen::metrics example (design D1/D4/D5;
// spec metrics/examples "Descriptive statistics example").
//
// Simulates a real-time sensor/control loop recording measurements into
// DescriptiveStatistics:
//   1. A known stream pinned against the Welford fixture (count/min/max/mean/std).
//   2. Cumulative all-time statistics vs ResetOnRead windowed statistics polled
//      between ticks, with multiple std::jthread producers (MT double-bank slots).
//   3. Visible non-finite rejection into ignored_samples.
//   4. Snapshots printed via to_string() and std::format with policy enums.
//
// Concurrency contract (design D4): collect_interval = 0, no background
// harvester; record() stays on the wait-free instrument path and all printing
// happens after the loops. Window polls run against MultiThreaded instruments —
// the seq/bank handshake is the designed cross-thread observation surface.
//
// The example self-verifies its invariants and exits non-zero on any mismatch.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/metrics/descriptive_statistics.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>
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
constexpr ResetPolicy Cumulative = ResetPolicy::Cumulative;
constexpr ResetPolicy ResetOnRead = ResetPolicy::ResetOnRead;

constexpr int worker_count = 4;
constexpr int phase_count = 3;
constexpr int per_phase_worker = 10'000;

int g_failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

[[nodiscard]] bool near(double actual, double expected, double tolerance = 1e-6) {
    return std::fabs(actual - expected) <= tolerance;
}

} // namespace

int main() {
    metrics::NullMetricSink sink;
    metrics::MetricProvider_config config{};
    config.collect_interval = std::chrono::nanoseconds{0}; // no background thread
    metrics::MetricProvider provider{config, sink};
    expect(provider.startup_status().has_value(), "provider startup");

    const core::Result<metrics::Meter*> meter = provider.get_meter("control-loop", "2.0.0");
    expect(meter.has_value(), "get_meter succeeds");
    if (!meter) {
        return 1;
    }

    // --- 1. Running summary: the Welford fixture pinned exactly.
    std::printf("== Running summary (fixture) ==\n");
    const auto fixture = (*meter)->create_statistics<ST, Readable, Cumulative>("fixture");
    expect(fixture.has_value(), "fixture statistics created");
    if (!fixture) {
        return 1;
    }
    for (double value : {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}) {
        (*fixture)->record(value);
    }
    metrics::StatisticsSnapshot summary = (*fixture)->read();
    std::printf("  %s\n", summary.to_string().c_str());
    expect(summary.count == 8U, "fixture count is 8");
    expect(near(summary.mean, 5.0), "fixture mean is 5");
    expect(near(summary.stddev, 2.0), "fixture population std is 2");
    expect(near(summary.min, 2.0) && near(summary.max, 9.0), "fixture min/max are 2/9");

    // Non-finite samples are rejected visibly (moments unchanged).
    (*fixture)->record(std::nan(""));
    (*fixture)->record(std::numeric_limits<double>::infinity());
    (*fixture)->record(-std::numeric_limits<double>::infinity());
    summary = (*fixture)->read();
    expect(summary.count == 8U, "non-finite samples do not change the moments");
    expect(summary.ignored_samples == 3U, "ignored-sample counter reports the rejections");

    // --- 2. Cumulative all-time statistics across producer phases.
    std::printf("== Cumulative all-time statistics ==\n");
    const auto all_time = (*meter)->create_statistics<MT, Readable, Cumulative>("sensor_all");
    const auto windowed = (*meter)->create_statistics<MT, Readable, ResetOnRead>("sensor_window");
    expect(all_time.has_value() && windowed.has_value(), "multi-threaded statistics created");
    if (!all_time || !windowed) {
        return 1;
    }

    for (int phase = 1; phase <= phase_count; ++phase) {
        {
            std::vector<std::jthread> producers;
            producers.reserve(worker_count);
            for (int w = 0; w < worker_count; ++w) {
                const double value = static_cast<double>(phase * 10 + (w + 1));
                producers.emplace_back([&, value] {
                    for (int i = 0; i < per_phase_worker; ++i) {
                        (*all_time)->record(value);
                        (*windowed)->record(value);
                    }
                });
            }
        } // producers joined: the interval is complete

        // Poll the window between ticks: it covers exactly this interval.
        const metrics::StatisticsSnapshot window = (*windowed)->read();
        std::printf("  window %d: count=%llu min=%.1f max=%.1f mean=%.3f\n", phase,
                    static_cast<unsigned long long>(window.count), window.min, window.max,
                    window.mean);
        expect(window.count == static_cast<std::uint64_t>(worker_count * per_phase_worker),
               "window reports exactly its own interval's samples");
        expect(near(window.min, static_cast<double>(phase * 10 + 1)), "window min is the phase min");
        expect(near(window.max, static_cast<double>(phase * 10 + worker_count)),
               "window max is the phase max");
        expect(near(window.mean, static_cast<double>(phase * 10) + 2.5), "window mean is the phase mean");
    }

    // The cumulative instrument reflects the whole run.
    const metrics::StatisticsSnapshot total = (*all_time)->read();
    std::printf("  all-time: %s\n", total.to_string().c_str());
    expect(total.count == static_cast<std::uint64_t>(phase_count * worker_count * per_phase_worker),
           "cumulative count covers every phase");
    expect(near(total.min, 11.0) && near(total.max, 34.0), "cumulative min/max span all phases");

    // --- 3. Windowed statistics under live polling (exactly-once swap).
    std::printf("== Live polling while producers record ==\n");
    std::atomic<int> active_workers{worker_count};
    std::uint64_t windowed_total = 0;
    int polls = 0;
    {
        std::vector<std::jthread> producers;
        producers.reserve(worker_count);
        for (int w = 0; w < worker_count; ++w) {
            producers.emplace_back([&] {
                for (int i = 0; i < 250'000; ++i) {
                    (*all_time)->record(100.0);
                    (*windowed)->record(100.0);
                }
                active_workers.fetch_sub(1, std::memory_order_relaxed);
            });
        }
        while (active_workers.load(std::memory_order_relaxed) > 0) {
            const metrics::StatisticsSnapshot window = (*windowed)->read();
            windowed_total += window.count;
            ++polls;
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
            if (polls > 10'000) {
                break; // safety valve; producers are bounded
            }
        }
    } // producers joined
    // Drain: after a swap, records racing the boundary may still sit in the
    // shadow bank and land in the next window — read until two consecutive
    // empty windows so every record is attributed exactly once.
    std::uint64_t consecutive_empty = 0;
    while (consecutive_empty < 2) {
        const metrics::StatisticsSnapshot window = (*windowed)->read();
        windowed_total += window.count;
        consecutive_empty = window.count == 0U ? consecutive_empty + 1 : 0;
    }
    const metrics::StatisticsSnapshot cumulative = (*all_time)->read();
    const std::uint64_t live_recorded = static_cast<std::uint64_t>(worker_count) * 250'000U;
    const std::uint64_t recorded =
        live_recorded + static_cast<std::uint64_t>(phase_count * worker_count * per_phase_worker);
    std::printf("  polls=%d windowed_total=%llu live_recorded=%llu\n", polls,
                static_cast<unsigned long long>(windowed_total),
                static_cast<unsigned long long>(live_recorded));
    expect(windowed_total == live_recorded,
           "every record lands in exactly one window across concurrent polls");
    expect(cumulative.count == recorded, "cumulative count matches every record");

    // --- 4. Diagnostics: to_string() and std::format agree.
    std::printf("== Diagnostics ==\n");
    const std::string text = (*fixture)->to_string();
    std::printf("  %s\n", text.c_str());
    const std::string formatted = std::format("{}", summary);
    expect(!text.empty() && text.find("Cumulative") != std::string::npos,
           "instrument diagnostics name the policy");
    expect(formatted == summary.to_string(), "std::format equals to_string() for the snapshot");
    expect(std::format("{}", MT) == "MultiThreaded", "policy enums format by canonical name");
    expect(std::format("{}", Readable) == "Readable", "access policy formats by canonical name");
    expect(std::format("{}", ResetOnRead) == "ResetOnRead", "reset policy formats by canonical name");

    std::printf("ex_descriptive_statistics: %s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
