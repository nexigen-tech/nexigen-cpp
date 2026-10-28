// ex_histograms — runnable nexigen::metrics example (design D1/D4/D5;
// spec metrics/examples "Histogram example").
//
// Measures simulated real-time task durations from multiple threads with
// per-producer nexigen::chrono::Stamp_source clocks and records them into
// both histogram families:
//   1. ExplicitBucketHistogram with caller-defined latency upper bounds
//      {10, 25, 50, 100, 250, 500, 1000} ns.
//   2. ExponentialHistogram with automatic base-2 exponential bucket mapping
//      across the wide dynamic range of the workload.
// Reports count, sum, min, max and p50/p90/p99/p99.9 for both.
//
// Concurrency contract (design D4): collect_interval = 0, no background
// harvester; both instruments are MultiThreaded (per-thread shards merged at
// observation) and snapshots are taken after the producers join. Recording
// stays on the wait-free instrument paths; printing is cold path.
//
// The example self-verifies its invariants and exits non-zero on any mismatch.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <thread>
#include <vector>

#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/metrics/explicit_histogram.hpp>
#include <nexigen/metrics/exponential_histogram.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>

namespace metrics = nexigen::metrics;
namespace core = nexigen::core;

namespace {

using metrics::AccessPolicy;
using metrics::ConcurrencyPolicy;
using metrics::ResetPolicy;
constexpr ConcurrencyPolicy MT = ConcurrencyPolicy::MultiThreaded;
constexpr AccessPolicy Readable = metrics::AccessPolicy::Readable;
constexpr ResetPolicy Cumulative = ResetPolicy::Cumulative;

/// Latency upper bounds in nanoseconds (spec metrics/examples).
constexpr double latency_bounds[] = {10.0, 25.0, 50.0, 100.0, 250.0, 500.0, 1000.0};
constexpr std::int32_t exponential_scale = 10;

constexpr int worker_count = 4;
constexpr int measurements_per_worker = 50'000;
constexpr std::uint64_t expected_samples =
    static_cast<std::uint64_t>(worker_count) * measurements_per_worker;

int g_failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

/// Percentiles are monotonically non-decreasing in rank.
[[nodiscard]] bool percentiles_ordered(const metrics::HistogramSnapshot& snapshot) {
    return snapshot.p50 <= snapshot.p90 && snapshot.p90 <= snapshot.p99 &&
           snapshot.p99 <= snapshot.p999;
}

} // namespace

int main() {
    metrics::NullMetricSink sink;
    metrics::MetricProvider_config config{};
    config.collect_interval = std::chrono::nanoseconds{0}; // no background thread
    metrics::MetricProvider provider{config, sink};
    expect(provider.startup_status().has_value(), "provider startup");

    const core::Result<metrics::Meter*> meter = provider.get_meter("hil", "1.0");
    expect(meter.has_value(), "get_meter succeeds");
    if (!meter) {
        return 1;
    }

    const auto latency = (*meter)->create_explicit_histogram<MT, Readable, Cumulative>(
        "task_latency_ns", std::span<const double>{latency_bounds});
    const auto wide = (*meter)->create_exponential_histogram<MT, Readable, Cumulative>(
        "task_latency_wide", exponential_scale);
    expect(latency.has_value() && wide.has_value(), "histograms created");
    if (!latency || !wide) {
        return 1;
    }

    // --- Measure simulated task durations from multiple threads. Each
    // producer owns a calibrated Stamp_source (hot path: sample() only) and
    // simulates workloads whose spin counts span orders of magnitude.
    std::printf("== Measured task durations ==\n");
    std::atomic<int> calibration_failures{0};
    {
        std::vector<std::jthread> producers;
        producers.reserve(worker_count);
        for (int w = 0; w < worker_count; ++w) {
            producers.emplace_back([&] {
                nexigen::chrono::Stamp_source stamps{};
                if (!stamps.calibrate().has_value()) {
                    calibration_failures.fetch_add(1, std::memory_order_relaxed);
                }
                volatile std::uint64_t spin_sink = 0;
                for (int i = 0; i < measurements_per_worker; ++i) {
                    const nexigen::chrono::Timestamp start = stamps.sample();
                    const int iterations = 1 << (i % 12); // 1 .. 2048 spin steps
                    for (int s = 0; s < iterations; ++s) {
                        spin_sink = spin_sink + 1U;
                    }
                    const nexigen::chrono::Timestamp end = stamps.sample();
                    const double duration =
                        static_cast<double>(end.nanoseconds() - start.nanoseconds());
                    (*latency)->record(duration);
                    (*wide)->record(duration);
                }
                static_cast<void>(spin_sink);
            });
        }
    } // producers joined
    expect(calibration_failures.load(std::memory_order_relaxed) == 0,
           "every producer clock calibrated");

    // --- ExplicitBucketHistogram: configured bounds are reflected in the
    // snapshot and the buckets account for every sample.
    std::printf("== ExplicitBucketHistogram ==\n");
    const metrics::HistogramSnapshot latency_view = (*latency)->read();
    std::printf("  %s\n", latency_view.to_string().c_str());
    expect(latency_view.count == expected_samples, "explicit histogram counts every measurement");
    expect(latency_view.bucket_count == 8U, "one bucket per bound plus the overflow bucket");
    std::uint64_t latency_buckets = 0;
    for (std::uint32_t i = 0; i < latency_view.bucket_count; ++i) {
        latency_buckets += latency_view.buckets_count[i];
    }
    expect(latency_buckets == latency_view.count, "bucket counts match the summary");
    for (std::size_t i = 0; i < std::size(latency_bounds); ++i) {
        expect(latency_view.buckets_lower[i + 1U] == latency_bounds[i],
               "configured bounds appear as bucket edges");
    }

    // --- ExponentialHistogram: wide dynamic range, effective scale reported,
    // buckets plus zeros account for every accepted sample.
    std::printf("== ExponentialHistogram ==\n");
    const metrics::HistogramSnapshot wide_view = (*wide)->read();
    std::printf("  %s\n", wide_view.to_string().c_str());
    expect(wide_view.count == expected_samples, "exponential histogram counts every measurement");
    std::uint64_t wide_buckets = 0;
    for (std::uint32_t i = 0; i < wide_view.bucket_count; ++i) {
        wide_buckets += wide_view.buckets_count[i];
    }
    expect(wide_buckets + wide_view.zero_count == wide_view.count,
           "buckets plus zeros account for every accepted sample");
    expect(wide_view.effective_scale >= 0 && wide_view.effective_scale <= exponential_scale,
           "effective scale is reported and at most the configured scale");

    // --- Percentiles for both families.
    std::printf("== Percentiles ==\n");
    std::printf("  explicit:    p50=%.1f p90=%.1f p99=%.1f p99.9=%.1f\n", latency_view.p50,
                latency_view.p90, latency_view.p99, latency_view.p999);
    std::printf("  exponential: p50=%.1f p90=%.1f p99=%.1f p99.9=%.1f\n", wide_view.p50,
                wide_view.p90, wide_view.p99, wide_view.p999);
    expect(percentiles_ordered(latency_view), "explicit percentiles are ordered");
    expect(percentiles_ordered(wide_view), "exponential percentiles are ordered");
    expect(latency_view.p50 > 0.0 && wide_view.p50 > 0.0, "percentiles are populated");

    std::printf("ex_histograms: %s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
