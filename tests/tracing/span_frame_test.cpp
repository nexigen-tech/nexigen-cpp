// Split from tracing_test.cpp: the cross-process wire frame and its sink
// configuration (change add-iceoryx2-trace-sink; spec
// tracing/iceoryx2-trace-sink). No iceoryx2 dependency: these types are part
// of the core tracing library and must build with NEXIGEN_ENABLE_COM=OFF.

#include "tracing_test_support.hpp"

#include <nexigen/tracing/detail/fnv1a_64.hpp>
#include <nexigen/tracing/iceoryx2_trace_sink_config.hpp>
#include <nexigen/tracing/span_frame.hpp>

namespace {

// Published FNV-1a 64 check vectors: the hash is part of the cross-process
// wire contract, so a change here is a wire-format change.
static_assert(tracing::detail::fnv1a_64("") == 0xcbf29ce484222325ULL);
static_assert(tracing::detail::fnv1a_64("a") == 0xaf63dc4c8601ec8cULL);
static_assert(tracing::detail::fnv1a_64("foobar") == 0x85944171f73967e8ULL);
static_assert(tracing::detail::fnv1a_64(static_cast<const char*>(nullptr)) == 0ULL);

void test_fnv1a() {
    using tracing::detail::fnv1a_64;

    // Independent evaluations of identical text agree (per-process stability).
    const std::string_view path = "/opt/nexigen/src/camera/isp.cpp";
    NX_CHECK(fnv1a_64(path) == fnv1a_64(path));
    NX_CHECK(fnv1a_64(std::string_view{"CameraCapture"}) ==
             fnv1a_64(static_cast<const char*>("CameraCapture")));

    // Different text produces different values (not a proof, a tripwire).
    NX_CHECK(fnv1a_64(std::string_view{"isp.cpp"}) != fnv1a_64(std::string_view{"yolo.cpp"}));

    // NUL bytes are hashed like any other byte, and the terminator is not.
    const char with_nul[] = {'a', '\0', 'b'};
    NX_CHECK(fnv1a_64(std::string_view{with_nul, 3}) != fnv1a_64(std::string_view{"ab"}));
}

void test_span_frame_layout() {
    using tracing::SpanFrame;

    static_assert(sizeof(SpanFrame) == 384);
    static_assert(alignof(SpanFrame) == 64);
    static_assert(std::is_trivially_copyable_v<SpanFrame>);
    static_assert(offsetof(SpanFrame, trace_id) == 0);
    static_assert(offsetof(SpanFrame, span_id) == 8);
    static_assert(offsetof(SpanFrame, parent_span_id) == 16);
    static_assert(offsetof(SpanFrame, kind) == 24);
    static_assert(offsetof(SpanFrame, status) == 25);
    static_assert(offsetof(SpanFrame, flags) == 26);
    static_assert(offsetof(SpanFrame, attribute_count) == 27);
    static_assert(offsetof(SpanFrame, thread_id) == 28);
    static_assert(offsetof(SpanFrame, start_ns) == 32);
    static_assert(offsetof(SpanFrame, end_ns) == 40);
    static_assert(offsetof(SpanFrame, file_hash) == 48);
    static_assert(offsetof(SpanFrame, function_hash) == 56);
    static_assert(offsetof(SpanFrame, line) == 64);
    static_assert(offsetof(SpanFrame, column) == 68);
    static_assert(offsetof(SpanFrame, name) == 72);
    static_assert(offsetof(SpanFrame, attributes) == 120);
    static_assert(offsetof(SpanFrame, app_id) == 312);
    static_assert(offsetof(SpanFrame, pid) == 316);
    static_assert(offsetof(SpanFrame, sequence) == 320);
    static_assert(offsetof(SpanFrame, publish_ts_ns) == 328);
    static_assert(offsetof(SpanFrame, version) == 336);
    static_assert(offsetof(SpanFrame, reserved) == 338);

    SpanFrame frame{};
    NX_CHECK(frame.version == SpanFrame::format_version);
    NX_CHECK(frame.app_id_text().empty());
    NX_CHECK(frame.name_view().empty());
    NX_CHECK(frame.duration_ns() == 0U);

    frame.start_ns = 1000U;
    frame.end_ns = 1400U;
    NX_CHECK(frame.duration_ns() == 400U);

    // A name longer than the bound is truncated by the producer's bounded
    // copy, never overflowed.
    for (std::size_t i = 0; i < SpanFrame::name_capacity; ++i) {
        frame.name[i] = 'n';
    }
    frame.name[SpanFrame::name_capacity] = '\0';
    NX_CHECK(frame.name_view().size() == SpanFrame::name_capacity);

    // app_id is a NUL-trimmed 4-byte view.
    frame.app_id[0] = 'L';
    frame.app_id[1] = 'I';
    frame.app_id[2] = 'D';
    frame.app_id[3] = 'R';
    NX_CHECK(frame.app_id_text() == "LIDR");
}

void test_span_frame_serialization() {
    using tracing::SpanFrame;

    SpanFrame frame{};
    frame.trace_id = tracing::TraceId{0x0123456789ABCDEFULL};
    frame.span_id = tracing::SpanId{0xFEDCBA9876543210ULL};
    frame.parent_span_id = tracing::SpanId{0x11ULL};
    frame.kind = tracing::SpanKind::Client;
    frame.status = tracing::SpanStatus::Ok;
    frame.flags = tracing::SpanRecord::flag_ended_explicit;
    frame.attribute_count = 2U;
    frame.thread_id = 77U;
    frame.start_ns = 10U;
    frame.end_ns = 25U;
    frame.file_hash = 0x1122334455667788ULL;
    frame.function_hash = 0x8877665544332211ULL;
    frame.line = 42U;
    frame.column = 7U;
    std::snprintf(frame.name, sizeof(frame.name), "Inference");
    NX_CHECK(frame.attributes[0].store("frame", 9));
    NX_CHECK(frame.attributes[1].store("camera", std::string_view{"front"}));
    frame.app_id[0] = 'L';
    frame.app_id[1] = 'I';
    frame.app_id[2] = 'D';
    frame.app_id[3] = 'R';
    frame.pid = 4242U;
    frame.sequence = 7U;
    frame.publish_ts_ns = 12345U;
    frame.version = SpanFrame::format_version;

    const std::string text = frame.to_string();
    NX_CHECK(text.find("Inference") != std::string::npos);
    NX_CHECK(text.find("LIDR") != std::string::npos);
    NX_CHECK(text.find("seq=7") != std::string::npos);
    NX_CHECK(std::format("{}", frame) == text);
    NX_CHECK(tracing::to_string(frame) == text);

    nlohmann::json json{};
    tracing::to_json(json, frame);
    NX_CHECK(json["name"] == "Inference");
    NX_CHECK(json["app_id"] == "LIDR");
    NX_CHECK(json["pid"] == 4242U);
    NX_CHECK(json["sequence"] == 7U);
    NX_CHECK(json["file_hash"] == 0x1122334455667788ULL);
    NX_CHECK(json["attribute_count"] == 2U);
    NX_CHECK(json["attributes"].is_array());
    NX_CHECK(json["attributes"].size() == 2U);

    SpanFrame parsed{};
    tracing::from_json(json, parsed);
    NX_CHECK(parsed.trace_id == frame.trace_id);
    NX_CHECK(parsed.span_id == frame.span_id);
    NX_CHECK(parsed.parent_span_id == frame.parent_span_id);
    NX_CHECK(parsed.kind == frame.kind);
    NX_CHECK(parsed.status == frame.status);
    NX_CHECK(parsed.flags == frame.flags);
    NX_CHECK(parsed.attribute_count == frame.attribute_count);
    NX_CHECK(parsed.thread_id == frame.thread_id);
    NX_CHECK(parsed.start_ns == frame.start_ns);
    NX_CHECK(parsed.end_ns == frame.end_ns);
    NX_CHECK(parsed.file_hash == frame.file_hash);
    NX_CHECK(parsed.function_hash == frame.function_hash);
    NX_CHECK(parsed.line == frame.line);
    NX_CHECK(parsed.column == frame.column);
    NX_CHECK(parsed.name_view() == frame.name_view());
    NX_CHECK(parsed.app_id_text() == frame.app_id_text());
    NX_CHECK(parsed.pid == frame.pid);
    NX_CHECK(parsed.sequence == frame.sequence);
    NX_CHECK(parsed.publish_ts_ns == frame.publish_ts_ns);
    NX_CHECK(parsed.version == frame.version);
    NX_CHECK(parsed.attributes[0].type == tracing::SpanAttributeType::Int64);
    NX_CHECK(parsed.attributes[0].value.i64 == 9);
    NX_CHECK(parsed.attributes[1].text_view() == "front");

    // A shorter application code must not inherit stale tail bytes from the
    // frame being overwritten.
    SpanFrame reused{};
    reused.app_id[0] = 'Z';
    reused.app_id[1] = 'Z';
    reused.app_id[2] = 'Z';
    reused.app_id[3] = 'Z';
    nlohmann::json short_code{};
    tracing::to_json(short_code, frame);
    short_code["app_id"] = "AB";
    tracing::from_json(short_code, reused);
    NX_CHECK(reused.app_id_text() == "AB");

    // Hostile input leaves the parsed frame usable (no throw under
    // JSON_NOEXCEPTION=1).
    SpanFrame hostile{};
    tracing::from_json(nlohmann::json{{"name", 5}, {"attributes", "nope"}, {"pid", -1}}, hostile);
    NX_CHECK(hostile.name_view().empty());
    NX_CHECK(hostile.attribute_count == 0U);
}

void test_trace_sink_config() {
    using tracing::Iceoryx2TraceSinkConfig;

    Iceoryx2TraceSinkConfig config{};
    NX_CHECK(config.validate().has_value());
    NX_CHECK(!config.service_name.empty());
    NX_CHECK(!config.app_id.empty());

    Iceoryx2TraceSinkConfig invalid = config;
    invalid.service_name.clear();
    NX_CHECK(!invalid.validate().has_value());

    invalid = config;
    invalid.app_id.clear();
    NX_CHECK(!invalid.validate().has_value());

    invalid = config;
    invalid.app_id = "TOOLONG";
    NX_CHECK(!invalid.validate().has_value());

    invalid = config;
    invalid.service_name = std::string(512U, 's');
    NX_CHECK(!invalid.validate().has_value());

    config.service_name = "Nexigen/Test/TraceSinkConfig";
    config.app_id = "LIDR";
    config.subscriber_max_buffer_depth = 4096U;
    const std::string text = config.to_string();
    NX_CHECK(text.find("Nexigen/Test/TraceSinkConfig") != std::string::npos);
    NX_CHECK(text.find("LIDR") != std::string::npos);
    NX_CHECK(text.find("4096") != std::string::npos);
    NX_CHECK(std::format("{}", config) == text);
    NX_CHECK(tracing::to_string(config) == text);

    nlohmann::json json{};
    tracing::to_json(json, config);
    Iceoryx2TraceSinkConfig parsed{};
    tracing::from_json(json, parsed);
    NX_CHECK(parsed.service_name == config.service_name);
    NX_CHECK(parsed.app_id == config.app_id);
    NX_CHECK(parsed.subscriber_max_buffer_depth == config.subscriber_max_buffer_depth);

    Iceoryx2TraceSinkConfig untouched{};
    const Iceoryx2TraceSinkConfig before = untouched;
    tracing::from_json(nlohmann::json{"not-an-object"}, untouched);
    NX_CHECK(untouched.service_name == before.service_name);
    NX_CHECK(untouched.app_id == before.app_id);
}

} // namespace

void suite_span_frame() {
    run_suite("fnv1a_64", test_fnv1a);
    run_suite("span_frame", test_span_frame_layout);
    run_suite("span_frame_format", test_span_frame_serialization);
    run_suite("trace_sink_config", test_trace_sink_config);
}
