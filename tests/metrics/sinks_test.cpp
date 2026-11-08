// Split from metrics_test.cpp: nexigen::metrics sinks.

#include "metrics_test_support.hpp"

namespace {

void test_sinks_null_console() {
    // Null sink: a true no-op under an armed arena.
    {
        metrics::NullMetricSink null_sink;
        metrics::MetricRecord record{};
        record.scalar.counter = 1;
        metrics::MetricRecord records[1]{record};
        metrics::MetricBatchSnapshot batch{};
        batch.records = records;
        batch.record_count = 1;
        batch.record_capacity = 1;
        const std::size_t armed = g_alloc_used;
        const Status delivered = null_sink.on_batch(batch);
        const Status flushed = null_sink.flush();
        NX_CHECK(delivered.has_value() && flushed.has_value());
        NX_CHECK(g_alloc_used == armed);
        NX_CHECK(metrics::to_string(null_sink) == null_sink.to_string());
        nlohmann::json j = null_sink;
        NX_CHECK(j.is_object());
        metrics::NullMetricSink parsed{};
        metrics::from_json(j, parsed);
    }

    // Console sink: a pipe fixture renders metric names and values.
    int fds[2]{-1, -1};
    NX_CHECK(::pipe(fds) == 0);
    {
        metrics::MetricRecord records[2]{};
        std::snprintf(records[0].identity.metric_name, sizeof(records[0].identity.metric_name),
                      "pipe_metric");
        records[0].kind = metrics::MetricKind::Counter;
        records[0].scalar.counter = 7;
        std::snprintf(records[1].identity.metric_name, sizeof(records[1].identity.metric_name),
                      "second_metric");
        records[1].kind = metrics::MetricKind::Gauge;
        records[1].scalar.gauge_int = -11;
        records[1].scalar.is_double = false;
        metrics::MetricBatchSnapshot batch{};
        batch.records = records;
        batch.record_count = 2;
        batch.record_capacity = 2;

        metrics::ConsoleMetricSink console{fds[1]};
        NX_CHECK(console.on_batch(batch).has_value());
        NX_CHECK(console.flush().has_value());
        ::close(fds[1]);
        char buffer[4096];
        const ssize_t bytes = ::read(fds[0], buffer, sizeof(buffer) - 1U);
        NX_CHECK(bytes > 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            const std::string_view text(buffer, static_cast<std::size_t>(bytes));
            NX_CHECK(text.find("pipe_metric") != std::string_view::npos);
            NX_CHECK(text.find("second_metric") != std::string_view::npos);
            NX_CHECK(text.find("7") != std::string_view::npos);
        }
        ::close(fds[0]);

        // Closed descriptor: SystemError, no abort.
        metrics::ConsoleMetricSink broken{-1};
        const Status failed = broken.on_batch(batch);
        NX_CHECK(!failed.has_value());
        NX_CHECK(failed.error() == ErrorCode::SystemError);

        // Diagnostics / JSON contract (the fd round-trips).
        NX_CHECK(metrics::to_string(console) == console.to_string());
        NX_CHECK(std::format("{}", console) == console.to_string());
        NX_CHECK(console.to_string().find("fd=") != std::string::npos);
        nlohmann::json j = console;
        metrics::ConsoleMetricSink restored{99};
        metrics::from_json(j, restored);
        NX_CHECK(restored.to_string() == console.to_string()); // fd round-trips
    }
}

void test_sinks_json_tempfile(char* path) {
    const int made = ::mkstemp(path);
    NX_CHECK(made >= 0);
    ::close(made);
}

void test_sinks_json_opened(metrics::JsonFileMetricSink& sink) {
    {
        // Kept in its own scope so the assertion text is byte-identical.
        NX_CHECK(sink.is_open());
    }
}

void test_sinks_json_timestamp(const nlohmann::json& parsed);
void test_sinks_json_records(const nlohmann::json& parsed);

void test_sinks_json_write(const char* path, metrics::JsonFileMetricSink& sink,
                           const metrics::MetricBatchSnapshot& batch, const auto& read_lines) {
    {
        NX_CHECK(sink.on_batch(batch).has_value());
        NX_CHECK(sink.on_batch(batch).has_value());
        NX_CHECK(sink.flush().has_value());

        const std::vector<std::string> lines = read_lines(path);
        NX_CHECK(lines.size() == 2U); // one parseable JSON object per batch
        for (const std::string& line : lines) {
            const nlohmann::json parsed = nlohmann::json::parse(line, nullptr, false);
            NX_CHECK(!parsed.is_discarded() && parsed.is_object());
            if (parsed.is_object()) {
                test_sinks_json_timestamp(parsed);
                test_sinks_json_records(parsed);
            }
        }
    }
}

void test_sinks_json_timestamp(const nlohmann::json& parsed) {
    {
        {
            {
                const auto stamp_it = parsed.find("timestamp");
                NX_CHECK(stamp_it != parsed.end() && stamp_it->is_string());
                if (stamp_it != parsed.end() && stamp_it->is_string()) {
                    const std::string stamp = stamp_it->get<std::string>();
                    NX_CHECK(stamp.size() == 30U); // YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ
                    NX_CHECK(stamp[10] == 'T' && stamp[19] == '.' && stamp[29] == 'Z');
                }
            }
        }
    }
}

void test_sinks_json_records(const nlohmann::json& parsed) {
    {
        {
            {
                const auto records_it = parsed.find("records");
                NX_CHECK(records_it != parsed.end() && records_it->is_array());
                if (records_it != parsed.end() && records_it->is_array() && !records_it->empty()) {
                    const auto meter_it = (*records_it)[0].find("meter");
                    NX_CHECK(meter_it != (*records_it)[0].end() && meter_it->is_string());
                    if (meter_it != (*records_it)[0].end() && meter_it->is_string()) {
                        // Escaped strings round-trip.
                        NX_CHECK(meter_it->get<std::string>() == "quote\"slash\\ctrl\x01");
                    }
                }
            }
        }
    }
}

void test_sinks_json_append(const char* path, const metrics::MetricBatchSnapshot& batch,
                            const auto& read_lines) {
    {
        // Append ordering: a second sink appends one more line.
        metrics::JsonFileMetricSinkConfig appended{std::string(path), "", true};
        metrics::JsonFileMetricSink append_sink{appended};
        NX_CHECK(append_sink.on_batch(batch).has_value());
        NX_CHECK(append_sink.flush().has_value());
        NX_CHECK(read_lines(path).size() == 3U);
    }
}

void test_sinks_json_diagnostics(metrics::JsonFileMetricSink& sink,
                                 const metrics::JsonFileMetricSinkConfig& config) {
    {
        // Diagnostics + config trio; the sink itself is not deserializable.
        NX_CHECK(metrics::to_string(sink) == sink.to_string());
        nlohmann::json sink_json = sink;
        NX_CHECK(sink_json.is_object());
        nlohmann::json config_json = config;
        metrics::JsonFileMetricSinkConfig config_back{};
        metrics::from_json(config_json, config_back);
        NX_CHECK(config_back.path == config.path && config_back.append == config.append);
        NX_CHECK(metrics::to_string(config) == config.to_string());
        NX_CHECK(std::format("{}", config) == config.to_string());
    }
}

void test_sinks_json_open_failure() {
    // Open failure surfaces through Status without emitting invalid JSON.
    {
        metrics::JsonFileMetricSinkConfig broken_config{"/nonexistent-nexigen-dir/telemetry.jsonl",
                                                        "", false};
        metrics::JsonFileMetricSink broken{broken_config};
        NX_CHECK(!broken.is_open());
        metrics::MetricBatchSnapshot empty{};
        const Status failed = broken.on_batch(empty);
        NX_CHECK(!failed.has_value());
        NX_CHECK(failed.error() == ErrorCode::SystemError);
    }
}

void test_sinks_json_composite() {
    // Composite diagnostics surface (task 11.3).
    {
        metrics::CompositeMetricSink<metrics::NullMetricSink, metrics::ConsoleMetricSink> composite{
            metrics::NullMetricSink{}, metrics::ConsoleMetricSink{-1}};
        NX_CHECK(composite.to_string().find("members=2") != std::string::npos);
        NX_CHECK(metrics::to_string(composite) == composite.to_string());
        NX_CHECK(std::format("{}", composite) == composite.to_string());
        nlohmann::json j = composite;
        NX_CHECK(j["members"] == 2U);
    }
}

void test_sinks_json_file(const char* path) {
    auto read_lines = [](const char* file) {
        std::vector<std::string> lines;
        std::ifstream input(file);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
        return lines;
    };

    {
        metrics::JsonFileMetricSinkConfig config{std::string(path), "", false};
        metrics::JsonFileMetricSink sink{config};
        test_sinks_json_opened(sink);
        metrics::MetricRecord records[1]{};
        std::snprintf(records[0].identity.meter_name, sizeof(records[0].identity.meter_name),
                      "quote\"slash\\ctrl\x01");
        records[0].kind = metrics::MetricKind::Counter;
        records[0].scalar.counter = 5;
        metrics::MetricBatchSnapshot batch{};
        batch.records = records;
        batch.record_count = 1;
        batch.record_capacity = 1;
        test_sinks_json_write(path, sink, batch, read_lines);
        test_sinks_json_append(path, batch, read_lines);
        test_sinks_json_diagnostics(sink, config);
    }
}

// --- JSON-lines file sink (spec metrics/metric-sinks; task 11.2) plus the
// composite diagnostics surface (task 11.3).
void test_sinks_json() {
    char path[] = "/tmp/nexigen_metrics_sink_XXXXXX";
    test_sinks_json_tempfile(path);
    test_sinks_json_file(path);
    test_sinks_json_open_failure();
    test_sinks_json_composite();
    ::unlink(path);
}

} // namespace

void suite_sinks_null_console() {
    run_suite("sinks_null_console", test_sinks_null_console);
}

void suite_sinks_json() {
    run_suite("sinks_json", test_sinks_json);
}
