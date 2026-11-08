// Split from metrics_test.cpp: nexigen::metrics composite sink.

#include "metrics_test_support.hpp"

namespace {

namespace {

/// Minimal movable sink probe: counts deliveries and can be told to fail.
struct ProbeSink {
    int batches{0};
    int flushes{0};
    core::Status batch_status{};
    core::Status flush_status{};

    [[nodiscard]] core::Status on_batch(const metrics::MetricBatchSnapshot&) noexcept {
        ++batches;
        return batch_status;
    }
    [[nodiscard]] core::Status flush() noexcept {
        ++flushes;
        return flush_status;
    }
};
static_assert(metrics::concepts::MetricSink<ProbeSink>);

} // namespace

void test_composite_injection() {
    // A two-record batch for the fan-out checks.
    metrics::MetricRecord records[2]{};
    std::snprintf(records[0].identity.meter_name, sizeof(records[0].identity.meter_name), "engine");
    std::snprintf(records[0].identity.meter_version, sizeof(records[0].identity.meter_version),
                  "1.2.0");
    std::snprintf(records[0].identity.metric_name, sizeof(records[0].identity.metric_name), "hits");
    records[0].kind = metrics::MetricKind::Counter;
    records[0].scalar.counter = 7;
    std::snprintf(records[1].identity.meter_name, sizeof(records[1].identity.meter_name), "engine");
    std::snprintf(records[1].identity.meter_version, sizeof(records[1].identity.meter_version),
                  "1.2.0");
    std::snprintf(records[1].identity.metric_name, sizeof(records[1].identity.metric_name),
                  "latency");
    records[1].kind = metrics::MetricKind::Gauge;
    records[1].scalar.gauge_double = 12.5;
    records[1].scalar.is_double = true;

    metrics::MetricBatchSnapshot batch{};
    batch.records = records;
    batch.record_count = 2;
    batch.record_capacity = 2;

    const auto line_count = [](const char* path) {
        std::FILE* const file = std::fopen(path, "rb");
        if (file == nullptr) {
            return -1;
        }
        int lines = 0;
        int ch = 0;
        while ((ch = std::fgetc(file)) != EOF) {
            if (ch == '\n') {
                ++lines;
            }
        }
        std::fclose(file);
        return lines;
    };

    // Construction with configured members: the exact composite from the
    // pipeline example builds around a path-configured JSON file sink.
    char json_path[] = "/tmp/nexigen_metrics_composite_XXXXXX";
    const int json_fd = ::mkstemp(json_path);
    NX_CHECK(json_fd >= 0);
    ::close(json_fd);
    char console_path[] = "/tmp/nexigen_metrics_console_XXXXXX";
    const int console_fd = ::mkstemp(console_path);
    NX_CHECK(console_fd >= 0);

    metrics::CompositeMetricSink<metrics::ConsoleMetricSink, metrics::JsonFileMetricSink,
                                 metrics::NullMetricSink>
        composite{metrics::ConsoleMetricSink{console_fd},
                  metrics::JsonFileMetricSink{
                      metrics::JsonFileMetricSinkConfig{std::string(json_path), "", false}},
                  metrics::NullMetricSink{}};

    // Typed and indexed access name the same member.
    NX_CHECK(&composite.get<0>() == &composite.get<metrics::ConsoleMetricSink>());
    NX_CHECK(&composite.get<1>() == &composite.get<metrics::JsonFileMetricSink>());

    // Every member receives each batch exactly once: the JSON member emits
    // one line per batch, the console member one line per record.
    NX_CHECK(composite.on_batch(batch).has_value());
    NX_CHECK(composite.on_batch(batch).has_value());
    NX_CHECK(composite.flush().has_value());
    NX_CHECK(line_count(json_path) == 2);
    NX_CHECK(line_count(console_path) == 4);

    // First failure wins without starving the rest: the console member fails
    // (fd = -1 -> SystemError) but the JSON member still writes its line.
    char json2_path[] = "/tmp/nexigen_metrics_composite2_XXXXXX";
    const int json2_fd = ::mkstemp(json2_path);
    NX_CHECK(json2_fd >= 0);
    ::close(json2_fd);
    metrics::CompositeMetricSink<metrics::ConsoleMetricSink, metrics::JsonFileMetricSink,
                                 metrics::NullMetricSink>
        flaky{metrics::ConsoleMetricSink{-1},
              metrics::JsonFileMetricSink{
                  metrics::JsonFileMetricSinkConfig{std::string(json2_path), "", false}},
              metrics::NullMetricSink{}};
    const Status flaky_status = flaky.on_batch(batch);
    NX_CHECK(!flaky_status.has_value());
    NX_CHECK(flaky_status.error() == ErrorCode::SystemError);
    NX_CHECK(line_count(json2_path) == 1);

    // Aggregated flush and exact delivery counts over probes.
    metrics::CompositeMetricSink<ProbeSink, ProbeSink> probes{ProbeSink{}, ProbeSink{}};
    probes.get<0>().batch_status = std::unexpected(ErrorCode::InvalidArgument);
    probes.get<1>().flush_status = std::unexpected(ErrorCode::SystemError);
    const Status probe_batch = probes.on_batch(batch);
    NX_CHECK(!probe_batch.has_value());
    NX_CHECK(probe_batch.error() == ErrorCode::InvalidArgument);
    NX_CHECK(probes.get<0>().batches == 1);
    NX_CHECK(probes.get<1>().batches == 1);
    const Status probe_flush = probes.flush();
    NX_CHECK(!probe_flush.has_value());
    NX_CHECK(probe_flush.error() == ErrorCode::SystemError);
    NX_CHECK(probes.get<0>().flushes == 1);
    NX_CHECK(probes.get<1>().flushes == 1);

    ::unlink(json_path);
    ::unlink(json2_path);
    ::unlink(console_path);
    ::close(console_fd);
}

// --- Records racing a ResetOnRead swap land exactly once (design D9): the
// window sum over concurrent polls equals the recorded count exactly.

} // namespace

void suite_composite_injection() {
    run_suite("composite_injection", test_composite_injection);
}
