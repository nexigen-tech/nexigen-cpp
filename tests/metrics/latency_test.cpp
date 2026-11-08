// Split from metrics_test.cpp: nexigen::metrics hot-path latency gates.

#include "metrics_test_support.hpp"

namespace {

#ifdef NEXIGEN_METRICS_STRICT_LATENCY
namespace {

[[nodiscard]] inline double now_ns() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<double>(ts.tv_sec) * 1e9 + static_cast<double>(ts.tv_nsec);
}

template <class F> [[nodiscard]] double median_ns_per_op(F&& body, int batches, int ops) {
    body(ops / 10 + 1); // warm-up
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(batches));
    for (int b = 0; b < batches; ++b) {
        const double start = now_ns();
        body(ops);
        const double stop = now_ns();
        samples.push_back((stop - start) / static_cast<double>(ops));
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

} // namespace
#endif

#if !defined(NEXIGEN_METRICS_STRICT_LATENCY) || defined(NX_SANITIZER_BUILD)
void test_latency_disabled() {
    std::printf("  latency gates compiled out (correctness-only mode)\n");
    NX_CHECK(true);
}
#endif

#if defined(NEXIGEN_METRICS_STRICT_LATENCY) && !defined(NX_SANITIZER_BUILD)
void test_latency_meter(const Result<metrics::Meter*>& meter) {
    NX_CHECK(meter.has_value());
}

void test_latency_ready(
    const Result<metrics::Counter<ST, Readable, Cumulative>*>& st_counter,
    const Result<metrics::Counter<MT, Readable, Cumulative>*>& mt_counter,
    const Result<metrics::Gauge<std::int64_t, MT, Readable, Cumulative>*>& gauge,
    const Result<metrics::DescriptiveStatistics<MT, Readable, Cumulative>*>& stats,
    const Result<metrics::ExplicitBucketHistogram<MT, Readable, Cumulative>*>& hist,
    const Result<metrics::ExponentialHistogram<MT, Readable, Cumulative>*>& expo) {
    NX_CHECK(st_counter && mt_counter && gauge && stats && hist && expo);
}

[[nodiscard]] double
test_latency_st_inc(const Result<metrics::Counter<ST, Readable, Cumulative>*>& st_counter,
                    int batches, int ops) {
    const double st_inc = median_ns_per_op(
        [&st_counter](int n) {
            for (int i = 0; i < n; ++i) {
                (*st_counter)->inc();
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
        },
        batches, ops);
    return st_inc;
}

[[nodiscard]] double
test_latency_mt_inc(const Result<metrics::Counter<MT, Readable, Cumulative>*>& mt_counter,
                    int batches, int ops) {
    const double mt_inc = median_ns_per_op(
        [&mt_counter](int n) {
            for (int i = 0; i < n; ++i) {
                (*mt_counter)->inc();
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
        },
        batches, ops);
    return mt_inc;
}

[[nodiscard]] double
test_latency_gauge_set(const Result<metrics::Gauge<std::int64_t, MT, Readable, Cumulative>*>& gauge,
                       int batches, int ops) {
    const double gauge_set = median_ns_per_op(
        [&gauge](int n) {
            for (int i = 0; i < n; ++i) {
                (*gauge)->set(42);
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
        },
        batches, ops);
    return gauge_set;
}

[[nodiscard]] double
test_latency_gauge_add(const Result<metrics::Gauge<std::int64_t, MT, Readable, Cumulative>*>& gauge,
                       int batches, int ops) {
    const double gauge_add = median_ns_per_op(
        [&gauge](int n) {
            for (int i = 0; i < n; ++i) {
                (*gauge)->add(1);
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
        },
        batches, ops);
    return gauge_add;
}

[[nodiscard]] double test_latency_stats_record(
    const Result<metrics::DescriptiveStatistics<MT, Readable, Cumulative>*>& stats, int batches,
    int ops) {
    const double stats_record = median_ns_per_op(
        [&stats](int n) {
            for (int i = 0; i < n; ++i) {
                (*stats)->record(1.5);
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
        },
        batches, ops);
    return stats_record;
}

[[nodiscard]] double test_latency_hist_record(
    const Result<metrics::ExplicitBucketHistogram<MT, Readable, Cumulative>*>& hist, int batches,
    int ops) {
    const double hist_record = median_ns_per_op(
        [&hist](int n) {
            for (int i = 0; i < n; ++i) {
                (*hist)->record(1.5);
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
        },
        batches, ops);
    return hist_record;
}

[[nodiscard]] double test_latency_exp_record(
    const Result<metrics::ExponentialHistogram<MT, Readable, Cumulative>*>& expo, int batches,
    int ops) {
    const double expo_record = median_ns_per_op(
        [&expo](int n) {
            for (int i = 0; i < n; ++i) {
                (*expo)->record(1.5);
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
        },
        batches, ops);
    return expo_record;
}

void test_latency_report(double st_inc, double mt_inc, double gauge_set, double gauge_add,
                         double stats_record, double hist_record, double expo_record) {
    std::printf("  latency medians (ns/op): st_inc=%.2f mt_inc=%.2f gauge_set=%.2f "
                "gauge_add=%.2f stats=%.2f hist=%.2f expo=%.2f\n",
                st_inc, mt_inc, gauge_set, gauge_add, stats_record, hist_record, expo_record);
#ifdef NX_SANITIZER_BUILD
    // Instrumented timings are not representative (design D16): the medians
    // are reported but never gated in sanitizer builds.
    static_cast<void>(st_inc);
    NX_CHECK(true);
#else
    NX_CHECK(st_inc <= 2.0);        // task 6.3
    NX_CHECK(mt_inc <= 6.0);        // task 6.3
    NX_CHECK(gauge_set <= 8.0);     // task 6.3 (set passes over all 33 shards)
    NX_CHECK(gauge_add <= 6.0);     // task 6.3
    NX_CHECK(stats_record <= 25.0); // task 7.3
    NX_CHECK(hist_record <= 15.0);  // task 8.3
    NX_CHECK(expo_record <= 15.0);  // task 9.4
#endif
}

void test_latency_strict() {
    metrics::NullMetricSink sink;
    metrics::MetricProviderConfig config{};
    config.collect_interval = std::chrono::nanoseconds{0};
    metrics::MetricProvider provider{config, sink};
    const Result<metrics::Meter*> meter = provider.get_meter("bench", "1.0");
    test_latency_meter(meter);
    if (!meter) {
        return;
    }
    const auto st_counter = (*meter)->create_counter<ST, Readable, Cumulative>("st_inc");
    const auto mt_counter = (*meter)->create_counter<MT, Readable, Cumulative>("mt_inc");
    const auto gauge = (*meter)->create_gauge<std::int64_t, MT, Readable, Cumulative>("gauge");
    const auto stats = (*meter)->create_statistics<MT, Readable, Cumulative>("stats");
    const auto hist = (*meter)->create_explicit_histogram<MT, Readable, Cumulative>(
        "hist", std::span<const double>{std::array<double, 2>{1.0, 2.0}});
    const auto expo = (*meter)->create_exponential_histogram<MT, Readable, Cumulative>("expo", 10);
    test_latency_ready(st_counter, mt_counter, gauge, stats, hist, expo);
    if (!st_counter || !mt_counter || !gauge || !stats || !hist || !expo) {
        return;
    }

    constexpr int batches = 21;
    constexpr int ops = 100'000;
    const double st_inc = test_latency_st_inc(st_counter, batches, ops);
    const double mt_inc = test_latency_mt_inc(mt_counter, batches, ops);
    const double gauge_set = test_latency_gauge_set(gauge, batches, ops);
    const double gauge_add = test_latency_gauge_add(gauge, batches, ops);
    const double stats_record = test_latency_stats_record(stats, batches, ops);
    const double hist_record = test_latency_hist_record(hist, batches, ops);
    const double expo_record = test_latency_exp_record(expo, batches, ops);

    test_latency_report(st_inc, mt_inc, gauge_set, gauge_add, stats_record, hist_record,
                        expo_record);
}
#endif

// --- Hot-path latency gates (spec metrics/counter-gauge etc. "latency gate"
// scenarios; tasks 6.3/7.3/8.3/9.4). Compiled only under
// NEXIGEN_METRICS_STRICT_LATENCY (design D16): Debug and sanitizer builds run
// the correctness-only mode.
void test_latency() {
#if !defined(NEXIGEN_METRICS_STRICT_LATENCY) || defined(NX_SANITIZER_BUILD)
    test_latency_disabled();
#else
    test_latency_strict();
#endif
}

// --- Background harvester and control plane (spec metrics/metric-provider;
// tasks 12.1/12.2).

} // namespace

void suite_latency() {
    run_suite("latency", test_latency);
}
