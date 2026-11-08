// Split from metrics_test.cpp: nexigen::metrics exponential histogram.

#include "metrics_test_support.hpp"

namespace {

void test_exponential_histogram_meter(const Result<metrics::Meter*>& meter) {
    NX_CHECK(meter.has_value());
}

void test_exponential_histogram_probes() {
    // The mapping is exact (design D11): every value lands in the bucket whose
    // geometric edges [B(i), B(i+1)) contain it with B(i) = 2^(i / 2^scale)
    // (base = 2^(2^-scale)); the mapping is deterministic and monotonic, and
    // exact powers land exactly on their bucket's lower edge. Reference
    // boundaries are rendered here independently of the mapping (pow + exact
    // ldexp scaling).
    const auto ref_edge = [](std::int64_t index, std::int32_t s) {
        const std::int64_t denominator = std::int64_t{1} << s;
        std::int64_t quotient = index / denominator;
        std::int64_t remainder = index - quotient * denominator;
        if (remainder < 0) {
            remainder += denominator;
            --quotient;
        }
        return std::ldexp(
            std::pow(2.0, static_cast<double>(remainder) / static_cast<double>(denominator)),
            static_cast<int>(quotient));
    };
    // previous_value guards the monotonic assert: probe values that round
    // together on the subnormal grid are still checked for containment and
    // determinism, but cannot form a strictly increasing sweep.
    const auto check_value = [&](double value, std::int32_t s, std::int64_t& previous,
                                 double& previous_value) {
        const std::int64_t index = metrics::detail::exponential_index(value, s);
        if (value > previous_value) {
            NX_CHECK(index >= previous); // monotonic on an increasing sweep
            previous = index;
            previous_value = value;
        }
        // Containment for subnormal values is verified in the scaled domain:
        // boundary doubles collapse onto the subnormal grid and cannot decide
        // containment there (the mapping normalizes the same way).
        const double verify_value =
            (value < std::numeric_limits<double>::min()) ? value * 0x1p64 : value;
        const std::int64_t verify_index =
            (verify_value == value) ? index : (index + (std::int64_t{64} << s));
        NX_CHECK(ref_edge(verify_index, s) <= verify_value); // containment
        NX_CHECK(verify_value < ref_edge(verify_index + 1, s));
        NX_CHECK(index == metrics::detail::exponential_index(value, s)); // deterministic
    };
    const double positive_inf = std::numeric_limits<double>::infinity();

    for (std::int32_t probe : {0, 1, 4, 10, 20}) {
        const std::int64_t denom = std::int64_t{1} << probe;
        std::int64_t previous = std::numeric_limits<std::int64_t>::min();
        double previous_value = 0.0;
        // Every exponent: exact powers map exactly, ±1 ULP around each power
        // boundary stays consistent, and interior values are contained.
        for (int exponent = -1074; exponent <= 1023; ++exponent) {
            const double power = std::ldexp(1.0, exponent);
            const std::int64_t index = metrics::detail::exponential_index(power, probe);
            NX_CHECK(index == static_cast<std::int64_t>(exponent) * denom);
            NX_CHECK(metrics::detail::bucket_lower_edge(index, probe) == power);
            // Keep the probe sequence strictly increasing.
            const double below = std::nextafter(power, 0.0);
            if (below > 0.0) {
                check_value(below, probe, previous, previous_value);
            }
            check_value(power, probe, previous, previous_value);
            const double above = std::nextafter(power, positive_inf);
            if (std::isfinite(above)) {
                check_value(above, probe, previous, previous_value);
            }
            check_value(std::ldexp(1.0 + 0.5, exponent), probe, previous, previous_value);
            check_value(std::ldexp(1.0 + 0.999, exponent), probe, previous, previous_value);
        }
        // ±1 ULP around interior boundaries across a central octave range.
        previous = std::numeric_limits<std::int64_t>::min();
        for (int exponent = -30; exponent <= 30; ++exponent) {
            const std::int64_t step = std::max<std::int64_t>(denom / 4, 1);
            for (std::int64_t k = 0; k < denom; k += step) {
                const double edge =
                    ref_edge(static_cast<std::int64_t>(exponent) * denom + k, probe);
                check_value(std::nextafter(edge, 0.0), probe, previous, previous_value);
                check_value(edge, probe, previous, previous_value);
                check_value(std::nextafter(edge, positive_inf), probe, previous, previous_value);
            }
        }
        // Pseudorandom positive doubles (sorted into an increasing sweep).
        constexpr std::uint64_t golden = static_cast<std::uint64_t>(0x9E3779B97F4A7C15ULL);
        constexpr std::uint64_t multiplier = static_cast<std::uint64_t>(6364136223846793005ULL);
        constexpr std::uint64_t increment = static_cast<std::uint64_t>(1442695040888963407ULL);
        constexpr std::uint64_t significand_top = static_cast<std::uint64_t>(0x3FF0000000000000ULL);
        std::uint64_t state = golden + static_cast<std::uint64_t>(probe);
        std::vector<double> randoms;
        randoms.reserve(2000);
        for (int i = 0; i < 2000; ++i) {
            state = state * multiplier + increment;
            const std::uint64_t bits = (state >> 12U) | significand_top;
            const int exponent =
                static_cast<int>((state >> 52U) % static_cast<std::uint64_t>(2046)) - 1023;
            randoms.push_back(std::ldexp(std::bit_cast<double>(bits), exponent));
        }
        std::sort(randoms.begin(), randoms.end());
        previous = std::numeric_limits<std::int64_t>::min();
        for (const double value : randoms) {
            check_value(value, probe, previous, previous_value);
        }
    }
}

void test_exponential_histogram_exact_powers(std::int32_t scale) {
    // Exact powers of two map to the octave boundary.
    NX_CHECK(metrics::detail::exponential_index(1.0, scale) == (0LL << scale));
    NX_CHECK(metrics::detail::exponential_index(1024.0, scale) == (10LL << scale));
}

void test_exponential_histogram_created(
    const Result<metrics::ExponentialHistogram<ST, Readable, Cumulative>*>& histogram) {
    NX_CHECK(histogram.has_value());
}

void test_exponential_histogram_snapshot(
    const Result<metrics::ExponentialHistogram<ST, Readable, Cumulative>*>& histogram,
    std::int32_t scale) {
    (*histogram)->record(1.0);
    (*histogram)->record(2.0);
    (*histogram)->record(4.0);
    (*histogram)->record(0.0);
    (*histogram)->record(-1.0);
    (*histogram)->record(std::nan(""));
    const metrics::HistogramSnapshot snapshot = (*histogram)->read();
    NX_CHECK(snapshot.count == 4U); // 1, 2, 4 and the zero
    NX_CHECK(snapshot.zero_count == 1U);
    NX_CHECK(snapshot.ignored_samples == 2U);
    NX_CHECK(std::abs(snapshot.sum - 7.0) < 1e-12);
    NX_CHECK(snapshot.min == 0.0);
    NX_CHECK(snapshot.max == 4.0);
    NX_CHECK(snapshot.effective_scale <= scale);
    NX_CHECK(snapshot.p50 <= snapshot.p90);
    NX_CHECK(snapshot.p90 <= snapshot.p99);
    NX_CHECK(snapshot.p99 <= snapshot.p999);

    // The bucket totals plus the zeros equal the accepted samples.
    std::uint64_t bucket_total = 0;
    for (std::uint32_t i = 0; i < snapshot.bucket_count; ++i) {
        bucket_total += snapshot.buckets_count[i];
    }
    NX_CHECK(bucket_total + snapshot.zero_count == snapshot.count);
}

void test_exponential_histogram_wide(const Result<metrics::Meter*>& meter) {
    // A wide range coarsens the resolution but never loses a sample.
    const auto wide = (*meter)->create_exponential_histogram<ST, Readable, Cumulative>("wide", 20);
    NX_CHECK(wide.has_value());
    if (wide) {
        const double values[] = {1e-9, 1e-3, 1.0, 1e3, 1e9};
        for (const double value : values) {
            (*wide)->record(value);
        }
        const metrics::HistogramSnapshot wide_snapshot = (*wide)->read();
        NX_CHECK(wide_snapshot.count == 5U);
        NX_CHECK(wide_snapshot.effective_scale < 20);
        std::uint64_t wide_total = 0;
        for (std::uint32_t i = 0; i < wide_snapshot.bucket_count; ++i) {
            wide_total += wide_snapshot.buckets_count[i];
        }
        NX_CHECK(wide_total == 5U);
    }
}

void test_exponential_histogram_reanchor(const Result<metrics::Meter*>& meter) {
    // Loss-free coarsening under a low-side re-anchor (design D10): buckets
    // falling outside the re-anchored window fold into the edge buckets.
    const auto reanchor =
        (*meter)->create_exponential_histogram<ST, Readable, Cumulative>("reanchor", 10);
    NX_CHECK(reanchor.has_value());
    if (reanchor) {
        (*reanchor)->record(2.0);
        (*reanchor)->record(2.2);
        (*reanchor)->record(1.0); // re-anchors the window below its base
        const metrics::HistogramSnapshot reanchor_snapshot = (*reanchor)->read();
        NX_CHECK(reanchor_snapshot.count == 3U);
        std::uint64_t reanchor_total = 0;
        for (std::uint32_t i = 0; i < reanchor_snapshot.bucket_count; ++i) {
            reanchor_total += reanchor_snapshot.buckets_count[i];
        }
        NX_CHECK(reanchor_total == 3U);
    }
}

void test_exponential_histogram_invalid_scale(const Result<metrics::Meter*>& meter) {
    // Out-of-range scales are rejected.
    const auto bad = (*meter)->create_exponential_histogram<ST, Readable, Cumulative>("bad", 21);
    NX_CHECK(!bad.has_value() && bad.error() == ErrorCode::InvalidArgument);
    const auto negative =
        (*meter)->create_exponential_histogram<ST, Readable, Cumulative>("neg", -1);
    NX_CHECK(!negative.has_value() && negative.error() == ErrorCode::InvalidArgument);
}

void test_exponential_histogram_serialization(
    const Result<metrics::ExponentialHistogram<ST, Readable, Cumulative>*>& histogram) {
    // The serialization surface.
    const std::string text = (*histogram)->to_string();
    NX_CHECK(!text.empty());
    NX_CHECK(metrics::to_string(**histogram) == text);
    NX_CHECK(std::format("{}", **histogram) == text);
}

// --- exponential histogram (spec metrics/exponential-histogram).
void test_exponential_histogram() {
    metrics::NullMetricSink sink;
    metrics::MetricProvider provider{metrics::MetricProviderConfig{}, sink};
    const Result<metrics::Meter*> meter = provider.get_meter("engine", "1.0");
    test_exponential_histogram_meter(meter);
    if (!meter) {
        return;
    }

    test_exponential_histogram_probes();

    constexpr std::int32_t scale = 10;
    test_exponential_histogram_exact_powers(scale);

    const auto histogram =
        (*meter)->create_exponential_histogram<ST, Readable, Cumulative>("latency", scale);
    test_exponential_histogram_created(histogram);
    if (!histogram) {
        return;
    }
    test_exponential_histogram_snapshot(histogram, scale);

    test_exponential_histogram_wide(meter);

    test_exponential_histogram_reanchor(meter);

    test_exponential_histogram_invalid_scale(meter);

    test_exponential_histogram_serialization(histogram);
}

// --- Composite member-injection construction (design D3): a composite owns
// configured, non-default-constructible members (the config-constructed JSON
// file sink), fans out without short-circuiting, and aggregates flush.

void exp_hist_containment(metrics::Meter& meter) {
    // Containment at the effective scale after coarsening (task 9.2): the
    // reported edges bracket each recorded value in its own bucket.
    const auto contained =
        meter.create_exponential_histogram<ST, Readable, Cumulative>("contained", 10);
    NX_CHECK(contained.has_value());
    if (contained) {
        for (int k = -3; k <= 3; ++k) {
            (*contained)->record(std::ldexp(1.0, k));
        }
        const metrics::HistogramSnapshot snapshot = (*contained)->read();
        NX_CHECK(snapshot.count == 7U);
        bool strictly_increasing = true;
        bool bracketed = true;
        for (int k = -3; k <= 3; ++k) {
            const double value = std::ldexp(1.0, k);
            bool found = false;
            for (std::uint32_t i = 0; i + 1U < snapshot.bucket_count; ++i) {
                if (snapshot.buckets_count[i] == 0U) {
                    continue;
                }
                if (snapshot.buckets_lower[i] <= value && value < snapshot.buckets_lower[i + 1U]) {
                    found = true;
                    break;
                }
            }
            bracketed = bracketed && found;
        }
        for (std::uint32_t i = 0; i + 1U < snapshot.bucket_count; ++i) {
            if (snapshot.buckets_lower[i + 1U] <= snapshot.buckets_lower[i]) {
                strictly_increasing = false;
            }
        }
        NX_CHECK(bracketed);
        NX_CHECK(strictly_increasing);
    }
}

void exp_hist_empty(metrics::Meter& meter) {
    // Empty histogram reports zeros (task 9.3).
    const auto empty_hist =
        meter.create_exponential_histogram<ST, Readable, Cumulative>("empty", 10);
    NX_CHECK(empty_hist.has_value());
    if (empty_hist) {
        const metrics::HistogramSnapshot snapshot = (*empty_hist)->read();
        NX_CHECK(snapshot.count == 0U);
        NX_CHECK(snapshot.p50 == 0.0 && snapshot.p999 == 0.0);
    }
}

void exp_hist_log_uniform(metrics::Meter& meter) {
    // Log-uniform percentiles land within one bucket width (task 9.3).
    const auto log_uniform =
        meter.create_exponential_histogram<ST, Readable, Cumulative>("log_uniform", 10);
    NX_CHECK(log_uniform.has_value());
    if (log_uniform) {
        for (int i = 0; i <= 80; ++i) {
            (*log_uniform)->record(std::exp2(static_cast<double>(i) / 8.0));
        }
        const metrics::HistogramSnapshot snapshot = (*log_uniform)->read();
        NX_CHECK(snapshot.count == 81U);
        NX_CHECK(snapshot.p50 <= snapshot.p90 && snapshot.p90 <= snapshot.p99 &&
                 snapshot.p99 <= snapshot.p999);
        // True p50 sits at 2^5 = 32; the containing bucket's width bounds the error.
        const double true_p50 = 32.0;
        double tolerance = 0.0;
        for (std::uint32_t i = 0; i + 1U < snapshot.bucket_count; ++i) {
            if (snapshot.buckets_lower[i] <= true_p50 &&
                true_p50 < snapshot.buckets_lower[i + 1U]) {
                tolerance = snapshot.buckets_lower[i + 1U] - snapshot.buckets_lower[i];
                break;
            }
        }
        NX_CHECK(tolerance > 0.0);
        NX_CHECK(std::abs(snapshot.p50 - true_p50) <= tolerance);
    }
}

/// Returns false when the histogram could not be created; the original
/// early-return path skipped the remaining multi-thread cases in that case.
[[nodiscard]] bool exp_hist_sharded(metrics::Meter& meter) {
    // 32 threads x 1M samples: count and bucket totals are exact.
    const auto sharded =
        meter.create_exponential_histogram<MT, Readable, Cumulative>("sharded", 10);
    NX_CHECK(sharded.has_value());
    if (!sharded) {
        return false;
    }
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
        std::uint64_t total = 0;
        for (std::uint32_t i = 0; i < snapshot.bucket_count; ++i) {
            total += snapshot.buckets_count[i];
        }
        NX_CHECK(total == 32'000'000U);
        NX_CHECK(snapshot.sum == 48'000'000.0);
    }
    return true;
}

