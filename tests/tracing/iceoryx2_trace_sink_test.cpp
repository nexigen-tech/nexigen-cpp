// Tests nexigen::tracing::Iceoryx2TraceSink (change add-iceoryx2-trace-sink;
// spec tracing/iceoryx2-trace-sink). Build only when NEXIGEN_ENABLE_COM=ON.
//
// The suite shares the tracing allocation-arena harness: operator new is
// re-backed by a fixed arena, so `arena_used()` is an exact C++-heap counter
// (iceoryx2's Rust allocations go through the system allocator and are outside
// that counter; the structural no-allocation guarantee of `on_span` is what is
// asserted).
#include "tracing_test_support.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <thread>

#if defined(__x86_64__)
#include <x86intrin.h>
#endif

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/tracing/detail/fnv1a_64.hpp>
#include <nexigen/tracing/iceoryx2_trace_sink.hpp>
#include <nexigen/tracing/iceoryx2_trace_sink_config.hpp>
#include <nexigen/tracing/span_frame.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace com = nexigen::com;
namespace tracing = nexigen::tracing;

namespace {

static_assert(tracing::concepts::TraceSink<tracing::Iceoryx2TraceSink>);
static_assert(com::concepts::Message<tracing::SpanFrame>);

template <typename T>
concept HasFromJson = requires(const nlohmann::json& json, T& value) { from_json(json, value); };
static_assert(!HasFromJson<tracing::Iceoryx2TraceSink>, "live sink: deliberately no from_json");

// No per-span notification surface: the daemon drains by interval.
template <typename T>
concept HasNotifier = requires(T& sink) { sink.notifier(); };
template <typename T>
concept HasNotify = requires(T& sink) { sink.notify(); };
static_assert(!HasNotifier<tracing::Iceoryx2TraceSink>);
static_assert(!HasNotify<tracing::Iceoryx2TraceSink>);

tracing::SpanRecord make_record(std::uint64_t index, const char* file = "/src/camera/isp.cpp",
                                const char* function = "CaptureFrame") {
    tracing::SpanRecord record{};
    record.trace_id = tracing::TraceId{0xAAAA0000ULL + index};
    record.span_id = tracing::SpanId{0xBBBB0000ULL + index};
    record.parent_span_id = tracing::SpanId{0xCCCC0000ULL + index};
    record.kind = tracing::SpanKind::Server;
    record.status = tracing::SpanStatus::Ok;
    record.flags = tracing::SpanRecord::flag_ended_explicit;
    record.thread_id = static_cast<std::uint32_t>(100U + index);
    record.start = chrono::Timestamp{1'000'000U + index};
    record.end = chrono::Timestamp{1'000'500U + index};
    record.file_name = file;
    record.function_name = function;
    record.line = static_cast<std::uint32_t>(10U + index);
    record.column = static_cast<std::uint32_t>(2U + index);
    std::snprintf(record.name, sizeof(record.name), "span_%llu",
                  static_cast<unsigned long long>(index));
    if (record.attributes[0].store("frame", static_cast<int>(index))) {
        record.attribute_count = 1U;
    }
    return record;
}

std::uint32_t receive_frames(com::Subscriber<tracing::SpanFrame>& subscriber,
                             tracing::SpanFrame* out, std::uint32_t capacity) {
    std::uint32_t received = 0U;
    for (int attempt = 0; attempt < 1000 && received < capacity; ++attempt) {
        bool drained = false;
        for (;;) {
            auto got = subscriber.receive();
            NX_CHECK(got.has_value());
            if (!got.has_value() || !got->has_value()) {
                break;
            }
            if (received < capacity) {
                out[received] = (*got)->payload();
            }
            ++received;
            drained = true;
        }
        if (!drained) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    return received;
}

void test_concept_and_diagnostics() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    tracing::Iceoryx2TraceSinkConfig config;
    config.app_id = "TEST";
    config.service_name = "Nexigen/Test/TraceSinkDiag";
    tracing::Iceoryx2TraceSink sink{config, &*node};

    NX_CHECK(tracing::to_string(sink) == sink.to_string());
    NX_CHECK(std::format("{}", sink) == sink.to_string());
    NX_CHECK(sink.to_string().find("TEST") != std::string::npos);
    NX_CHECK(!sink.started());

    const nlohmann::json json = sink;
    NX_CHECK(json["service_name"] == config.service_name);
    NX_CHECK(json["app_id"] == "TEST");
    NX_CHECK(json["started"] == false);
    NX_CHECK(json.contains("published"));
    NX_CHECK(json.contains("dropped"));
    NX_CHECK(json.contains("hash_hits"));
    NX_CHECK(json.contains("hash_misses"));

    // Not started: on_span reports NotInitialized and never touches transport.
    const auto record = make_record(0U);
    NX_CHECK(!sink.on_span(record).has_value());
    NX_CHECK(sink.flush().has_value());
}

void test_publish_roundtrip() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }

    tracing::Iceoryx2TraceSinkConfig config;
    config.app_id = "LIDR";
    config.service_name = "Nexigen/Test/TraceSinkRoundTrip";
    tracing::Iceoryx2TraceSink sink{config, &*node};
    NX_CHECK(sink.start().has_value());

    const auto name = com::ServiceName::create(config.service_name.c_str());
    NX_CHECK(name.has_value());
    auto subscriber = com::Subscriber<tracing::SpanFrame>::create(*node, *name, 8U, 8U);
    NX_CHECK(subscriber.has_value());
    if (!subscriber.has_value()) {
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    tracing::SpanRecord record = make_record(3U);
    const std::string_view long_text = "0123456789abcdefghijklmnopqrstuvwxyz";
    NX_CHECK(record.attributes[1].store("blob", long_text));
    NX_CHECK(!record.attributes[1].text_inline); // zero-copy view arm
    record.attribute_count = 2U;
    NX_CHECK(sink.on_span(record).has_value());

    tracing::SpanFrame frame{};
    NX_CHECK(receive_frames(*subscriber, &frame, 1U) == 1U);

    NX_CHECK(frame.trace_id == record.trace_id);
    NX_CHECK(frame.span_id == record.span_id);
    NX_CHECK(frame.parent_span_id == record.parent_span_id);
    NX_CHECK(frame.kind == record.kind);
    NX_CHECK(frame.status == record.status);
    NX_CHECK(frame.flags == record.flags);
    NX_CHECK(frame.thread_id == record.thread_id);
    NX_CHECK(frame.start_ns == record.start.nanoseconds());
    NX_CHECK(frame.end_ns == record.end.nanoseconds());
    NX_CHECK(frame.file_hash == tracing::detail::fnv1a_64(record.file_name));
    NX_CHECK(frame.function_hash == tracing::detail::fnv1a_64(record.function_name));
    NX_CHECK(frame.line == record.line);
    NX_CHECK(frame.column == record.column);
    NX_CHECK(frame.name_view() == record.name_view());
    NX_CHECK(frame.attribute_count == 2U);
    NX_CHECK(frame.attributes[0].type == tracing::SpanAttributeType::Int64);
    NX_CHECK(frame.attributes[0].value.i64 == 3);
    // The zero-copy string pointer cannot cross the process boundary: the
    // wire attribute is the inline (clipped) arm with no address.
    NX_CHECK(frame.attributes[1].type == tracing::SpanAttributeType::String);
    NX_CHECK(frame.attributes[1].text_inline);
    NX_CHECK(frame.attributes[1].text_view() ==
             long_text.substr(0U, tracing::attribute_inline_text_capacity));
    NX_CHECK(frame.app_id_text() == "LIDR");
    NX_CHECK(frame.pid == sink.pid());
    NX_CHECK(frame.sequence == 0U);
    NX_CHECK(frame.publish_ts_ns != 0U);
    NX_CHECK(frame.version == tracing::SpanFrame::format_version);

    NX_CHECK(sink.flush().has_value());
    NX_CHECK(sink.published() == 1U);
    NX_CHECK(sink.dropped() == 0U);
    sink.stop();
    NX_CHECK(!sink.started());
}

