// Split from tracing_test.cpp: nexigen::tracing record and JSON writer formats.

#include "tracing_test_support.hpp"

namespace {

// --- extended trace writer/sink coverage (change improve-test-coverage):
// every escaped byte class through both JSON writers plus empty chrome trace.
void test_trace_writer_bounds_full() {
    char small[3]{};
    tracing::detail::FixedWriter writer{small, sizeof small};
    NX_CHECK(writer.append_char('a'));
    NX_CHECK(writer.append_sv("bc"));
    NX_CHECK(!writer.append_char('d'));
    NX_CHECK(!writer.ok());
    NX_CHECK(!writer.append_sv("x"));
    NX_CHECK(!writer.append_u64(1));
    NX_CHECK(!writer.append_i64(-1));
    NX_CHECK(!writer.append_double(1.0));
    NX_CHECK(!writer.append_hex16(1));
    NX_CHECK(!writer.append_escaped("x"));

    char buffer[256]{};
    tracing::detail::FixedWriter all{buffer, sizeof buffer};
    NX_CHECK(all.append_u64(std::numeric_limits<std::uint64_t>::max()));
    NX_CHECK(all.append_i64(std::numeric_limits<std::int64_t>::min()));
    NX_CHECK(all.append_double(-0.0));
    NX_CHECK(all.append_hex16(0x0123456789abcdefULL));
    NX_CHECK(all.append_escaped("q\"\\\b\f\n\r\t\x02 and UTF-8"));
    NX_CHECK(all.ok());

    char tiny[4]{};
    tracing::detail::FixedWriter escaped{tiny, sizeof tiny};
    NX_CHECK(!escaped.append_escaped("\x01"));
    NX_CHECK(!escaped.ok());
}

void test_trace_writer_escape_matrix() {
    const std::string json_path = std::string(sink_tmp_dir) + "/escape-matrix.jsonl";
    const std::string chrome_path = std::string(sink_tmp_dir) + "/escape-matrix.json";
    static_cast<void>(::unlink(json_path.c_str()));
    static_cast<void>(::unlink(chrome_path.c_str()));

    const std::string escaped = "a\b\f\r\t\x01\x1f\"\\Кириллица";
    tracing::SpanRecord record = crafted_record(1'000'000'000ULL, escaped);
    static_cast<void>(record.attributes[3].store("ctl", std::string_view(escaped)));

    {
        tracing::JsonFileTraceSinkConfig config{};
        config.path = json_path;
        tracing::JsonFileTraceSink sink{config};
        NX_CHECK(sink.start().has_value());
        NX_CHECK(sink.on_span(record).has_value());
        NX_CHECK(sink.flush().has_value());
        sink.stop();
        const std::string content = read_file(json_path.c_str());
        const nlohmann::json parsed = nlohmann::json::parse(content, nullptr, false);
        NX_CHECK(!parsed.is_discarded());
        NX_CHECK(parsed.at("name").get<std::string>() == escaped);
    }

    {
        tracing::ChromeTraceSinkConfig config{};
        config.path = chrome_path;
        tracing::ChromeTraceSink sink{config};
        NX_CHECK(sink.start().has_value());
        NX_CHECK(sink.on_span(record).has_value());
        NX_CHECK(sink.flush().has_value());
        sink.stop();
        const std::string content = read_file(chrome_path.c_str());
        const nlohmann::json parsed = nlohmann::json::parse(content, nullptr, false);
        NX_CHECK(!parsed.is_discarded());
        NX_CHECK(parsed["traceEvents"].size() == 1U);
    }

    // Empty chrome trace: header/footer with no events.
    {
        const std::string empty_path = std::string(sink_tmp_dir) + "/empty.json";
        static_cast<void>(::unlink(empty_path.c_str()));
        tracing::ChromeTraceSinkConfig config{};
        config.path = empty_path;
        tracing::ChromeTraceSink sink{config};
        NX_CHECK(sink.start().has_value());
        NX_CHECK(sink.flush().has_value());
        sink.stop();
        const std::string content = read_file(empty_path.c_str());
        const nlohmann::json parsed = nlohmann::json::parse(content, nullptr, false);
        NX_CHECK(!parsed.is_discarded());
        NX_CHECK(parsed["traceEvents"].empty());
    }
}

// --- record serialization matrix (change improve-test-coverage):
// ids/context/attributes/record/config/dropped_stats round-trips.
void test_record_serialization_full() {
    {
        const tracing::TraceId trace{0x0123456789abcdefULL};
        const tracing::SpanId span{0xfedcba9876543210ULL};
        NX_CHECK(trace.to_string() == "0123456789abcdef");
        NX_CHECK(tracing::to_string(span) == "fedcba9876543210");
        nlohmann::json json;
        tracing::to_json(json, trace);
        tracing::TraceId trace_back{};
        tracing::from_json(json, trace_back);
        NX_CHECK(trace_back == trace);
        tracing::to_json(json, span);
        tracing::SpanId span_back{};
        tracing::from_json(json, span_back);
        NX_CHECK(span_back == span);
        tracing::from_json(nlohmann::json(7), trace_back);
        NX_CHECK(trace_back == trace);
    }
    {
        tracing::SpanContext context{};
        context.trace_id = tracing::TraceId{1};
        context.span_id = tracing::SpanId{2};
        context.sampled = tracing::SamplingDecision::RecordAndSample;
        nlohmann::json json;
        tracing::to_json(json, context);
        tracing::SpanContext back{};
        tracing::from_json(json, back);
        NX_CHECK(back.trace_id == context.trace_id);
        NX_CHECK(back.sampled == context.sampled);
        NX_CHECK(context.to_string().find("0000000000000001") != std::string::npos);
    }
    {
        tracing::SpanAttribute attribute{};
        NX_CHECK(attribute.store("i", std::int64_t{-3}));
        nlohmann::json json;
        tracing::to_json(json, attribute);
        tracing::SpanAttribute attr_back{};
        tracing::from_json(json, attr_back);
        NX_CHECK(attr_back.type == tracing::SpanAttributeType::Int64);
        NX_CHECK(attribute.to_string().find("key=i") != std::string::npos);

        NX_CHECK(attribute.store("u", std::uint64_t{7}));
        NX_CHECK(attribute.store("d", 1.5));
        NX_CHECK(attribute.store("b", true));
        NX_CHECK(attribute.store("s", std::string_view{"short-value"}));
        tracing::to_json(json, attribute);
        tracing::from_json(json, attr_back);
        NX_CHECK(attr_back.type == tracing::SpanAttributeType::String);
        NX_CHECK(attr_back.text_view() == "short-value");
        NX_CHECK(attribute.store("long", std::string_view{"long-enough-string-value"}));
        tracing::to_json(json, attribute);
        tracing::SpanAttribute long_back{};
        tracing::from_json(json, long_back);
        NX_CHECK(long_back.type != tracing::SpanAttributeType::String);
    }
    {
        tracing::SpanRecord record = crafted_record(1'000'000'000ULL, "serialize");
        nlohmann::json json;
        tracing::to_json(json, record);
        tracing::SpanRecord back{};
        tracing::from_json(json, back);
        NX_CHECK(back.name_view() == record.name_view());
        NX_CHECK(back.kind == record.kind);
        NX_CHECK(record.to_string().find("serialize") != std::string::npos);
        tracing::from_json(nlohmann::json(42), back);
        NX_CHECK(back.name_view() == record.name_view());
    }
    {
        tracing::TracerProviderConfig config{};
        config.worker_thread.set_cpus(core::CpuSet::from_core_mask(3));
        config.max_tracers = 8;
        config.max_spans_per_window = 10;
        config.window_ns = 11;
        nlohmann::json json;
        tracing::to_json(json, config);
        tracing::TracerProviderConfig back{};
        tracing::from_json(json, back);
        NX_CHECK(back.max_tracers == 8U);
        NX_CHECK(config.to_string().find("max_tracers") != std::string::npos);
        tracing::DroppedStats dropped{1, 2, 3};
        tracing::to_json(json, dropped);
        tracing::DroppedStats dropped_back{};
        tracing::from_json(json, dropped_back);
        NX_CHECK(dropped_back.total() == 6U);
        NX_CHECK(dropped.to_string().find("total=6") != std::string::npos);
    }
    // Wrong-typed readers leave the whole value untouched.
    {
        tracing::SpanId span{7};
        tracing::from_json(nlohmann::json(1), span);
        NX_CHECK(span.value() == 7U);
        tracing::SpanContext context_keep{};
        context_keep.trace_id = tracing::TraceId{5};
        tracing::from_json(nlohmann::json(1), context_keep);
        NX_CHECK(context_keep.trace_id.value() == 5U);
        tracing::SpanAttribute attr_keep{};
        NX_CHECK(attr_keep.store("k", std::int64_t{1}));
        tracing::from_json(nlohmann::json(1), attr_keep);
        NX_CHECK(attr_keep.key_view() == "k");
        tracing::from_json(nlohmann::json{{"key", "k"}}, attr_keep);
        NX_CHECK(attr_keep.type == tracing::SpanAttributeType::Int64);
        tracing::from_json(
            nlohmann::json{{"key", std::string(16, 'x')}, {"type", "Int64"}, {"value", 1}},
            attr_keep);
        NX_CHECK(attr_keep.key_view() == "k");
        tracing::from_json(nlohmann::json{{"key", "k"}, {"type", "Int64"}, {"value", "wrong"}},
                           attr_keep);
        NX_CHECK(attr_keep.value.i64 == 1);
        tracing::from_json(nlohmann::json{{"key", "k"}, {"type", "Uint64"}, {"value", "wrong"}},
                           attr_keep);
        tracing::from_json(nlohmann::json{{"key", "k"}, {"type", "Double"}, {"value", "wrong"}},
                           attr_keep);
        tracing::from_json(nlohmann::json{{"key", "k"}, {"type", "Bool"}, {"value", "wrong"}},
                           attr_keep);
        tracing::from_json(nlohmann::json{{"key", "k"}, {"type", "String"}, {"value", 1}},
                           attr_keep);
        tracing::from_json(nlohmann::json{{"key", "k"}, {"type", "Bogus"}, {"value", 1}},
                           attr_keep);
        tracing::from_json(nlohmann::json{{"key", "k"}, {"type", "Int64"}}, attr_keep);
        tracing::SpanRecord record_keep = crafted_record(1'000'000'000ULL, "keep");
        tracing::from_json(nlohmann::json{{"kind", 7}}, record_keep);
        NX_CHECK(record_keep.name_view() == "keep");
        tracing::TracerProviderConfig config_keep{};
        config_keep.max_tracers = 3;
        tracing::from_json(nlohmann::json{{"max_tracers", "x"}}, config_keep);
        NX_CHECK(config_keep.max_tracers == 3U);
        tracing::DroppedStats dropped_keep{1, 2, 3};
        tracing::from_json(nlohmann::json{{"rate_limited", "x"}}, dropped_keep);
        NX_CHECK(dropped_keep.total() == 6U);
    }
}

} // namespace

void suite_writer_bounds_full() {
    run_suite("writer_bounds_full", test_trace_writer_bounds_full);
}

void suite_writer_escape_matrix() {
    run_suite("writer_escape_matrix", test_trace_writer_escape_matrix);
}

void suite_serialization_full() {
    run_suite("serialization_full", test_record_serialization_full);
}
