// Split from metrics_test.cpp: nexigen::metrics policy enums.

#include "metrics_test_support.hpp"

namespace {

void test_policy_names() {
    using metrics::AccessPolicy;
    using metrics::ConcurrencyPolicy;
    using metrics::MetricKind;
    using metrics::ResetPolicy;

    NX_CHECK(metrics::to_string(ConcurrencyPolicy::SingleThreaded) == "SingleThreaded");
    NX_CHECK(metrics::to_string(ConcurrencyPolicy::MultiThreaded) == "MultiThreaded");
    NX_CHECK(metrics::to_string(AccessPolicy::Readable) == "Readable");
    NX_CHECK(metrics::to_string(AccessPolicy::WriteOnly) == "WriteOnly");
    NX_CHECK(metrics::to_string(ResetPolicy::Cumulative) == "Cumulative");
    NX_CHECK(metrics::to_string(ResetPolicy::ResetOnRead) == "ResetOnRead");
    NX_CHECK(metrics::to_string(MetricKind::Counter) == "Counter");
    NX_CHECK(metrics::to_string(MetricKind::Gauge) == "Gauge");
    NX_CHECK(metrics::to_string(MetricKind::DescriptiveStatistics) == "DescriptiveStatistics");
    NX_CHECK(metrics::to_string(MetricKind::ExplicitHistogram) == "ExplicitHistogram");
    NX_CHECK(metrics::to_string(MetricKind::ExponentialHistogram) == "ExponentialHistogram");
    NX_CHECK(metrics::to_string(MetricKind::State) == "State");

    // Garbage-cast values render as "Unknown" and never throw.
    NX_CHECK(metrics::to_string(static_cast<ConcurrencyPolicy>(200)) == "Unknown");
    NX_CHECK(metrics::to_string(static_cast<AccessPolicy>(200)) == "Unknown");
    NX_CHECK(metrics::to_string(static_cast<ResetPolicy>(200)) == "Unknown");
    NX_CHECK(metrics::to_string(static_cast<MetricKind>(200)) == "Unknown");

    // from_string accepts only the canonical spelling and leaves the output
    // untouched otherwise.
    ConcurrencyPolicy concurrency = ConcurrencyPolicy::SingleThreaded;
    NX_CHECK(metrics::from_string("MultiThreaded", concurrency));
    NX_CHECK(concurrency == ConcurrencyPolicy::MultiThreaded);
    NX_CHECK(!metrics::from_string("singlethreaded", concurrency));
    NX_CHECK(concurrency == ConcurrencyPolicy::MultiThreaded);
    NX_CHECK(!metrics::from_string("", concurrency));
    NX_CHECK(concurrency == ConcurrencyPolicy::MultiThreaded);

    AccessPolicy access = AccessPolicy::Readable;
    NX_CHECK(metrics::from_string("WriteOnly", access));
    NX_CHECK(access == AccessPolicy::WriteOnly);
    NX_CHECK(!metrics::from_string("Unknown", access));
    NX_CHECK(access == AccessPolicy::WriteOnly);

    ResetPolicy reset = ResetPolicy::Cumulative;
    NX_CHECK(metrics::from_string("ResetOnRead", reset));
    NX_CHECK(reset == ResetPolicy::ResetOnRead);
    NX_CHECK(!metrics::from_string("resetonread", reset));
    NX_CHECK(reset == ResetPolicy::ResetOnRead);

    MetricKind kind = MetricKind::Counter;
    NX_CHECK(metrics::from_string("ExponentialHistogram", kind));
    NX_CHECK(kind == MetricKind::ExponentialHistogram);
    NX_CHECK(!metrics::from_string("exponential", kind));
    NX_CHECK(kind == MetricKind::ExponentialHistogram);
    NX_CHECK(metrics::from_string("State", kind));
    NX_CHECK(kind == MetricKind::State);
    NX_CHECK(!metrics::from_string("state", kind));
    NX_CHECK(kind == MetricKind::State);

    // Compile-time checks: the enums are byte-sized and constexpr usable.
    static_assert(sizeof(ConcurrencyPolicy) == 1);
    static_assert(sizeof(AccessPolicy) == 1);
    static_assert(sizeof(ResetPolicy) == 1);
    static_assert(sizeof(MetricKind) == 1);
    static_assert(metrics::to_string(ConcurrencyPolicy::MultiThreaded) == "MultiThreaded");
}

void test_policy_json() {
    using metrics::AccessPolicy;
    using metrics::ConcurrencyPolicy;
    using metrics::MetricKind;
    using metrics::ResetPolicy;

    nlohmann::json j = metrics::ConcurrencyPolicy::MultiThreaded;
    NX_CHECK(j.is_string());
    NX_CHECK(j.get<std::string>() == "MultiThreaded");
    ConcurrencyPolicy concurrency = ConcurrencyPolicy::SingleThreaded;
    metrics::from_json(j, concurrency);
    NX_CHECK(concurrency == ConcurrencyPolicy::MultiThreaded);

    j = metrics::AccessPolicy::WriteOnly;
    AccessPolicy access = AccessPolicy::Readable;
    metrics::from_json(j, access);
    NX_CHECK(access == AccessPolicy::WriteOnly);

    j = metrics::ResetPolicy::ResetOnRead;
    ResetPolicy reset = ResetPolicy::Cumulative;
    metrics::from_json(j, reset);
    NX_CHECK(reset == ResetPolicy::ResetOnRead);

    j = metrics::MetricKind::ExponentialHistogram;
    MetricKind kind = MetricKind::Counter;
    metrics::from_json(j, kind);
    NX_CHECK(kind == MetricKind::ExponentialHistogram);

    // Malformed input never aborts and never changes the value.
    const nlohmann::json number = 42;
    metrics::from_json(number, concurrency);
    NX_CHECK(concurrency == ConcurrencyPolicy::MultiThreaded);
    const nlohmann::json object = nlohmann::json{{"a", 1}};
    metrics::from_json(object, access);
    NX_CHECK(access == AccessPolicy::WriteOnly);
    const nlohmann::json array = nlohmann::json::array();
    metrics::from_json(array, reset);
    NX_CHECK(reset == ResetPolicy::ResetOnRead);
    const nlohmann::json null_value = nullptr;
    metrics::from_json(null_value, kind);
    NX_CHECK(kind == MetricKind::ExponentialHistogram);
    const nlohmann::json unknown = "nonsense";
    metrics::from_json(unknown, kind);
    NX_CHECK(kind == MetricKind::ExponentialHistogram);

    // The new State enumerator round-trips through JSON too.
    j = metrics::MetricKind::State;
    metrics::from_json(j, kind);
    NX_CHECK(kind == MetricKind::State);
}

void test_policy_format() {
    NX_CHECK(std::format("{}", metrics::ConcurrencyPolicy::SingleThreaded) == "SingleThreaded");
    NX_CHECK(std::format("{}", metrics::AccessPolicy::Readable) == "Readable");
    NX_CHECK(std::format("{}", metrics::ResetPolicy::Cumulative) == "Cumulative");
    NX_CHECK(std::format("{}", metrics::MetricKind::DescriptiveStatistics) ==
             "DescriptiveStatistics");
    NX_CHECK(std::format("{}", metrics::MetricKind::State) == "State");
    // Width and alignment pass through to the string-view formatter.
    NX_CHECK(std::format("{:>20}", metrics::AccessPolicy::WriteOnly) ==
             std::string(11, ' ') + "WriteOnly");
}

// --- concepts (spec metrics/metric-concepts).

} // namespace

void suite_policy_names() {
    run_suite("policy_names", test_policy_names);
}

void suite_policy_json() {
    run_suite("policy_json", test_policy_json);
}

void suite_policy_format() {
    run_suite("policy_format", test_policy_format);
}