void test_memo_cache() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }

    tracing::Iceoryx2TraceSinkConfig config;
    config.app_id = "MEMO";
    config.service_name = "Nexigen/Test/TraceSinkMemo";
    config.subscriber_max_buffer_depth = 256U;
    tracing::Iceoryx2TraceSink sink{config, &*node};
    NX_CHECK(sink.start().has_value());

    // No subscriber needed for the counter semantics: sends succeed into an
    // empty service and the frames are discarded.
    const tracing::SpanRecord record = make_record(1U);
    NX_CHECK(sink.on_span(record).has_value());
    NX_CHECK(sink.hash_misses() == 2U); // file + function
    NX_CHECK(sink.hash_hits() == 0U);
    NX_CHECK(sink.on_span(record).has_value());
    NX_CHECK(sink.hash_hits() == 2U);
    NX_CHECK(sink.hash_misses() == 2U);

    // More distinct locations than the cache holds: every frame still carries
    // the hash of its own text. A live subscriber lets us inspect each frame.
    const auto name = com::ServiceName::create(config.service_name.c_str());
    NX_CHECK(name.has_value());
    auto subscriber = com::Subscriber<tracing::SpanFrame>::create(*node, *name, 256U, 256U);
    NX_CHECK(subscriber.has_value());
    if (!subscriber.has_value()) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    constexpr std::size_t distinct = tracing::Iceoryx2TraceSink::hash_cache_capacity + 6U;
    static char paths[distinct][32]{};
    for (std::size_t i = 0; i < distinct; ++i) {
        std::snprintf(paths[i], sizeof(paths[i]), "/src/file_%02zu.cpp", i);
        NX_CHECK(sink.on_span(make_record(i, paths[i], "run")).has_value());
    }
    tracing::SpanFrame frames[distinct]{};
    NX_CHECK(receive_frames(*subscriber, frames, static_cast<std::uint32_t>(distinct)) == distinct);
    for (std::size_t i = 0; i < distinct; ++i) {
        NX_CHECK(frames[i].file_hash == tracing::detail::fnv1a_64(paths[i]));
        NX_CHECK(frames[i].function_hash == tracing::detail::fnv1a_64("run"));
    }

    // Null locations hash to zero without entering the cache.
    const std::uint64_t misses_before = sink.hash_misses();
    tracing::SpanRecord null_location = make_record(99U, nullptr, nullptr);
    NX_CHECK(sink.on_span(null_location).has_value());
    NX_CHECK(sink.hash_misses() == misses_before);
    tracing::SpanFrame null_frame{};
    NX_CHECK(receive_frames(*subscriber, &null_frame, 1U) == 1U);
    NX_CHECK(null_frame.file_hash == 0U);
    NX_CHECK(null_frame.function_hash == 0U);
    sink.stop();
}

