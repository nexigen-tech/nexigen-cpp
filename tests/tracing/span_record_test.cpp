// Split from tracing_test.cpp: nexigen::tracing span record.

#include "tracing_test_support.hpp"

namespace {

void test_span_record() {
    using tracing::SpanRecord;
    using tracing::SpanAttributeType;

    // Compile-time pins mirrored in the test TU (public layout = wire shape).
    static_assert(sizeof(SpanRecord) == 320);
    static_assert(alignof(SpanRecord) == 64);
    static_assert(std::is_trivially_copyable_v<SpanRecord>);
    static_assert(offsetof(SpanRecord, trace_id) == 0);
    static_assert(offsetof(SpanRecord, span_id) == 8);
    static_assert(offsetof(SpanRecord, parent_span_id) == 16);
    static_assert(offsetof(SpanRecord, kind) == 24);
    static_assert(offsetof(SpanRecord, status) == 25);
    static_assert(offsetof(SpanRecord, flags) == 26);
    static_assert(offsetof(SpanRecord, attribute_count) == 27);
    static_assert(offsetof(SpanRecord, thread_id) == 28);
    static_assert(offsetof(SpanRecord, start) == 32);
    static_assert(offsetof(SpanRecord, end) == 40);
    static_assert(offsetof(SpanRecord, file_name) == 48);
    static_assert(offsetof(SpanRecord, function_name) == 56);
    static_assert(offsetof(SpanRecord, line) == 64);
    static_assert(offsetof(SpanRecord, column) == 68);
    static_assert(offsetof(SpanRecord, name) == 72);
    static_assert(offsetof(SpanRecord, attributes) == 120);

    // A fully populated record.
    SpanRecord record{};
    record.trace_id = tracing::TraceId{0x0123456789ABCDEFULL};
    record.span_id = tracing::SpanId{0xFEDCBA9876543210ULL};
    record.parent_span_id = tracing::SpanId{0x1ULL};
    record.kind = tracing::SpanKind::Server;
    record.status = tracing::SpanStatus::Error;
    record.flags = SpanRecord::flag_ended_explicit;
    record.thread_id = 4242;
    record.start = chrono::Timestamp{1'000};
    record.end = chrono::Timestamp{2'500};
    record.file_name = "engine.cpp";
    record.function_name = "void run_engine()";
    record.line = 77;
    record.column = 13;
    const std::string_view name_text = "simulation-step";
    for (std::size_t i = 0; i < name_text.size(); ++i) {
        record.name[i] = name_text[i];
    }
    record.name[name_text.size()] = '\0';
    static_cast<void>(record.attributes[0].store("retries", std::int64_t{3}));
    static_cast<void>(record.attributes[1].store("ratio", 0.5));
    static_cast<void>(record.attributes[2].store("ok", true));
    static_cast<void>(record.attributes[3].store("tag", std::string_view("hil")));
    record.attribute_count = 4;

    NX_CHECK(record.duration_ns() == 1'500);
    NX_CHECK(record.name_view() == "simulation-step");

    // Lossless byte-for-byte copy: every field equal.
    {
        SpanRecord copy{};
        std::memcpy(&copy, &record, sizeof(SpanRecord));
        NX_CHECK(copy.trace_id == record.trace_id);
        NX_CHECK(copy.span_id == record.span_id);
        NX_CHECK(copy.parent_span_id == record.parent_span_id);
        NX_CHECK(copy.kind == record.kind);
        NX_CHECK(copy.status == record.status);
        NX_CHECK(copy.flags == record.flags);
        NX_CHECK(copy.attribute_count == record.attribute_count);
        NX_CHECK(copy.thread_id == record.thread_id);
        NX_CHECK(copy.start == record.start);
        NX_CHECK(copy.end == record.end);
        NX_CHECK(std::string_view(copy.file_name) == "engine.cpp");
        NX_CHECK(std::string_view(copy.function_name) == "void run_engine()");
        NX_CHECK(copy.line == record.line && copy.column == record.column);
        NX_CHECK(copy.name_view() == record.name_view());
        for (std::size_t i = 0; i < tracing::MaxAttributes; ++i) {
            NX_CHECK(copy.attributes[i].type == record.attributes[i].type);
            NX_CHECK(copy.attributes[i].key_view() == record.attributes[i].key_view());
        }
    }

    // Diagnostics: member/free agreement, std::format equality, content.
    {
        const std::string member = record.to_string();
        NX_CHECK(member == tracing::to_string(record));
        NX_CHECK(std::format("{}", record) == member);
        NX_CHECK(member.find("0123456789abcdef") != std::string::npos);
        NX_CHECK(member.find("fedcba9876543210") != std::string::npos);
        NX_CHECK(member.find("Server") != std::string::npos);
        NX_CHECK(member.find("Error") != std::string::npos);
        NX_CHECK(member.find("simulation-step") != std::string::npos);
    }

    // JSON round-trip: ids as 16-char hex, kind/status canonical, timestamps
    // and location and all four typed attributes survive.
    {
        const nlohmann::json j = record;
        NX_CHECK(j.at("trace_id").get<std::string>() == "0123456789abcdef");
        NX_CHECK(j.at("span_id").get<std::string>() == "fedcba9876543210");
        NX_CHECK(j.at("parent_span_id").get<std::string>() == "0000000000000001");
        NX_CHECK(j.at("kind").get<std::string>() == "Server");
        NX_CHECK(j.at("status").get<std::string>() == "Error");
        NX_CHECK(j.at("thread_id").get<std::uint32_t>() == 4242);
        NX_CHECK(j.at("start_ns").get<std::uint64_t>() == 1'000);
        NX_CHECK(j.at("end_ns").get<std::uint64_t>() == 2'500);
        NX_CHECK(j.at("name").get<std::string>() == "simulation-step");
        NX_CHECK(j.at("file").get<std::string>() == "engine.cpp");
        NX_CHECK(j.at("line").get<std::uint32_t>() == 77);

        SpanRecord back{};
        nlohmann::from_json(j, back);
        NX_CHECK(back.trace_id == record.trace_id);
        NX_CHECK(back.span_id == record.span_id);
        NX_CHECK(back.parent_span_id == record.parent_span_id);
        NX_CHECK(back.kind == record.kind);
        NX_CHECK(back.status == record.status);
        NX_CHECK(back.flags == record.flags);
        NX_CHECK(back.thread_id == record.thread_id);
        NX_CHECK(back.start == record.start && back.end == record.end);
        NX_CHECK(back.name_view() == record.name_view());
        NX_CHECK(back.attribute_count == 4);
        NX_CHECK(back.attributes[0].type == SpanAttributeType::Int64 &&
                 back.attributes[0].value.i64 == 3);
        NX_CHECK(back.attributes[1].type == SpanAttributeType::Double &&
                 back.attributes[1].value.f64 == 0.5);
        NX_CHECK(back.attributes[2].type == SpanAttributeType::Bool && back.attributes[2].value.b);
        NX_CHECK(back.attributes[3].type == SpanAttributeType::String &&
                 back.attributes[3].text_view() == "hil");
        NX_CHECK(back.line == 77 && back.column == 13);
        // Location text restores through the intern table (stable storage).
        NX_CHECK(std::string_view(back.file_name) == "engine.cpp");
        NX_CHECK(std::string_view(back.function_name) == "void run_engine()");
    }

    // Hostile JSON: scalars/arrays/null/wrong-typed fields on the populated
    // record — returns normally, record unchanged.
    {
        const SpanRecord before = record;
        const std::array<nlohmann::json, 5> hostile{
            nlohmann::json(5), nlohmann::json{1, 2}, nlohmann::json(nullptr),
            nlohmann::json{{"trace_id", 12}, {"kind", 7}, {"name", false}, {"start_ns", "text"}},
            nlohmann::json{{"name", nullptr}}};
        for (const nlohmann::json& bad : hostile) {
            nlohmann::from_json(bad, record);
            NX_CHECK(std::memcmp(&record, &before, sizeof(SpanRecord)) == 0);
        }
        // A valid attributes array with only unparseable elements resets the
        // count but leaves the rest of the record untouched.
        SpanRecord attr_record = before;
        nlohmann::from_json(
            nlohmann::json{
                {"attributes", nlohmann::json::array({{"key", "x"}, {"type", "Unknown"}})}},
            attr_record);
        NX_CHECK(attr_record.attribute_count == 0U);
        NX_CHECK(attr_record.trace_id == before.trace_id);
    }
}

// --- parent_stack suite (task 4.1 / design D7; spec tracing/span-context
// "Automatic intra-thread parent tracking"): nested/sibling frame chaining,
// explicit-parent override semantics, roots on empty stacks, over-deep
// bounded roots, end-with-active-children truncation — plus the affinity
// pin/read round-trip with error paths on a probe thread. Provider-level
// automatic chains land in the scoped_span suite (task 6.2).

} // namespace

void suite_span_record() {
    run_suite("span_record", test_span_record);
}
