// Split from metrics_test.cpp: nexigen::metrics explicit-bucket histogram.

#include "metrics_test_support.hpp"

namespace {

void test_explicit_histogram() {
    metrics::NullMetricSink sink;
    metrics::MetricProvider provider{metrics::MetricProviderConfig{}, sink};
    const Result<metrics::Meter*> meter = provider.get_meter("engine", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter) {
        return;
    }

    const double bounds[] = {1.0, 2.0, 4.0};

    // The bucket assignment rule: bucket i covers (bounds[i-1], bounds[i]].
    const auto histogram =
        (*meter)->create_explicit_histogram<ST, Readable, Cumulative>("size", bounds);
    NX_CHECK(histogram.has_value());
    if (!histogram) {
        return;
    }
    (*histogram)->record(1.0);
    (*histogram)->record(1.5);
    (*histogram)->record(4.0);
    (*histogram)->record(100.0);
    const metrics::HistogramSnapshot snapshot = (*histogram)->read();
    NX_CHECK(snapshot.count == 4U);
    NX_CHECK(std::abs(snapshot.sum - 106.5) < 1e-12);
    NX_CHECK(snapshot.min == 1.0);
    NX_CHECK(snapshot.max == 100.0);
    NX_CHECK(snapshot.bucket_count == 4U);
    NX_CHECK(snapshot.buckets_count[0] == 1U); // 1.0 -> bound 1
    NX_CHECK(snapshot.buckets_count[1] == 1U); // 1.5 -> bound 2
    NX_CHECK(snapshot.buckets_count[2] == 1U); // 4.0 -> bound 4
    NX_CHECK(snapshot.buckets_count[3] == 1U); // 100 -> overflow

    // Percentiles are ordered.
    NX_CHECK(snapshot.p50 <= snapshot.p90);
    NX_CHECK(snapshot.p90 <= snapshot.p99);
    NX_CHECK(snapshot.p99 <= snapshot.p999);

    // Invalid bounds are rejected.
    const double unsorted[] = {2.0, 1.0};
    const auto bad = (*meter)->create_explicit_histogram<ST, Readable, Cumulative>("bad", unsorted);
    NX_CHECK(!bad.has_value() && bad.error() == ErrorCode::InvalidArgument);
    const double duplicated[] = {1.0, 1.0};
    const auto dup =
        (*meter)->create_explicit_histogram<ST, Readable, Cumulative>("dup", duplicated);
    NX_CHECK(!dup.has_value() && dup.error() == ErrorCode::InvalidArgument);
    const double infinite[] = {std::numeric_limits<double>::infinity()};
    const auto inf = (*meter)->create_explicit_histogram<ST, Readable, Cumulative>("inf", infinite);
    NX_CHECK(!inf.has_value() && inf.error() == ErrorCode::InvalidArgument);

    // Reset-on-read windows.
    const auto windowed =
        (*meter)->create_explicit_histogram<ST, Readable, ResetOnRead>("windowed", bounds);
    NX_CHECK(windowed.has_value());
    if (!windowed) {
        return;
    }
    (*windowed)->record(1.0);
    NX_CHECK((*windowed)->read().count == 1U);
    NX_CHECK((*windowed)->read().count == 0U);

    // The serialization surface.
    const std::string text = (*histogram)->to_string();
    NX_CHECK(!text.empty());
    NX_CHECK(metrics::to_string(**histogram) == text);
    NX_CHECK(std::format("{}", **histogram) == text);
    nlohmann::json j = **histogram;
    NX_CHECK(j.is_object());
}

void test_explicit_histogram_mt_meter(const Result<metrics::Meter*>& meter) {
    NX_CHECK(meter.has_value());
}

void test_explicit_histogram_mt_filtered(const Result<metrics::Meter*>& meter,
                                         std::span<const double> bounds_view) {
    // Non-finite samples are ignored visibly (task 8.1).
    const auto filtered =
        (*meter)->create_explicit_histogram<ST, Readable, Cumulative>("filtered", bounds_view);
    NX_CHECK(filtered.has_value());
    if (filtered) {
        (*filtered)->record(1.5);
        (*filtered)->record(std::nan(""));
        (*filtered)->record(std::numeric_limits<double>::infinity());
        const metrics::HistogramSnapshot snapshot = (*filtered)->read();
        NX_CHECK(snapshot.count == 1U);
        NX_CHECK(snapshot.ignored_samples == 2U);
        NX_CHECK(snapshot.sum == 1.5);
    }
}

void test_explicit_histogram_mt_empty(const Result<metrics::Meter*>& meter,
                                      std::span<const double> bounds_view) {
    // Empty histogram reports zeros (task 8.2).
    const auto empty_hist =
        (*meter)->create_explicit_histogram<ST, Readable, Cumulative>("empty", bounds_view);
    NX_CHECK(empty_hist.has_value());
    if (empty_hist) {
        const metrics::HistogramSnapshot snapshot = (*empty_hist)->read();
        NX_CHECK(snapshot.count == 0U);
        NX_CHECK(snapshot.p50 == 0.0 && snapshot.p90 == 0.0);
        NX_CHECK(snapshot.p99 == 0.0 && snapshot.p999 == 0.0);
    }
}

void test_explicit_histogram_mt_uniform(const Result<metrics::Meter*>& meter,
                                        std::span<const double> bounds_view) {
    // Uniform-distribution percentiles land within one bucket width (task 8.2).
    const auto uniform =
        (*meter)->create_explicit_histogram<ST, Readable, Cumulative>("uniform", bounds_view);
    NX_CHECK(uniform.has_value());
    if (uniform) {
        for (int i = 0; i < 10; ++i) {
            (*uniform)->record(0.5 + static_cast<double>(i)); // 0.5 .. 9.5
        }
        const metrics::HistogramSnapshot snapshot = (*uniform)->read();
        const double bucket_width = 1.0; // uniform unit bounds
        NX_CHECK(std::abs(snapshot.p50 - 5.0) <= bucket_width);
        NX_CHECK(std::abs(snapshot.p90 - 9.0) <= bucket_width);
        NX_CHECK(snapshot.p50 <= snapshot.p90 && snapshot.p90 <= snapshot.p99 &&
                 snapshot.p99 <= snapshot.p999);
    }
}

void test_explicit_histogram_mt_sharded_created(
    const Result<metrics::ExplicitBucketHistogram<MT, Readable, Cumulative>*>& sharded) {
    NX_CHECK(sharded.has_value());
}

void test_explicit_histogram_mt_sharded(
    const Result<metrics::ExplicitBucketHistogram<MT, Readable, Cumulative>*>& sharded) {
    {
        std::vector<std::jthread> workers;
        workers.reserve(32);
        for (int t = 0; t < 32; ++t) {
            workers.emplace_back([&sharded] {
                for (int i = 0; i < 1'000'000; ++i) {
                    (*sharded)->record(1.5);
                }
            });
        }
    } // joined
    {
        const metrics::HistogramSnapshot snapshot = (*sharded)->read();
        NX_CHECK(snapshot.count == 32'000'000U);
        NX_CHECK(snapshot.buckets_count[1] == 32'000'000U);
        NX_CHECK(snapshot.sum == 48'000'000.0);
    }
}

void test_explicit_histogram_mt_windowed(const Result<metrics::Meter*>& meter,
                                         std::span<const double> bounds_view) {
    // Reset-window exactness under concurrent recording (task 8.2).
    const auto windowed =
        (*meter)->create_explicit_histogram<MT, Readable, ResetOnRead>("windowed_mt", bounds_view);
    NX_CHECK(windowed.has_value());
    if (windowed) {
        std::uint64_t windowed_total = 0;
        std::atomic<int> active{16};
        {
            std::vector<std::jthread> workers;
            workers.reserve(16);
            for (int t = 0; t < 16; ++t) {
                workers.emplace_back([&windowed, &active] {
                    for (int i = 0; i < 50'000; ++i) {
                        (*windowed)->record(2.5);
                    }
                    active.fetch_sub(1, std::memory_order_relaxed);
                });
            }
            while (active.load(std::memory_order_relaxed) > 0) {
                windowed_total += (*windowed)->read().count;
            }
        }
        int empty_windows = 0;
        while (empty_windows < 2) {
            const std::uint64_t count = (*windowed)->read().count;
            windowed_total += count;
            empty_windows = count == 0U ? empty_windows + 1 : 0;
        }
        NX_CHECK(windowed_total == 800'000U);
    }
}

void test_explicit_histogram_mt_fallback(const Result<metrics::Meter*>& meter,
                                         std::span<const double> bounds_view) {
    // >32 threads stay exact through the shared fallback shard (task 8.2).
    const auto fallback =
        (*meter)->create_explicit_histogram<MT, Readable, Cumulative>("fallback", bounds_view);
    NX_CHECK(fallback.has_value());
    if (fallback) {
        std::vector<std::jthread> workers;
        workers.reserve(40);
        for (int t = 0; t < 40; ++t) {
            workers.emplace_back([&fallback] {
                for (int i = 0; i < 10'000; ++i) {
                    (*fallback)->record(2.5);
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }
        const metrics::HistogramSnapshot snapshot = (*fallback)->read();
        NX_CHECK(snapshot.count == 400'000U);
        std::uint64_t total = 0;
        for (std::uint32_t i = 0; i < snapshot.bucket_count; ++i) {
            total += snapshot.buckets_count[i];
        }
        NX_CHECK(total == 400'000U);
    }
}

// --- Explicit histogram: ST extras (task 8.1/8.2) and the MT suites
// (32-thread exactness, reset-window exactness, >32-thread fallback).
void test_explicit_histogram_mt() {
    metrics::NullMetricSink sink;
    metrics::MetricProvider provider{metrics::MetricProviderConfig{}, sink};
    const Result<metrics::Meter*> meter = provider.get_meter("explicit", "1.0");
    test_explicit_histogram_mt_meter(meter);
    if (!meter) {
        return;
    }
    const double bounds[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
    const std::span<const double> bounds_view{bounds};

    test_explicit_histogram_mt_filtered(meter, bounds_view);
    test_explicit_histogram_mt_empty(meter, bounds_view);
    test_explicit_histogram_mt_uniform(meter, bounds_view);

    // 32 threads x 1M samples: count and every bucket total are exact.
    const auto sharded =
        (*meter)->create_explicit_histogram<MT, Readable, Cumulative>("sharded", bounds_view);
    test_explicit_histogram_mt_sharded_created(sharded);
    if (!sharded) {
        return;
    }
    test_explicit_histogram_mt_sharded(sharded);

    test_explicit_histogram_mt_windowed(meter, bounds_view);
    test_explicit_histogram_mt_fallback(meter, bounds_view);
}

// --- Exponential histogram: ST extras (task 9.2/9.3) and the MT suites.

} // namespace

void suite_explicit_histogram() {
    run_suite("explicit_histogram", test_explicit_histogram);
}

void suite_explicit_histogram_mt() {
    run_suite("explicit_histogram_mt", test_explicit_histogram_mt);
}