void test_overflow_and_gap() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }

    tracing::Iceoryx2TraceSinkConfig config;
    config.app_id = "OVFL";
    config.service_name = "Nexigen/Test/TraceSinkOverflow";
    config.subscriber_max_buffer_depth = 1U;
    tracing::Iceoryx2TraceSink sink{config, &*node};
    NX_CHECK(sink.start().has_value());

    const auto name = com::ServiceName::create(config.service_name.c_str());
    NX_CHECK(name.has_value());
    auto subscriber = com::Subscriber<tracing::SpanFrame>::create(*node, *name, 1U, 1U);
    NX_CHECK(subscriber.has_value());
    if (!subscriber.has_value()) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    NX_CHECK(sink.on_span(make_record(0U)).has_value()); // seq 0, queued
    NX_CHECK(sink.on_span(make_record(1U)).has_value()); // seq 1, queue full -> dropped
    NX_CHECK(sink.dropped() >= 1U);

    tracing::SpanFrame first{};
    NX_CHECK(receive_frames(*subscriber, &first, 1U) == 1U);
    NX_CHECK(first.sequence == 0U);

    NX_CHECK(sink.on_span(make_record(2U)).has_value()); // seq 2, delivered
    tracing::SpanFrame third{};
    NX_CHECK(receive_frames(*subscriber, &third, 1U) == 1U);
    NX_CHECK(third.sequence == 2U); // the dropped 1 is an observable gap
    NX_CHECK(sink.flush().has_value());
    sink.stop();
}

void test_burst_and_sequences() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }

    tracing::Iceoryx2TraceSinkConfig config;
    config.app_id = "BRST";
    config.service_name = "Nexigen/Test/TraceSinkBurst";
    config.subscriber_max_buffer_depth = 16U;
    tracing::Iceoryx2TraceSink sink{config, &*node};
    NX_CHECK(sink.start().has_value());

    const auto name = com::ServiceName::create(config.service_name.c_str());
    NX_CHECK(name.has_value());
    auto subscriber = com::Subscriber<tracing::SpanFrame>::create(*node, *name, 16U, 16U);
    NX_CHECK(subscriber.has_value());
    if (!subscriber.has_value()) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    constexpr std::uint32_t count = 8U;
    for (std::uint32_t i = 0; i < count; ++i) {
        NX_CHECK(sink.on_span(make_record(i)).has_value());
    }
    tracing::SpanFrame frames[count]{};
    NX_CHECK(receive_frames(*subscriber, frames, count) == count);
    for (std::uint32_t i = 0; i < count; ++i) {
        NX_CHECK(frames[i].sequence == i);
    }
    NX_CHECK(sink.dropped() == 0U);
    sink.stop();
}

void test_allocation_free() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }

    tracing::Iceoryx2TraceSinkConfig config;
    config.app_id = "ALOC";
    config.service_name = "Nexigen/Test/TraceSinkAlloc";
    tracing::Iceoryx2TraceSink sink{config, &*node};
    NX_CHECK(sink.start().has_value());

    const tracing::SpanRecord record = make_record(7U);
    for (int i = 0; i < 64; ++i) { // warm up lazy transport paths first
        NX_CHECK(sink.on_span(record).has_value());
    }
    const std::size_t before = arena_used();
    for (int i = 0; i < 1000; ++i) {
        NX_CHECK(sink.on_span(record).has_value());
    }
    NX_CHECK(arena_used() == before);
    sink.stop();
}