void exp_hist_windowed(metrics::Meter& meter) {
    // Reset-window exactness under concurrent recording (task 9.3).
    const auto windowed =
        meter.create_exponential_histogram<MT, Readable, ResetOnRead>("windowed_mt", 10);
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

void exp_hist_fallback(metrics::Meter& meter) {
    // >32 threads stay exact through the bucketed fallback shard (task 9.3):
    // bucket totals plus zeros still account for every accepted sample.
    const auto fallback =
        meter.create_exponential_histogram<MT, Readable, Cumulative>("fallback", 10);
    NX_CHECK(fallback.has_value());
    if (fallback) {
        std::vector<std::jthread> workers;
        workers.reserve(40);
        for (int t = 0; t < 40; ++t) {
            workers.emplace_back([&fallback] {
                for (int i = 0; i < 10'000; ++i) {
                    (*fallback)->record(2.5);
                    if (i % 100 == 0) {
                        (*fallback)->record(0.0);
                    }
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }
        const metrics::HistogramSnapshot snapshot = (*fallback)->read();
        std::uint64_t total = 0;
        for (std::uint32_t i = 0; i < snapshot.bucket_count; ++i) {
            total += snapshot.buckets_count[i];
        }
        NX_CHECK(total + snapshot.zero_count == snapshot.count);
        NX_CHECK(snapshot.count == 400'000U + 4'000U);
    }
}

void test_exponential_histogram_mt() {
    metrics::NullMetricSink sink;
    metrics::MetricProvider provider{metrics::MetricProviderConfig{}, sink};
    const Result<metrics::Meter*> meter = provider.get_meter("exponential", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter) {
        return;
    }
    exp_hist_containment(**meter);
    exp_hist_empty(**meter);
    exp_hist_log_uniform(**meter);
    if (!exp_hist_sharded(**meter)) {
        return;
    }
    exp_hist_windowed(**meter);
    exp_hist_fallback(**meter);
}

} // namespace

void suite_exponential_histogram() {
    run_suite("exponential_histogram", test_exponential_histogram);
}

void suite_exponential_histogram_mt() {
    run_suite("exponential_histogram_mt", test_exponential_histogram_mt);
}
