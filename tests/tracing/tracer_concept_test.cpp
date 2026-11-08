// Split from tracing_test.cpp: nexigen::tracing concepts.

#include "tracing_test_support.hpp"

namespace {

struct FullTracerModel {
    struct Guard {
        bool set_attribute(std::string_view, int) noexcept { return false; }
        bool set_attribute(std::string_view, std::int64_t) noexcept { return false; }
        bool set_attribute(std::string_view, std::uint64_t) noexcept { return false; }
        bool set_attribute(std::string_view, double) noexcept { return false; }
        bool set_attribute(std::string_view, bool) noexcept { return false; }
        bool set_attribute(std::string_view, std::string_view) noexcept { return false; }
        bool set_status(tracing::SpanStatus) noexcept { return false; }
        bool end() noexcept { return false; }
        tracing::SpanContext context() const noexcept { return {}; }
        std::string to_string() const { return "guard"; }
    };
    Guard start_span(std::string_view, tracing::SpanKind, const tracing::SpanContext* = nullptr,
                     std::source_location = std::source_location::current()) noexcept {
        return {};
    }
    std::string to_string() const { return "FullTracerModel"; }
};
static_assert(tracing::concepts::Tracer<FullTracerModel>);

/// start_span omitted entirely.
struct NoStartSpanModel {
    std::string to_string() const { return "no-start"; }
};
static_assert(!tracing::concepts::Tracer<NoStartSpanModel>);

/// The returned guard has no end() — the guard concept fails, and with it
/// the tracer concept.
struct GuardWithoutEndModel {
    struct Guard {
        bool set_attribute(std::string_view, int) noexcept { return false; }
        bool set_attribute(std::string_view, std::int64_t) noexcept { return false; }
        bool set_attribute(std::string_view, std::uint64_t) noexcept { return false; }
        bool set_attribute(std::string_view, double) noexcept { return false; }
        bool set_attribute(std::string_view, bool) noexcept { return false; }
        bool set_attribute(std::string_view, std::string_view) noexcept { return false; }
        bool set_status(tracing::SpanStatus) noexcept { return false; }
        tracing::SpanContext context() const noexcept { return {}; }
        std::string to_string() const { return "guard"; }
    };
    Guard start_span(std::string_view, tracing::SpanKind, const tracing::SpanContext* = nullptr,
                     std::source_location = std::source_location::current()) noexcept {
        return {};
    }
    std::string to_string() const { return "guard-without-end"; }
};
static_assert(!tracing::concepts::Tracer<GuardWithoutEndModel>);

/// The tracer has no to_string() diagnostic.
struct TracerWithoutToStringModel {
    struct Guard {
        bool set_attribute(std::string_view, int) noexcept { return false; }
        bool set_attribute(std::string_view, std::int64_t) noexcept { return false; }
        bool set_attribute(std::string_view, std::uint64_t) noexcept { return false; }
        bool set_attribute(std::string_view, double) noexcept { return false; }
        bool set_attribute(std::string_view, bool) noexcept { return false; }
        bool set_attribute(std::string_view, std::string_view) noexcept { return false; }
        bool set_status(tracing::SpanStatus) noexcept { return false; }
        bool end() noexcept { return false; }
        tracing::SpanContext context() const noexcept { return {}; }
        std::string to_string() const { return "guard"; }
    };
    Guard start_span(std::string_view, tracing::SpanKind, const tracing::SpanContext* = nullptr,
                     std::source_location = std::source_location::current()) noexcept {
        return {};
    }
};
static_assert(!tracing::concepts::Tracer<TracerWithoutToStringModel>);

/// Complete conforming sink surface (the positive reference).
struct FullSinkModel {
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) noexcept { return {}; }
    core::Status flush() noexcept { return {}; }
    void stop() noexcept {}
    std::string to_string() const { return "FullSinkModel"; }
};
static_assert(tracing::concepts::TraceSink<FullSinkModel>);

/// flush omitted entirely.
struct SinkWithoutFlushModel {
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) noexcept { return {}; }
    void stop() noexcept {}
    std::string to_string() const { return "no-flush"; }
};
static_assert(!tracing::concepts::TraceSink<SinkWithoutFlushModel>);

/// on_span is NOT noexcept — a throwing delivery fails the contract.
struct ThrowingOnSpanModel {
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) { return {}; } // no noexcept
    core::Status flush() noexcept { return {}; }
    void stop() noexcept {}
    std::string to_string() const { return "throwing"; }
};
static_assert(!tracing::concepts::TraceSink<ThrowingOnSpanModel>);

/// stop omitted entirely.
struct SinkWithoutStopModel {
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) noexcept { return {}; }
    core::Status flush() noexcept { return {}; }
    std::string to_string() const { return "no-stop"; }
};
static_assert(!tracing::concepts::TraceSink<SinkWithoutStopModel>);

// Positive pins for the shipped null types (spec scenarios).
static_assert(tracing::concepts::Tracer<tracing::NullTracer>);
static_assert(tracing::concepts::TraceSink<tracing::NullTraceSink>);

