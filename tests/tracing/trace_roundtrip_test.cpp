// End-to-end trace propagation (change add-trace-propagation; gated ON):
// caller fills the outgoing TraceHeader from the thread's TraceContext via
// make_trace_header(), publishes it as a custom com header, and the
// subscriber recovers the same trace id and source span id.
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/tracing/detail/trace_context.hpp>
#include <nexigen/tracing/null_trace_sink.hpp>
#include <nexigen/tracing/trace_header.hpp>
#include <nexigen/tracing/tracer_provider.hpp>

namespace com = nexigen::com;
namespace tracing = nexigen::tracing;

namespace {

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

struct Payload {
    std::int32_t frame;
};

void test_propagation_round_trip() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/TraceRoundTrip");
    NX_CHECK(name.has_value());
    auto publisher = com::Publisher<Payload, tracing::TraceHeader>::create(*node, *name);
    NX_CHECK(publisher.has_value());
    auto subscriber = com::Subscriber<Payload, tracing::TraceHeader>::create(*node, *name);
    NX_CHECK(subscriber.has_value());

    using Provider = tracing::TracerProvider<64, 1>;
    alignas(Provider) static std::byte storage[sizeof(Provider)];
    tracing::NullTraceSink null_sink;
    auto* provider =
        new (static_cast<void*>(storage)) Provider(tracing::TracerProviderConfig{}, null_sink);
    NX_CHECK(provider->initialize().has_value());
    const auto tracer = provider->get_tracer("camera");
    NX_CHECK(tracer.has_value());

    auto& context = tracing::current_trace_context();
    context.trace_id = tracing::TraceId{};
    context.source_span_id = tracing::SpanId{};
    NEXIGEN_TRACE_NEW_ROOT("CameraCapture");
    const tracing::TraceId trace = context.trace_id;

    tracing::SpanId source_span;
    {
        auto span = (*tracer)->start_span("capture", tracing::SpanKind::Internal);
        source_span = span.context().span_id;

        auto loaned = publisher->loan_uninit();
        NX_CHECK(loaned.has_value());
        auto sample = loaned->write_payload(Payload{7});
        sample.user_header_mut() = tracing::make_trace_header(11);
        NX_CHECK(publisher->send(std::move(sample)).has_value());
    }

    bool received = false;
    for (int i = 0; i < 500 && !received; ++i) {
        const auto got = subscriber->receive();
        NX_CHECK(got.has_value());
        if (got->has_value()) {
            NX_CHECK((*got)->payload().frame == 7);
            const tracing::TraceHeader& header = (*got)->user_header();
            NX_CHECK(header.trace_id == trace);
            NX_CHECK(header.source_span_id == source_span);
            NX_CHECK(header.sequence_number == 11U);
            received = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    NX_CHECK(received);

    provider->~Provider();
}

} // namespace

int main() {
    test_propagation_round_trip();
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
