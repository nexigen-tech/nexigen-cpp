// Split from metrics_test.cpp: nexigen::metrics DescriptiveStatistics.

#include "metrics_test_support.hpp"

namespace {

void test_statistics() {
    metrics::NullMetricSink sink;
    metrics::MetricProvider provider{metrics::MetricProviderConfig{}, sink};
    const Result<metrics::Meter*> meter = provider.get_meter("engine", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter) {
        return;
    }

    // The spec fixture: 2,4,4,4,5,5,7,9 -> mean 5, population std 2.
    const auto stats = (*meter)->create_statistics<ST, Readable, Cumulative>("latency");
    NX_CHECK(stats.has_value());
    if (!stats) {
        return;
    }
    const double fixture[] = {2, 4, 4, 4, 5, 5, 7, 9};
    for (const double value : fixture) {
        (*stats)->record(value);
    }
    const metrics::StatisticsSnapshot summary = (*stats)->read();
    NX_CHECK(summary.count == 8U);
    NX_CHECK(summary.min == 2.0);
    NX_CHECK(summary.max == 9.0);
    NX_CHECK(std::abs(summary.mean - 5.0) < 1e-12);
    NX_CHECK(std::abs(summary.stddev - 2.0) < 1e-12);

    // An empty instrument is well defined.
    const auto empty = (*meter)->create_statistics<ST, Readable, Cumulative>("empty");
    NX_CHECK(empty.has_value());
    if (empty) {
        const metrics::StatisticsSnapshot blank = (*empty)->read();
        NX_CHECK(blank.count == 0U);
        NX_CHECK(blank.min == 0.0 && blank.max == 0.0 && blank.mean == 0.0 && blank.stddev == 0.0);
    }

    // Non-finite samples are ignored and counted.
    (*stats)->record(std::nan(""));
    (*stats)->record(std::numeric_limits<double>::infinity());
    const metrics::StatisticsSnapshot after = (*stats)->read();
    NX_CHECK(after.count == 8U);
    NX_CHECK(after.ignored_samples == 2U);

    // Reset-on-read windows report only the interval's samples.
    const auto windowed = (*meter)->create_statistics<ST, Readable, ResetOnRead>("windowed");
    NX_CHECK(windowed.has_value());
    if (!windowed) {
        return;
    }
    (*windowed)->record(1.0);
    (*windowed)->record(3.0);
    NX_CHECK((*windowed)->read().count == 2U);
    NX_CHECK((*windowed)->read().count == 0U);
    (*windowed)->record(5.0);
    NX_CHECK((*windowed)->read().count == 1U);

    // The Welford core (design D9): the merge matches the single pass.
    metrics::detail::WelfordState whole{};
    metrics::detail::WelfordState part_a{};
    metrics::detail::WelfordState part_b{};
    for (std::size_t i = 0; i < 8; ++i) {
        metrics::detail::welford_add(whole, fixture[i]);
        (i < 4 ? part_a : part_b) = (i < 4) ? part_a : part_b;
        if (i < 4) {
            metrics::detail::welford_add(part_a, fixture[i]);
        } else {
            metrics::detail::welford_add(part_b, fixture[i]);
        }
    }
    const metrics::detail::WelfordState combined = metrics::detail::welford_merge(part_a, part_b);
    NX_CHECK(combined.count == whole.count);
    NX_CHECK(std::abs(combined.mean - whole.mean) < 1e-12);
    NX_CHECK(std::abs(metrics::detail::population_std(combined) -
                      metrics::detail::population_std(whole)) < 1e-12);

    // The serialization surface.
    const std::string text = (*stats)->to_string();
    NX_CHECK(!text.empty());
    NX_CHECK(metrics::to_string(**stats) == text);
    NX_CHECK(std::format("{}", **stats) == text);
    nlohmann::json j = **stats;
    NX_CHECK(j.is_object());
}

// --- explicit-bucket histogram (spec metrics/explicit-histogram).

} // namespace

void suite_statistics() {
    run_suite("statistics", test_statistics);
}