#if defined(__x86_64__)
struct LatencyStats {
    double median_ns{0.0};
    double p99_ns{0.0};
    double p999_ns{0.0};
    double max_ns{0.0};
};

LatencyStats stats_of(const std::uint64_t* sorted, std::size_t count, double ns_per_tick,
                      double overhead_ns) {
    LatencyStats stats{};
    const auto at = [&](std::size_t index) {
        return static_cast<double>(sorted[index]) * ns_per_tick - overhead_ns;
    };
    stats.median_ns = at(count / 2U);
    stats.p99_ns = at((count * 99U) / 100U);
    stats.p999_ns = at((count * 999U) / 1000U);
    stats.max_ns = at(count - 1U);
    return stats;
}

void test_latency() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }

    tracing::Iceoryx2TraceSinkConfig config;
    config.app_id = "LAT";
    config.service_name = "Nexigen/Test/TraceSinkLatency";
    tracing::Iceoryx2TraceSink sink{config, &*node};
    NX_CHECK(sink.start().has_value());

    // Pin the worker (this thread is the sink caller) to the first allowed
    // core; restore afterwards.
    cpu_set_t original{};
    bool pinned = false;
    if (::pthread_getaffinity_np(::pthread_self(), sizeof(original), &original) == 0) {
        for (std::size_t core = 0; core < static_cast<std::size_t>(CPU_SETSIZE); ++core) {
            if (CPU_ISSET(core, &original)) {
                cpu_set_t mine{};
                CPU_SET(core, &mine);
                pinned = ::pthread_setaffinity_np(::pthread_self(), sizeof(mine), &mine) == 0;
                break;
            }
        }
    }

    const tracing::SpanRecord record = make_record(1U);
    for (int i = 0; i < 2000; ++i) {
        NX_CHECK(sink.on_span(record).has_value());
    }

    const std::uint64_t t0 = __rdtsc();
    const auto m0 = chrono::Timestamp::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto m1 = chrono::Timestamp::now();
    const std::uint64_t t1 = __rdtsc();
    NX_CHECK(m0.has_value() && m1.has_value());
    const double ns_per_tick =
        static_cast<double>(m1->nanoseconds() - m0->nanoseconds()) / static_cast<double>(t1 - t0);

    constexpr std::size_t n = 10'000U;
    static std::uint64_t raw[n]{};
    static std::uint64_t overhead[n]{};
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t r0 = __rdtsc();
        NX_CHECK(sink.on_span(record).has_value());
        const std::uint64_t r1 = __rdtsc();
        raw[i] = r1 - r0;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t r0 = __rdtsc();
        const std::uint64_t r1 = __rdtsc();
        overhead[i] = r1 - r0;
    }
    std::sort(raw, raw + n);
    std::sort(overhead, overhead + n);
    const double overhead_ns = static_cast<double>(overhead[n / 2U]) * ns_per_tick;
    const LatencyStats stats = stats_of(raw, n, ns_per_tick, overhead_ns);
    std::printf("latency: on_span median %.1f ns  p99 %.1f ns  p99.9 %.1f ns  max %.1f ns "
                "(gate 400, overhead %.2f ns, ns/tick %.4f)\n",
                stats.median_ns, stats.p99_ns, stats.p999_ns, stats.max_ns, overhead_ns,
                ns_per_tick);

#if defined(NEXIGEN_TRACING_STRICT_LATENCY) && !defined(NX_SANITIZER_BUILD)
    NX_CHECK(stats.median_ns <= 400.0);
#endif

    if (pinned) {
        static_cast<void>(::pthread_setaffinity_np(::pthread_self(), sizeof(original), &original));
    }
    sink.stop();
}
#else
void test_latency() {
    auto node = com::Node::create();
    if (!node.has_value()) {
        NX_CHECK(false);
        return;
    }
    tracing::Iceoryx2TraceSinkConfig config;
    config.service_name = "Nexigen/Test/TraceSinkLatency";
    tracing::Iceoryx2TraceSink sink{config, &*node};
    NX_CHECK(sink.start().has_value());
    for (int i = 0; i < 1000; ++i) {
        NX_CHECK(sink.on_span(make_record(1U)).has_value());
    }
    std::printf("latency: accounting-only run (non-x86)\n");
    sink.stop();
}
#endif

void test_sink() {
    test_concept_and_diagnostics();
    test_publish_roundtrip();
    test_memo_cache();
    test_overflow_and_gap();
    test_burst_and_sequences();
    test_allocation_free();
    test_latency();
}

} // namespace

int main() {
    run_suite("trace_sink", test_sink);
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
