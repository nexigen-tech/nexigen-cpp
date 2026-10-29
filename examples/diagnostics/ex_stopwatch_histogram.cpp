// nexigen::diagnostics + nexigen::metrics runnable example: a stopwatch
// feeding an explicit-bucket histogram, rendered on the console.
//
// One demonstration:
//   1. A calibrated nexigen::chrono::Stamp_source clocks both the probes and
//      the measured ticks (hot path: sample() only, no syscall).
//   2. Two ScopedStopwatch probes establish the light/heavy payload envelope;
//      geometric bucket bounds are derived from it so the histogram stays
//      meaningful in every build mode (Debug, Release, sanitizers).
//   3. Every measured tick runs inside a ScopedStopwatch whose sink records
//      the scope's elapsed nanoseconds directly into an
//      ExplicitBucketHistogram (SingleThreaded/Readable/Cumulative) created
//      through the MetricProvider/Meter factory; a whole-run Stopwatch covers
//      the measurement phase.
//   4. The snapshot is printed as a summary (count/sum/min/max, percentiles)
//      plus a per-bucket table with an ASCII bar, followed by the cold-path
//      snapshot.to_string() diagnostic.
//
// Self-verification: every invariant (bounds strictly increasing, one sample
// per tick, bucket counts summing to the total, ordered percentiles, samples
// inside the whole-run measurement) is checked; a failure prints on stderr
// and exits NON-ZERO, success exits 0.
//
// Self-contained: creates its own workloads, provider and clock. Only the
// public diagnostics/metrics API. Run: ./ex_stopwatch_histogram
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/diagnostics/scoped_stopwatch.hpp>
#include <nexigen/diagnostics/stopwatch.hpp>
#include <nexigen/metrics/explicit_histogram.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>

namespace {

namespace metrics = nexigen::metrics;

using metrics::AccessPolicy;
using metrics::ConcurrencyPolicy;
using metrics::ResetPolicy;

constexpr int tick_count = 64;
constexpr std::size_t bucket_count = 8;
constexpr unsigned bar_width = 40;

bool g_ok = true;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "ex_stopwatch_histogram: FAILED check: %s\n", what);
        g_ok = false;
    }
}

// Deterministic staged work: one "unit" per 600 volatile-add iterations,
// impossible for the optimizer to remove.
[[nodiscard]] std::uint64_t work_units(int units) noexcept {
    volatile std::uint64_t sink = 0;
    for (int i = 0; i < units * 600; ++i) { sink += static_cast<std::uint64_t>(i); }
    return sink;
}

// Geometric bounds between half the light probe and 1.5x the heavy probe:
// wide enough that no build mode collapses the distribution into one bucket.
void build_bounds(std::uint64_t light_ns, std::uint64_t heavy_ns,
                  std::span<double> bounds) noexcept {
    double low = static_cast<double>(light_ns) * 0.5;
    if (low < 1.0) { low = 1.0; }
    double high = static_cast<double>(heavy_ns) * 1.5;
    if (high < low * 4.0) { high = low * 4.0; } // guarantee a usable spread
    const double ratio = high / low;
    for (std::size_t i = 0; i < bounds.size(); ++i) {
        bounds[i] = low * std::pow(ratio, static_cast<double>(i) /
                                             static_cast<double>(bounds.size() - 1U));
        if (i > 0U && bounds[i] <= bounds[i - 1U]) {
            bounds[i] = bounds[i - 1U] + 1.0; // keep the set strictly increasing
        }
    }
}

void print_histogram(const metrics::HistogramSnapshot& snapshot,
                     std::span<const double> bounds) {
    std::printf("  count=%llu  sum=%.0f ns  min=%.0f ns  max=%.0f ns  ignored=%llu\n",
                static_cast<unsigned long long>(snapshot.count), snapshot.sum, snapshot.min,
                snapshot.max, static_cast<unsigned long long>(snapshot.ignored_samples));
    std::printf("  p50=%.0f ns  p90=%.0f ns  p99=%.0f ns  p99.9=%.0f ns\n", snapshot.p50,
                snapshot.p90, snapshot.p99, snapshot.p999);

    std::uint64_t peak = 0;
    for (std::uint32_t i = 0; i < snapshot.bucket_count; ++i) {
        peak = std::max(peak, snapshot.buckets_count[i]);
    }
    for (std::uint32_t i = 0; i < snapshot.bucket_count; ++i) {
        const double lower = i == 0U ? 0.0 : bounds[i - 1U];
        const bool overflow = i >= bounds.size();
        const double upper = overflow ? 0.0 : bounds[i];

        char label[48];
        if (i == 0U) {
            std::snprintf(label, sizeof label, "(-inf, %.0f]", upper);
        } else if (overflow) {
            std::snprintf(label, sizeof label, "(%.0f, +inf)", lower);
        } else {
            std::snprintf(label, sizeof label, "(%.0f, %.0f]", lower, upper);
        }

        const std::uint64_t count = snapshot.buckets_count[i];
        const unsigned bar_len =
            peak == 0U ? 0U
                       : static_cast<unsigned>(count * bar_width / peak);
        char bar[bar_width + 1U];
        std::fill_n(bar, bar_len, '#');
        bar[bar_len] = '\0';
        std::printf("  %-20s %8llu  %s\n", label,
                    static_cast<unsigned long long>(count), bar);
    }
}

} // namespace

