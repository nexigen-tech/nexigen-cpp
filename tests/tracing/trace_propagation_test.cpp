// Trace propagation tests (change add-trace-propagation): TraceHeader layout,
// NEW_ROOT, and start_span inheriting the thread-local TraceContext
// (cross-process simulation).
#include "tracing_test_support.hpp"

#include <nexigen/tracing/detail/trace_context.hpp>
#include <nexigen/tracing/trace_header.hpp>

namespace {

using nexigen::tracing::SpanId;
using nexigen::tracing::SpanKind;
using nexigen::tracing::TraceContext;
using nexigen::tracing::TraceHeader;
using nexigen::tracing::TraceId;

void test_header_layout() {
    static_assert(sizeof(TraceHeader) == 32, "32-byte header");
    static_assert(alignof(TraceHeader) == 8, "8-byte alignment");
    static_assert(std::is_trivially_copyable_v<TraceHeader>, "trivially copyable");
    static_assert(nexigen::com::concepts::Message<TraceHeader>, "Message contract");

    TraceHeader header{};
    header.trace_id = TraceId{0xABCDEFULL};
    header.source_span_id = SpanId{0x1234ULL};
    header.publish_timestamp_ns = 42;
    header.sequence_number = 7;
    const std::string text = header.to_string();
    NX_CHECK(text.find("trace_id=") != std::string::npos);
    NX_CHECK(text.find("sequence_number=7") != std::string::npos);
    NX_CHECK(nexigen::tracing::to_string(header) == text);
    NX_CHECK(std::format("{}", header) == text);
}

void test_new_root() {
    auto& context = nexigen::tracing::current_trace_context();
    context.trace_id = TraceId{};
    context.source_span_id = SpanId{};

    NEXIGEN_TRACE_NEW_ROOT("CameraCapture");
    NX_CHECK(!context.trace_id.is_unset());
    NX_CHECK(context.source_span_id.is_unset());
    NX_CHECK(context.to_string().find("trace_id=") != std::string::npos);
    NX_CHECK(std::format("{}", context) == context.to_string());
}

void test_start_span_inherits_context() {
    using Provider = nexigen::tracing::TracerProvider<64, 1>;
    alignas(Provider) static std::byte storage[sizeof(Provider)];
    nexigen::tracing::NullTraceSink null_sink;
    auto* provider = new (static_cast<void*>(storage))
        Provider(nexigen::tracing::TracerProviderConfig{}, null_sink);
    NX_CHECK(provider->initialize().has_value());
    const auto tracer = provider->get_tracer("propagation");
    NX_CHECK(tracer.has_value());

    auto& context = nexigen::tracing::current_trace_context();
    context.trace_id = TraceId{};
    context.source_span_id = SpanId{};

    // (4.2) NEW_ROOT → root span inherits the fresh trace id, zero parent.
    NEXIGEN_TRACE_NEW_ROOT("CameraCapture");
    const TraceId root_trace = context.trace_id;
    {
        auto span = (*tracer)->start_span("frame", SpanKind::Internal);
        NX_CHECK(span.context().trace_id == root_trace);
        NX_CHECK(span.record().parent_span_id.is_unset());
    }

    // (4.3) Cross-process simulation: a received header sets the context; the
    // next root span inherits its trace id and treats its span id as parent.
    context.trace_id = TraceId{0x1234ULL};
    context.source_span_id = SpanId{0x5678ULL};
    {
        auto span = (*tracer)->start_span("process", SpanKind::Internal);
        NX_CHECK(span.context().trace_id == TraceId{0x1234ULL});
        NX_CHECK(span.record().parent_span_id == SpanId{0x5678ULL});
    }

    // Context is persistent (not consumed): the next root span keeps the same
    // trace after the first processing span ended.
    {
        auto span = (*tracer)->start_span("process_again", SpanKind::Internal);
        NX_CHECK(span.context().trace_id == TraceId{0x1234ULL});
    }

    provider->~Provider();
}

void test_outgoing_header_from_context() {
    // Design D6 caller side: make_trace_header fills the outgoing header from
    // the thread's TraceContext and the active span.
    using Provider = nexigen::tracing::TracerProvider<64, 1>;
    alignas(Provider) static std::byte storage[sizeof(Provider)];
    nexigen::tracing::NullTraceSink null_sink;
    auto* provider = new (static_cast<void*>(storage))
        Provider(nexigen::tracing::TracerProviderConfig{}, null_sink);
    NX_CHECK(provider->initialize().has_value());
    const auto tracer = provider->get_tracer("outgoing");
    NX_CHECK(tracer.has_value());

    auto& context = nexigen::tracing::current_trace_context();
    context.trace_id = TraceId{};
    context.source_span_id = SpanId{};

    NEXIGEN_TRACE_NEW_ROOT("CameraCapture");
    const TraceId trace = context.trace_id;

    // No active span: the header carries the context trace and a zero parent.
    {
        const TraceHeader header = nexigen::tracing::make_trace_header(3);
        NX_CHECK(header.trace_id == trace);
        NX_CHECK(header.source_span_id.is_unset());
        NX_CHECK(header.sequence_number == 3U);
    }

    // Inside a span: the header carries that span as the source.
    {
        auto span = (*tracer)->start_span("frame", SpanKind::Internal);
        const TraceHeader header = nexigen::tracing::make_trace_header(4);
        NX_CHECK(header.trace_id == trace);
        NX_CHECK(header.source_span_id == span.context().span_id);
        NX_CHECK(header.sequence_number == 4U);
    }

    provider->~Provider();
}

} // namespace

void suite_trace_propagation() {
    run_suite("trace_header", test_header_layout);
    run_suite("trace_new_root", test_new_root);
    run_suite("trace_context_propagation", test_start_span_inherits_context);
    run_suite("trace_outgoing_header", test_outgoing_header_from_context);
}
