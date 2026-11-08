// Split from metrics_test.cpp: nexigen::metrics concepts.

#include "metrics_test_support.hpp"

namespace {

namespace probes {

struct MissingKind {
    [[maybe_unused]] static constexpr metrics::ConcurrencyPolicy concurrency =
        metrics::ConcurrencyPolicy::SingleThreaded;
    [[maybe_unused]] static constexpr metrics::AccessPolicy access =
        metrics::AccessPolicy::Readable;
    [[maybe_unused]] static constexpr metrics::ResetPolicy reset = metrics::ResetPolicy::Cumulative;
    [[nodiscard]] std::string to_string() const { return "probe"; }
};

struct WrongKindType {
    static constexpr int kind = 0;
    [[maybe_unused]] static constexpr metrics::ConcurrencyPolicy concurrency =
        metrics::ConcurrencyPolicy::SingleThreaded;
    [[maybe_unused]] static constexpr metrics::AccessPolicy access =
        metrics::AccessPolicy::Readable;
    [[maybe_unused]] static constexpr metrics::ResetPolicy reset = metrics::ResetPolicy::Cumulative;
    [[nodiscard]] std::string to_string() const { return "probe"; }
};

struct Conforming {
    static constexpr metrics::MetricKind kind = metrics::MetricKind::Counter;
    static constexpr metrics::ConcurrencyPolicy concurrency =
        metrics::ConcurrencyPolicy::SingleThreaded;
    static constexpr metrics::AccessPolicy access = metrics::AccessPolicy::Readable;
    static constexpr metrics::ResetPolicy reset = metrics::ResetPolicy::Cumulative;
    [[nodiscard]] std::string to_string() const { return "probe"; }
    [[nodiscard]] std::uint64_t read() const { return 0; }
    [[nodiscard]] metrics::MetricRecord snapshot() const { return {}; }
};

struct NotASink {};

} // namespace probes

static_assert(metrics::concepts::Metric<probes::Conforming>);
static_assert(!metrics::concepts::Metric<probes::MissingKind>);
static_assert(!metrics::concepts::Metric<probes::WrongKindType>);
static_assert(metrics::concepts::ReadableMetric<probes::Conforming>);
static_assert(!metrics::concepts::MetricSink<probes::NotASink>);
static_assert(metrics::concepts::MetricSink<metrics::NullMetricSink>);
static_assert(metrics::concepts::MetricSink<metrics::ConsoleMetricSink>);
static_assert(metrics::concepts::MetricSink<metrics::JsonFileMetricSink>);

void test_concepts() {
    // The instruments satisfy the concept in both concurrency modes, and the
    // Readable refinement holds only for Readable instantiations.
    NX_CHECK(
        (metrics::concepts::Metric<
            metrics::Counter<metrics::ConcurrencyPolicy::SingleThreaded,
                             metrics::AccessPolicy::Readable, metrics::ResetPolicy::Cumulative>>));
    NX_CHECK((metrics::concepts::Metric<metrics::Counter<metrics::ConcurrencyPolicy::MultiThreaded,
                                                         metrics::AccessPolicy::WriteOnly,
                                                         metrics::ResetPolicy::ResetOnRead>>));
    NX_CHECK((metrics::concepts::ReadableMetric<
              metrics::Gauge<std::int64_t, metrics::ConcurrencyPolicy::SingleThreaded,
                             metrics::AccessPolicy::Readable, metrics::ResetPolicy::Cumulative>>));
    NX_CHECK(
        !(metrics::concepts::ReadableMetric<
            metrics::Gauge<std::int64_t, metrics::ConcurrencyPolicy::SingleThreaded,
                           metrics::AccessPolicy::WriteOnly, metrics::ResetPolicy::Cumulative>>));
    NX_CHECK((metrics::concepts::ReadableMetric<metrics::DescriptiveStatistics<
                  metrics::ConcurrencyPolicy::MultiThreaded, metrics::AccessPolicy::Readable,
                  metrics::ResetPolicy::ResetOnRead>>));
    NX_CHECK((metrics::concepts::ReadableMetric<metrics::ExplicitBucketHistogram<
                  metrics::ConcurrencyPolicy::MultiThreaded, metrics::AccessPolicy::Readable,
                  metrics::ResetPolicy::Cumulative>>));
    NX_CHECK((metrics::concepts::ReadableMetric<metrics::ExponentialHistogram<
                  metrics::ConcurrencyPolicy::MultiThreaded, metrics::AccessPolicy::Readable,
                  metrics::ResetPolicy::Cumulative>>));

    // The WriteOnly instantiation exposes no read side at all.
    NX_CHECK(
        !(metrics::detail::HasRead<
            metrics::Counter<metrics::ConcurrencyPolicy::SingleThreaded,
                             metrics::AccessPolicy::WriteOnly, metrics::ResetPolicy::Cumulative>>));
    NX_CHECK(!(metrics::detail::HasSnapshot<metrics::ExponentialHistogram<
                   metrics::ConcurrencyPolicy::SingleThreaded, metrics::AccessPolicy::WriteOnly,
                   metrics::ResetPolicy::Cumulative>>));
}

// --- registry and factory hierarchy (spec metrics/metric-provider).

} // namespace

void suite_concepts() {
    run_suite("concepts", test_concepts);
}