int main() {
    using nexigen::diagnostics::ScopedStopwatch;
    using nexigen::diagnostics::Stopwatch;

    // --- Calibrated clock: one TSC<->ns calibration for every stamp below.
    nexigen::chrono::Stamp_source stamps{};
    expect(stamps.calibrate().has_value(), "stamp source calibrated");

    // --- Provider: no background harvester, the example reads the histogram
    // directly. The null sink keeps the pipeline inactive (cold path only).
    metrics::NullMetricSink sink;
    metrics::MetricProvider_config config{};
    config.collect_interval = std::chrono::nanoseconds{0};
    metrics::MetricProvider provider{config, sink};
    expect(provider.startup_status().has_value(), "provider startup");

    const nexigen::core::Result<metrics::Meter*> meter = provider.get_meter("hil", "1.0");
    expect(meter.has_value(), "get_meter succeeds");
    if (!meter) { return 1; }

    // --- Probe the payload envelope, then derive strictly increasing bounds.
    std::printf("== bucket calibration ==\n");
    std::uint64_t light_ns = 0;
    {
        ScopedStopwatch probe{&stamps, light_ns};
        static_cast<void>(work_units(200));
    }
    std::uint64_t heavy_ns = 0;
    {
        ScopedStopwatch probe{&stamps, heavy_ns};
        static_cast<void>(work_units(3'000));
    }
    std::printf("  probes: light=%llu ns, heavy=%llu ns\n",
                static_cast<unsigned long long>(light_ns),
                static_cast<unsigned long long>(heavy_ns));
    expect(heavy_ns > light_ns, "the heavy probe takes longer than the light one");

    double bounds[bucket_count]{};
    build_bounds(light_ns, heavy_ns, std::span<double>{bounds});
    std::printf("  bounds (ns):");
    for (std::size_t i = 0; i < bucket_count; ++i) {
        std::printf(" %.0f", bounds[i]);
        expect(bounds[i] > 0.0, "every bound is positive");
        if (i > 0U) { expect(bounds[i] > bounds[i - 1U], "bounds strictly increase"); }
    }
    std::printf("\n\n");

    // --- Histogram created through the only legal path (provider -> meter).
    const auto created =
        (*meter)->create_explicit_histogram<ConcurrencyPolicy::SingleThreaded,
                                            AccessPolicy::Readable, ResetPolicy::Cumulative>(
            "tick_latency_ns", std::span<const double>{bounds, bucket_count});
    expect(created.has_value(), "explicit histogram created");
    if (!created) { return 1; }
    auto* histogram = *created;

    // --- Measure every tick with a ScopedStopwatch and record its elapsed
    // nanoseconds straight into the histogram from the guard's sink.
    std::printf("== measured tick durations ==\n");
    Stopwatch run{&stamps};
    run.start();
    for (int tick = 0; tick < tick_count; ++tick) {
        const int units = 100 + (tick * 37) % 2'900;
        ScopedStopwatch tick_guard{
            &stamps, [histogram](std::uint64_t elapsed_ns) noexcept {
                histogram->record(static_cast<double>(elapsed_ns));
            }};
        static_cast<void>(work_units(units));
    }
    const std::uint64_t run_ns = run.elapsed_ns();
    run.stop();
    std::printf("  %d ticks measured, whole-run stopwatch: %llu ns\n\n", tick_count,
                static_cast<unsigned long long>(run_ns));
    expect(run_ns > 0, "the whole-run stopwatch reports a positive interval");

    // --- Consume the histogram and print it.
    std::printf("== tick_latency_ns histogram ==\n");
    const metrics::HistogramSnapshot snapshot = histogram->read();
    print_histogram(snapshot, std::span<const double>{bounds, bucket_count});

    std::uint64_t bucket_total = 0;
    for (std::uint32_t i = 0; i < snapshot.bucket_count; ++i) {
        bucket_total += snapshot.buckets_count[i];
    }
    expect(snapshot.count == static_cast<std::uint64_t>(tick_count),
           "one sample per measured tick");
    expect(snapshot.ignored_samples == 0U, "no samples rejected as non-finite");
    expect(bucket_total == snapshot.count, "bucket counts sum to the total");
    expect(snapshot.bucket_count == bucket_count + 1U,
           "one bucket per bound plus the overflow bucket");
    for (std::size_t i = 0; i < bucket_count; ++i) {
        expect(snapshot.buckets_lower[i + 1U] == bounds[i],
               "configured bounds appear as bucket edges");
    }
    expect(snapshot.count > 0U && snapshot.min <= snapshot.max, "min/max bracket the samples");
    expect(snapshot.p50 <= snapshot.p90 && snapshot.p90 <= snapshot.p99 &&
               snapshot.p99 <= snapshot.p999,
           "percentiles are ordered");
    expect(snapshot.p50 > 0.0, "percentiles are populated");
    expect(static_cast<double>(run_ns) >= snapshot.sum,
           "recorded durations fit inside the whole-run stopwatch");

    std::printf("\n== cold-path diagnostics ==\n");
    std::printf("  %s\n", snapshot.to_string().c_str());

    if (!g_ok) {
        std::fprintf(stderr, "ex_stopwatch_histogram: one or more checks FAILED\n");
        return 1;
    }
    std::printf("\nex_stopwatch_histogram: all checks passed\n");
    return 0;
}
