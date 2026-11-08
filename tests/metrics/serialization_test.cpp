// Split from metrics_test.cpp: nexigen::metrics snapshot serialization.

#include "metrics_test_support.hpp"

namespace {

template <class T>
concept DeserializableFromJson =
    requires(const nlohmann::json& j, const T& value) { from_json(j, value); };

void test_serialization_format(const metrics::MetricIdentity& identity,
                               const metrics::ScalarValue& scalar,
                               const metrics::StatisticsSnapshot& statistics,
                               const metrics::HistogramSnapshot& histogram) {
    // Member/free agreement and std::format equality for every type.
    NX_CHECK(metrics::to_string(identity) == identity.to_string());
    NX_CHECK(metrics::to_string(scalar) == scalar.to_string());
    NX_CHECK(metrics::to_string(statistics) == statistics.to_string());
    NX_CHECK(metrics::to_string(histogram) == histogram.to_string());
    NX_CHECK(std::format("{}", identity) == identity.to_string());
    NX_CHECK(std::format("{}", scalar) == scalar.to_string());
    NX_CHECK(std::format("{}", statistics) == statistics.to_string());
    NX_CHECK(std::format("{}", histogram) == histogram.to_string());
    NX_CHECK(!identity.to_string().empty() && !scalar.to_string().empty());
}

void test_serialization_identity(const metrics::MetricIdentity& identity) {
    {
        nlohmann::json j = identity;
        metrics::MetricIdentity back{};
        metrics::from_json(j, back);
        NX_CHECK(metrics::detail::view_name(back.meter_name) == "engine");
        NX_CHECK(metrics::detail::view_name(back.meter_version) == "1.2.0");
        NX_CHECK(metrics::detail::view_name(back.metric_name) == "hits");
    }
}

void test_serialization_scalar(const metrics::ScalarValue& scalar) {
    {
        nlohmann::json j = scalar;
        metrics::ScalarValue back{};
        metrics::from_json(j, back);
        NX_CHECK(back.counter == 7U && back.gauge_int == -3);
        NX_CHECK(back.gauge_double == 1.5 && back.is_double);
    }
}

void test_serialization_statistics(const metrics::StatisticsSnapshot& statistics) {
    {
        nlohmann::json j = statistics;
        metrics::StatisticsSnapshot back{};
        metrics::from_json(j, back);
        NX_CHECK(back.count == 8U && back.min == 2.0 && back.max == 9.0);
        NX_CHECK(back.mean == 5.0 && back.stddev == 2.0);
        NX_CHECK(back.ignored_samples == 1U && back.dropped_samples == 2U);
    }
}

void test_serialization_histogram(const metrics::HistogramSnapshot& histogram) {
    {
        nlohmann::json j = histogram;
        metrics::HistogramSnapshot back{};
        metrics::from_json(j, back);
        NX_CHECK(back.count == 3U && back.sum == 7.5 && back.min == 1.5 && back.max == 3.5);
        NX_CHECK(back.zero_count == 1U && back.effective_scale == 4);
        NX_CHECK(back.p50 == 2.0 && back.p90 == 3.0 && back.p99 == 3.5 && back.p999 == 3.5);
        NX_CHECK(back.bucket_count == 3U);
        NX_CHECK(back.buckets_lower[2] == 4.0 && back.buckets_count[2] == 1U);
    }
}

void test_serialization_record(const metrics::MetricRecord& record) {
    {
        nlohmann::json j = record;
        NX_CHECK(j.contains("histogram"));
        NX_CHECK(!j.contains("scalar")); // only the active union member
        metrics::MetricRecord back{};
        metrics::from_json(j, back);
        NX_CHECK(back.kind == metrics::MetricKind::ExplicitHistogram);
        NX_CHECK(back.concurrency == MT && back.reset == ResetOnRead);
        NX_CHECK(back.histogram.count == 3U && back.histogram.sum == 7.5);
        NX_CHECK(back.histogram.p999 == 3.5 && back.histogram.bucket_count == 3U);
        NX_CHECK(back.histogram.buckets_count[1] == 1U);
        NX_CHECK(metrics::detail::view_name(back.identity.metric_name) == "hits");
    }
}

void test_serialization_batch(const metrics::MetricRecord& record,
                              const metrics::MetricIdentity& identity) {
    // A multi-record MetricBatchSnapshot round-trips count, timestamp and
    // per-record contents.
    metrics::MetricRecord records[2]{};
    records[0] = record;
    records[1].identity = identity;
    records[1].kind = metrics::MetricKind::Counter;
    records[1].scalar.counter = 42;
    metrics::MetricBatchSnapshot batch{};
    batch.stamp = chrono::Timestamp{123456789ULL};
    batch.utc_ns = 1'700'000'000'123'456'789LL;
    batch.records = records;
    batch.record_count = 2;
    batch.record_capacity = 2;
    {
        nlohmann::json j = batch;
        metrics::MetricRecord buffer[2]{};
        metrics::MetricBatchSnapshot back{};
        back.records = buffer;
        back.record_capacity = 2;
        metrics::from_json(j, back);
        NX_CHECK(back.record_count == 2U);
        NX_CHECK(back.stamp.nanoseconds() == 123456789ULL);
        NX_CHECK(back.utc_ns == 1'700'000'000'123'456'789LL);
        NX_CHECK(buffer[0].kind == metrics::MetricKind::ExplicitHistogram);
        NX_CHECK(buffer[0].histogram.count == 3U);
        NX_CHECK(buffer[1].kind == metrics::MetricKind::Counter);
        NX_CHECK(buffer[1].scalar.counter == 42U);
    }
    NX_CHECK(metrics::to_string(batch) == batch.to_string());
    NX_CHECK(std::format("{}", batch) == batch.to_string());
}

// --- Snapshot/record/config serialization (spec metrics/policy-enums
// "Public type ..."; tasks 4.1/4.2/10.3).
void test_serialization() {
    metrics::MetricIdentity identity{};
    std::snprintf(identity.meter_name, sizeof(identity.meter_name), "engine");
    std::snprintf(identity.meter_version, sizeof(identity.meter_version), "1.2.0");
    std::snprintf(identity.metric_name, sizeof(identity.metric_name), "hits");

    metrics::ScalarValue scalar{};
    scalar.counter = 7;
    scalar.gauge_int = -3;
    scalar.gauge_double = 1.5;
    scalar.is_double = true;

    metrics::StatisticsSnapshot statistics{};
    statistics.count = 8;
    statistics.min = 2.0;
    statistics.max = 9.0;
    statistics.mean = 5.0;
    statistics.stddev = 2.0;
    statistics.ignored_samples = 1;
    statistics.dropped_samples = 2;

    metrics::HistogramSnapshot histogram{};
    histogram.count = 3;
    histogram.sum = 7.5;
    histogram.min = 1.5;
    histogram.max = 3.5;
    histogram.zero_count = 1;
    histogram.effective_scale = 4;
    histogram.p50 = 2.0;
    histogram.p90 = 3.0;
    histogram.p99 = 3.5;
    histogram.p999 = 3.5;
    histogram.bucket_count = 3;
    histogram.buckets_lower[0] = 1.0;
    histogram.buckets_lower[1] = 2.0;
    histogram.buckets_lower[2] = 4.0;
    histogram.buckets_count[0] = 1;
    histogram.buckets_count[1] = 1;
    histogram.buckets_count[2] = 1;

    test_serialization_format(identity, scalar, statistics, histogram);

    // JSON round-trips of populated snapshots under JSON_NOEXCEPTION=1.
    test_serialization_identity(identity);
    test_serialization_scalar(scalar);
    test_serialization_statistics(statistics);
    test_serialization_histogram(histogram);

    // A histogram MetricRecord round-trips every payload field.
    metrics::MetricRecord record{};
    record.identity = identity;
    record.kind = metrics::MetricKind::ExplicitHistogram;
    record.concurrency = MT;
    record.access = Readable;
    record.reset = ResetOnRead;
    record.histogram = histogram;
    test_serialization_record(record);

    test_serialization_batch(record, identity);

    // Factory-only types are not deserializable (compile-time absence).
    static_assert(!DeserializableFromJson<metrics::Counter<ST, Readable, Cumulative>>);
    static_assert(!DeserializableFromJson<metrics::Gauge<std::int64_t, ST, Readable, Cumulative>>);
    static_assert(
        !DeserializableFromJson<metrics::DescriptiveStatistics<ST, Readable, Cumulative>>);
    static_assert(
        !DeserializableFromJson<metrics::ExplicitBucketHistogram<ST, Readable, Cumulative>>);
    static_assert(!DeserializableFromJson<metrics::ExponentialHistogram<ST, Readable, Cumulative>>);
    static_assert(!DeserializableFromJson<metrics::CompositeMetricSink<metrics::NullMetricSink>>);
}

// --- Slot exhaustion is visible (spec metrics/descriptive-statistics
// "Multi-threaded statistics merging"; task 7.2).

void test_writer_bounds_full() {
    char small[3]{};
    metrics::detail::FixedWriter writer{small, sizeof small};
    NX_CHECK(writer.append_char('a'));
    NX_CHECK(writer.append_sv("bc"));
    NX_CHECK(!writer.append_char('d')); // full
    NX_CHECK(!writer.ok());
    NX_CHECK(!writer.append_sv("x")); // already overflowed
    NX_CHECK(!writer.append_u64(1));
    NX_CHECK(!writer.append_i64(-1));
    NX_CHECK(!writer.append_double(1.0));
    NX_CHECK(!writer.append_escaped("x"));

    char buffer[128]{};
    metrics::detail::FixedWriter all{buffer, sizeof buffer};
    NX_CHECK(all.append_u64(std::numeric_limits<std::uint64_t>::max()));
    NX_CHECK(all.append_i64(std::numeric_limits<std::int64_t>::min()));
    NX_CHECK(all.append_double(-0.0));
    NX_CHECK(all.append_escaped("q\"\\\b\f\n\r\t\x02 and UTF-8"));
    NX_CHECK(all.ok());

    char tiny[4]{};
    metrics::detail::FixedWriter escaped{tiny, sizeof tiny};
    NX_CHECK(!escaped.append_escaped("\x01")); // escape expansion overflows
    NX_CHECK(!escaped.ok());
}

void test_sink_kind_matrix() {
    auto make_batch = [](metrics::MetricRecord* records, std::uint32_t count) {
        metrics::MetricBatchSnapshot batch{};
        batch.records = records;
        batch.record_count = count;
        batch.record_capacity = count;
        return batch;
    };

    metrics::MetricRecord records[7]{};
    for (auto& record : records) {
        std::snprintf(record.identity.meter_name, sizeof(record.identity.meter_name), "meter");
        std::snprintf(record.identity.meter_version, sizeof(record.identity.meter_version), "1.0");
        std::snprintf(record.identity.metric_name, sizeof(record.identity.metric_name), "metric");
    }
    records[0].kind = metrics::MetricKind::Counter;
    records[0].scalar.counter = std::numeric_limits<std::uint64_t>::max();
    std::snprintf(records[0].identity.unit, sizeof(records[0].identity.unit), "count");
    records[1].kind = metrics::MetricKind::Gauge;
    records[1].scalar.gauge_int = std::numeric_limits<std::int64_t>::min();
    records[1].scalar.is_double = false;
    records[2].kind = metrics::MetricKind::Gauge;
    records[2].scalar.gauge_double = 1.25;
    records[2].scalar.is_double = true;
    records[3].kind = metrics::MetricKind::DescriptiveStatistics;
    records[3].statistics = metrics::StatisticsSnapshot{5, -1.0, 2.0, 0.5, 1.5, 0, 0};
    records[4].kind = metrics::MetricKind::ExplicitHistogram;
    records[4].histogram.count = 4;
    records[4].histogram.sum = 10.0;
    records[4].histogram.min = 1.0;
    records[4].histogram.max = 4.0;
    records[4].histogram.p50 = 2.0;
    records[4].histogram.p90 = 3.0;
    records[4].histogram.p99 = 4.0;
    records[4].histogram.p999 = 4.0;
    records[5].kind = metrics::MetricKind::ExponentialHistogram;
    records[5].histogram = records[4].histogram;
    records[6].kind = metrics::MetricKind::State;
    std::snprintf(records[6].state.value, sizeof(records[6].state.value), "degraded");
    const metrics::MetricBatchSnapshot batch = make_batch(records, 7);

    char path[] = "/tmp/nexigen_metrics_kinds_XXXXXX";
    const int made = ::mkstemp(path);
    NX_CHECK(made >= 0);
    ::close(made);
    {
        metrics::JsonFileMetricSink sink{
            metrics::JsonFileMetricSinkConfig{std::string(path), "", false}};
        NX_CHECK(sink.is_open());
        NX_CHECK(sink.on_batch(batch).has_value());
        NX_CHECK(sink.flush().has_value());
        std::ifstream input(path);
        std::string line;
        NX_CHECK(static_cast<bool>(std::getline(input, line)));
        const nlohmann::json parsed = nlohmann::json::parse(line, nullptr, false);
        NX_CHECK(!parsed.is_discarded());
        NX_CHECK(parsed["records"].size() == 7U);
        NX_CHECK(parsed["records"][0]["value"] == std::numeric_limits<std::uint64_t>::max());
        NX_CHECK(parsed["records"][0]["unit"] == "count");
        NX_CHECK(!parsed["records"][1].contains("unit"));
        NX_CHECK(parsed["records"][3].contains("std"));
        NX_CHECK(parsed["records"][4].contains("p999"));
        NX_CHECK(parsed["records"][6]["kind"] == "State");
        NX_CHECK(parsed["records"][6]["value"] == "degraded");
        NX_CHECK(!parsed["records"][6].contains("count"));
    }

    // Escaping classes in every escaped field.
    metrics::MetricRecord escaped[1]{};
    std::snprintf(escaped[0].identity.meter_name, sizeof(escaped[0].identity.meter_name),
                  "m\"\\\b\f\n\r\t\x02\x1f");
    std::snprintf(escaped[0].identity.meter_version, sizeof(escaped[0].identity.meter_version),
                  "v-Cyr");
    std::snprintf(escaped[0].identity.metric_name, sizeof(escaped[0].identity.metric_name),
                  "Кириллица");
    escaped[0].kind = metrics::MetricKind::Counter;
    escaped[0].scalar.counter = 1;
    {
        metrics::JsonFileMetricSink sink{
            metrics::JsonFileMetricSinkConfig{std::string(path), "", false}};
        NX_CHECK(sink.on_batch(make_batch(escaped, 1)).has_value());
        NX_CHECK(sink.flush().has_value());
        std::ifstream input(path);
        std::string line;
        NX_CHECK(static_cast<bool>(std::getline(input, line)));
        const nlohmann::json parsed = nlohmann::json::parse(line, nullptr, false);
        NX_CHECK(!parsed.is_discarded());
        NX_CHECK(parsed["records"][0]["metric"] == "Кириллица");
    }

    // Batch edge cases: null records, empty batch, overflow (BufferFull).
    {
        metrics::JsonFileMetricSink sink{
            metrics::JsonFileMetricSinkConfig{std::string(path), "", false}};
        metrics::MetricBatchSnapshot null_batch{};
        null_batch.records = nullptr;
        null_batch.record_count = 1;
        NX_CHECK(sink.on_batch(null_batch).has_value());
        NX_CHECK(sink.on_batch(make_batch(records, 0)).has_value());

        metrics::MetricRecord many[120]{};
        for (auto& record : many) {
            record.kind = metrics::MetricKind::Counter;
            std::memset(record.identity.meter_name, 'a', sizeof(record.identity.meter_name) - 1U);
            record.identity.meter_name[sizeof(record.identity.meter_name) - 1U] = '\0';
            record.scalar.counter = 1;
        }
        const Status overflow = sink.on_batch(make_batch(many, 120));
        NX_CHECK(!overflow.has_value());
        NX_CHECK(overflow.error() == ErrorCode::BufferFull);
    }

    // Console sink covers the statistics/histogram branches and errors.
    int fds[2]{-1, -1};
    NX_CHECK(::pipe(fds) == 0);
    {
        metrics::ConsoleMetricSink console{fds[1]};
        NX_CHECK(console.on_batch(batch).has_value());
        metrics::ConsoleMetricSink broken{-1};
        NX_CHECK(!broken.on_batch(batch).has_value());
        NX_CHECK(broken.flush().has_value()); // console flushes are no-ops
        ::close(fds[1]);
        char buffer[8192];
        const ssize_t bytes = ::read(fds[0], buffer, sizeof(buffer) - 1U);
        NX_CHECK(bytes > 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            const std::string_view text(buffer, static_cast<std::size_t>(bytes));
            NX_CHECK(text.find("DescriptiveStatistics") != std::string_view::npos);
            NX_CHECK(text.find("ExplicitHistogram") != std::string_view::npos);
            NX_CHECK(text.find("p999") != std::string_view::npos);
        }
        ::close(fds[0]);
    }

    // Failed open: on_batch and flush both report SystemError.
    {
        metrics::JsonFileMetricSink broken{
            metrics::JsonFileMetricSinkConfig{"/nonexistent-nexigen/x.jsonl", "", false}};
        NX_CHECK(!broken.is_open());
        NX_CHECK(!broken.on_batch(batch).has_value());
        NX_CHECK(!broken.flush().has_value());
    }
    ::unlink(path);
}

// --- snapshot serialization matrix (change improve-test-coverage):
// every value type round-trips through to_json/from_json and to_string.
void test_snapshot_serialization_full() {
    metrics::MetricIdentity identity{};
    std::snprintf(identity.meter_name, sizeof(identity.meter_name), "meter");
    std::snprintf(identity.meter_version, sizeof(identity.meter_version), "1.0");
    std::snprintf(identity.metric_name, sizeof(identity.metric_name), "metric");
    std::snprintf(identity.unit, sizeof(identity.unit), "ns");
    {
        nlohmann::json json;
        metrics::to_json(json, identity);
        metrics::MetricIdentity back{};
        metrics::from_json(json, back);
        NX_CHECK(std::string_view(back.meter_name) == "meter");
        NX_CHECK(std::string_view(back.unit) == "ns");
        NX_CHECK(identity.to_string().find("meter") != std::string::npos);
        NX_CHECK(metrics::to_string(identity) == identity.to_string());
    }
    {
        metrics::ScalarValue value{};
        value.counter = 7;
        value.gauge_int = -3;
        value.gauge_double = 1.5;
        value.is_double = true;
        nlohmann::json json;
        metrics::to_json(json, value);
        metrics::ScalarValue back{};
        metrics::from_json(json, back);
        NX_CHECK(back.is_double == true);
        NX_CHECK(back.gauge_double == 1.5);
        NX_CHECK(value.to_string().find("double=") != std::string::npos);
        value.is_double = false;
        NX_CHECK(value.to_string().find("int64=") != std::string::npos);
    }
    {
        metrics::StatisticsSnapshot stats{5, -1.0, 2.0, 0.5, 1.5, 2, 3};
        nlohmann::json json;
        metrics::to_json(json, stats);
        metrics::StatisticsSnapshot back{};
        metrics::from_json(json, back);
        NX_CHECK(back.count == 5U && back.ignored_samples == 2U);
        NX_CHECK(stats.to_string().find("ignored=2") != std::string::npos);
    }
    {
        metrics::HistogramSnapshot histogram{};
        histogram.count = 4;
        histogram.sum = 10.0;
        histogram.min = 1.0;
        histogram.max = 4.0;
        histogram.zero_count = 1;
        histogram.effective_scale = 2;
        histogram.p50 = 2.0;
        histogram.p90 = 3.0;
        histogram.p99 = 4.0;
        histogram.p999 = 4.0;
        histogram.bucket_count = 2;
        histogram.buckets_lower[0] = 0.0;
        histogram.buckets_lower[1] = 2.0;
        histogram.buckets_count[0] = 2;
        histogram.buckets_count[1] = 2;
        nlohmann::json json;
        metrics::to_json(json, histogram);
        metrics::HistogramSnapshot back{};
        metrics::from_json(json, back);
        NX_CHECK(back.bucket_count == 2U);
        NX_CHECK(histogram.to_string().find("p999=4") != std::string::npos);
    }
    {
        metrics::MetricRecord record{};
        record.identity = identity;
        record.kind = metrics::MetricKind::Counter;
        record.scalar.counter = 9;
        nlohmann::json json;
        metrics::to_json(json, record);
        metrics::MetricRecord back{};
        metrics::from_json(json, back);
        NX_CHECK(back.kind == metrics::MetricKind::Counter);
        NX_CHECK(record.to_string().find("Counter") != std::string::npos);
        record.kind = metrics::MetricKind::DescriptiveStatistics;
        record.statistics.count = 1;
        NX_CHECK(record.to_string().find("StatisticsSnapshot") != std::string::npos);
        record.kind = metrics::MetricKind::ExplicitHistogram;
        NX_CHECK(record.to_string().find("HistogramSnapshot") != std::string::npos);
    }
    {
        metrics::MetricRecord stats_record{};
        stats_record.identity = identity;
        stats_record.kind = metrics::MetricKind::DescriptiveStatistics;
        stats_record.statistics.count = 1;
        stats_record.statistics.mean = 2.0;
        nlohmann::json json;
        metrics::to_json(json, stats_record);
        metrics::MetricRecord back{};
        metrics::from_json(json, back);
        NX_CHECK(back.kind == metrics::MetricKind::DescriptiveStatistics);
        NX_CHECK(back.statistics.count == 1U);
        NX_CHECK(back.statistics.mean == 2.0);

        metrics::MetricRecord state_record{};
        state_record.identity = identity;
        state_record.kind = metrics::MetricKind::State;
        std::snprintf(state_record.state.value, sizeof(state_record.state.value), "up");
        metrics::to_json(json, state_record);
        metrics::from_json(json, back);
        NX_CHECK(back.kind == metrics::MetricKind::State);
        NX_CHECK(std::string_view(back.state.value) == "up");
    }
    {
        metrics::MetricRecord records[1]{};
        records[0].identity = identity;
        metrics::MetricBatchSnapshot batch{};
        batch.records = records;
        batch.record_count = 1;
        batch.record_capacity = 1;
        batch.utc_ns = 42;
        nlohmann::json json;
        metrics::to_json(json, batch);
        metrics::MetricBatchSnapshot back{};
        back.records = records;
        back.record_capacity = 1;
        metrics::from_json(json, back);
        NX_CHECK(back.record_count == 1U);
        NX_CHECK(batch.to_string().find("record_count") == std::string::npos);
        NX_CHECK(batch.to_string().find("records=1") != std::string::npos);
    }
    {
        metrics::MetricProviderConfig config{};
        config.max_counters = 12;
        nlohmann::json json;
        metrics::to_json(json, config);
        metrics::MetricProviderConfig back{};
        metrics::from_json(json, back);
        NX_CHECK(back.max_counters == 12U);
        NX_CHECK(config.to_string().find("max_counters=12") != std::string::npos);

        nlohmann::json legacy = {{"max_counters", 12U}, {"core_mask", 0x5U}};
        metrics::MetricProviderConfig legacy_back{};
        metrics::from_json(legacy, legacy_back);
        NX_CHECK(legacy_back.max_counters == 12U);
        NX_CHECK(legacy_back.worker_thread.cpus().contains(0U));
    }
    // Hostile shapes leave targets untouched.
    {
        metrics::MetricIdentity keep{};
        std::snprintf(keep.meter_name, sizeof(keep.meter_name), "keep");
        metrics::from_json(nlohmann::json(42), keep);
        NX_CHECK(std::string_view(keep.meter_name) == "keep");
        metrics::MetricProviderConfig config_keep{};
        config_keep.max_counters = 1;
        metrics::from_json(nlohmann::json::array(), config_keep);
        NX_CHECK(config_keep.max_counters == 1U);
    }
    // Wrong-typed fields on every value type (readers are non-throwing).
    {
        metrics::MetricIdentity identity_keep{};
        std::snprintf(identity_keep.meter_name, sizeof(identity_keep.meter_name), "keep");
        metrics::from_json(
            nlohmann::json{{"meter_name", 1}, {"meter_version", "v"}, {"metric_name", "m"}},
            identity_keep);
        NX_CHECK(std::string_view(identity_keep.meter_name) == "keep");
        metrics::ScalarValue scalar_keep{};
        scalar_keep.counter = 3;
        metrics::from_json(nlohmann::json{{"counter", "x"}, {"gauge_int", "x"}}, scalar_keep);
        NX_CHECK(scalar_keep.counter == 3U);
        metrics::StatisticsSnapshot stats_keep{};
        stats_keep.count = 2;
        metrics::from_json(nlohmann::json{{"count", "x"}, {"min", "x"}}, stats_keep);
        NX_CHECK(stats_keep.count == 2U);
        metrics::HistogramSnapshot hist_keep{};
        hist_keep.count = 2;
        metrics::from_json(nlohmann::json{{"count", "x"}, {"buckets", "x"}}, hist_keep);
        NX_CHECK(hist_keep.count == 2U);
        metrics::MetricRecord record_keep{};
        record_keep.kind = metrics::MetricKind::Counter;
        metrics::from_json(nlohmann::json{{"kind", "Counter"}, {"scalar", "x"}}, record_keep);
        NX_CHECK(record_keep.kind == metrics::MetricKind::Counter);
        metrics::MetricBatchSnapshot batch_keep{};
        batch_keep.record_count = 1;
        metrics::from_json(nlohmann::json{{"record_count", "x"}}, batch_keep);
        NX_CHECK(batch_keep.record_count == 1U);
        metrics::MetricProviderConfig config_keep2{};
        config_keep2.max_gauges = 4;
        metrics::from_json(nlohmann::json{{"max_gauges", "x"}}, config_keep2);
        NX_CHECK(config_keep2.max_gauges == 4U);
    }
    // to_json for the remaining record kinds and free to_string helpers.
    {
        metrics::MetricRecord record{};
        record.identity = identity;
        record.kind = metrics::MetricKind::Gauge;
        record.scalar.is_double = true;
        record.scalar.gauge_double = 2.0;
        nlohmann::json json;
        metrics::to_json(json, record);
        NX_CHECK(json["kind"] == "Gauge");
        record.kind = metrics::MetricKind::DescriptiveStatistics;
        record.statistics.count = 1;
        metrics::to_json(json, record);
        NX_CHECK(json["kind"] == "DescriptiveStatistics");
        record.kind = metrics::MetricKind::ExponentialHistogram;
        metrics::to_json(json, record);
        NX_CHECK(json["kind"] == "ExponentialHistogram");
        metrics::MetricBatchSnapshot batch{};
        NX_CHECK(!metrics::to_string(batch).empty());
        NX_CHECK(metrics::to_string(record) == record.to_string());
        NX_CHECK(metrics::to_string(identity) == identity.to_string());
    }
}

} // namespace

void suite_serialization() {
    run_suite("serialization", test_serialization);
}

void suite_writer_bounds_full() {
    run_suite("writer_bounds_full", test_writer_bounds_full);
}

void suite_sink_kind_matrix() {
    run_suite("sink_kind_matrix", test_sink_kind_matrix);
}

void suite_snapshot_serialization_full() {
    run_suite("snapshot_serialization_full", test_snapshot_serialization_full);
}