// --- concepts suite (task 5.1 / spec tracing/tracer-concepts): runtime
// behavior of the null types — zero records, zero allocations, lifecycle
// cost indistinguishable from an empty baseline (Release), and the null
// serialization surface. Sink-type concept pins for the group-7 sinks land
// with the sinks suite.
void test_concepts() {
    using tracing::NullTracer;
    using tracing::NullTraceSink;

    // Null tracer: zero records, zero heap traffic across a large batch.
    {
        NullTracer null_tracer{};
        const std::size_t arena_before = arena_used();
        const std::size_t heap_before = heap_in_use();
        for (int i = 0; i < 50'000; ++i) {
            auto span = null_tracer.start_span("null", tracing::SpanKind::Server);
            static_cast<void>(span.set_attribute("k", 1));
            static_cast<void>(span.set_status(tracing::SpanStatus::Ok));
            static_cast<void>(span.context());
            static_cast<void>(span.end());
        }
        NX_CHECK(arena_used() == arena_before);
        NX_CHECK(heap_in_use() == heap_before);

        // The guard reports rejection for every mutation (a no-op surface).
        auto span = null_tracer.start_span("null", tracing::SpanKind::Client);
        NX_CHECK(!span.set_attribute("k", std::string_view("v")));
        NX_CHECK(!span.set_status(tracing::SpanStatus::Error));
        NX_CHECK(!span.end());
        NX_CHECK(span.context() == tracing::SpanContext{}); // zero ids, Drop
    }

    // Null sink: every delivery succeeds, nothing observable happens.
    {
        NullTraceSink sink{};
        tracing::SpanRecord record{};
        const std::size_t arena_before = arena_used();
        NX_CHECK(sink.start().has_value());
        for (int i = 0; i < 50'000; ++i) {
            NX_CHECK(sink.on_span(record).has_value());
        }
        NX_CHECK(sink.flush().has_value());
        sink.stop();
        NX_CHECK(arena_used() == arena_before);
    }

    // Null serialization surface: to_string/formatter agree, to_json writes.
    {
        NullTracer null_tracer{};
        NullTraceSink null_sink{};
        NX_CHECK(null_tracer.to_string() == "NullTracer()");
        NX_CHECK(tracing::to_string(null_tracer) == null_tracer.to_string());
        NX_CHECK(std::format("{}", null_tracer) == null_tracer.to_string());
        NX_CHECK(null_sink.to_string() == "NullTraceSink()");
        NX_CHECK(std::format("{}", null_sink) == null_sink.to_string());
        const nlohmann::json j_tracer = null_tracer;
        NX_CHECK(j_tracer.at("type").get<std::string>() == "NullTracer");
        const nlohmann::json j_sink = null_sink;
        NX_CHECK(j_sink.at("type").get<std::string>() == "NullTraceSink");
        // No from_json exists for either null type: compile-fail pins are
        // verified by the documented scratch-TU commands in the sinks suite.
    }

    // Null lifecycle cost vs an empty baseline loop (spec scenario "Null
    // tracer lifetime costs nothing"): identical machinery, the difference
    // must be noise. Gated to Release (no sanitizers); Debug prints only.
    {
        constexpr int n = 200'000;
        volatile int sink_value = 0; // keeps both loops equally "observed"
        const auto empty_loop = [&sink_value] {
            for (int i = 0; i < n; ++i) {
                sink_value = sink_value + 1;
            }
        };
        NullTracer null_tracer{};
        const auto null_loop = [&null_tracer, &sink_value] {
            for (int i = 0; i < n; ++i) {
                auto span = null_tracer.start_span("null", tracing::SpanKind::Internal);
                static_cast<void>(span);
                sink_value = sink_value + 1;
            }
        };
        empty_loop();
        null_loop();
#if defined(NEXIGEN_TRACING_STRICT_LATENCY) && !defined(NX_SANITIZER_BUILD)
        // Measure medians of batched per-op costs with the monotonic clock.
        std::array<double, 5> empty_ms{};
        std::array<double, 5> null_ms{};
        for (std::size_t pass = 0; pass < empty_ms.size(); ++pass) {
            const auto t0 = std::chrono::steady_clock::now();
            empty_loop();
            const auto t1 = std::chrono::steady_clock::now();
            null_loop();
            const auto t2 = std::chrono::steady_clock::now();
            empty_ms[pass] =
                std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(n);
            null_ms[pass] =
                std::chrono::duration<double, std::nano>(t2 - t1).count() / static_cast<double>(n);
        }
        std::sort(empty_ms.begin(), empty_ms.end());
        std::sort(null_ms.begin(), null_ms.end());
        const double baseline = empty_ms[empty_ms.size() / 2U];
        const double null_cost = null_ms[null_ms.size() / 2U];
        std::printf("null tracer: baseline %.3f ns/op, null %.3f ns/op\n", baseline, null_cost);
        // Indistinguishable from baseline: within 1 ns (noise budget).
        NX_CHECK(null_cost - baseline <= 1.0);
#else
        std::printf("null tracer: cost check compiled out (correctness-only build)\n");
#endif
    }
}

// --- sinks compile-time pins (task 7.x / spec tracing/tracer-concepts):
// every shipped sink satisfies the TraceSink contract.

} // namespace

void suite_concepts() {
    run_suite("concepts", test_concepts);
}
