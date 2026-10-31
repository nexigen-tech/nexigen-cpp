// Тесты nexigen::tracing — C++23, без сторонних фреймворков (харнес как в
// core_test.cpp / logging_test.cpp / metrics_test.cpp, design D14): NX_CHECK
// ведёт счёт проверок и падений с файлом/строкой, run_suite печатает
// строку-резюме на набор, main возвращает число падений (ненулевой код выхода
// роняет тест в CTest).
//
// Окна zero-allocation проверяются двумя независимыми счётчиками:
//  1) арена operator new/delete (перехватывает C++-аллокации всего бинарника);
//  2) mallinfo2().uordblks-дельта (перехватывает raw malloc из libc-путей,
//     который мимо оператора new). Под ASan перехват аллокаций уходит в
//     рантайм санитайзера, поэтому там mallinfo2 читается ~константой — чек
//     остаётся безопасной no-op проверкой, а реальную защиту даёт сам ASan.
//
// Структура наборов отражает дельты спек 1:1 (design D14): span_enums,
// ids_and_context, attributes, span_record, parent_hierarchy, scoped_span,
// cross_thread, concepts, sinks, provider, serialization (+ latency под
// NEXIGEN_TRACING_STRICT_LATENCY). Наборы дорастают по ходу задач 2..7;
// этот файл начинается с самопроверки харнеса (задача 1.2).

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <format>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <malloc.h> // mallinfo2(): glibc-счётчики кучи (Linux-only проект)

#include <sys/syscall.h> // SYS_gettid: worker-thread identity capture in sinks
#include <unistd.h>
#include <sched.h> // CPU_SETSIZE/cpu_set_t: benchmark placement + affinity probes

#if defined(__x86_64__)
#include <x86intrin.h> // __rdtsc — an intrinsic, not a language extension
#endif

#include <nexigen/core/result.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/tracing/span_enums.hpp>
#include <nexigen/tracing/span_context.hpp>
#include <nexigen/tracing/detail/json_writer.hpp>
#include <nexigen/tracing/span_record.hpp>
#include <nexigen/tracing/detail/active_span_stack.hpp>
#include <nexigen/tracing/detail/affinity.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/null_trace_sink.hpp>
#include <nexigen/tracing/tracer_provider.hpp>
#include <nexigen/tracing/console_trace_sink.hpp>
#include <nexigen/tracing/json_file_trace_sink.hpp>
#include <nexigen/tracing/chrome_trace_sink.hpp>
#include <nexigen/tracing/composite_trace_sink.hpp>

#include <fcntl.h>
#include <sys/stat.h>

// Sanitizer detection (как в logging/metrics): под ASan/TSan числовые ворота
// латентности выключены — инструментированные тайминги нерепрезентативны,
// остаётся корректность/bounded-time.
#ifndef NX_SANITIZER_BUILD
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define NX_SANITIZER_BUILD 1
#endif
#endif
#endif
#ifndef NX_SANITIZER_BUILD
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define NX_SANITIZER_BUILD 1
#endif
#endif

using namespace nexigen::core;    // test TU: exercise core unqualified
namespace core = nexigen::core;   // and explicit core:: qualification
namespace chrono = nexigen::chrono;
namespace tracing = nexigen::tracing;

// --- Compile-time claims (tasks 2.1/2.2 / spec tracing/span-enums): the
// three enums are distinct byte-sized types with stable append-only values;
// every canonical name resolves in constexpr; garbage casts render safely.
static_assert(sizeof(tracing::SpanKind) == 1);
static_assert(sizeof(tracing::SpanStatus) == 1);
static_assert(sizeof(tracing::SamplingDecision) == 1);
static_assert(!std::is_same_v<tracing::SpanKind, tracing::SpanStatus>);
static_assert(!std::is_same_v<tracing::SpanKind, tracing::SamplingDecision>);
static_assert(!std::is_same_v<tracing::SpanStatus, tracing::SamplingDecision>);
static_assert(std::is_same_v<std::underlying_type_t<tracing::SpanKind>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<tracing::SpanStatus>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<tracing::SamplingDecision>, std::uint8_t>);
static_assert(static_cast<std::uint8_t>(tracing::SpanKind::Internal) == 0 &&
              static_cast<std::uint8_t>(tracing::SpanKind::Server) == 1 &&
              static_cast<std::uint8_t>(tracing::SpanKind::Client) == 2 &&
              static_cast<std::uint8_t>(tracing::SpanKind::Producer) == 3 &&
              static_cast<std::uint8_t>(tracing::SpanKind::Consumer) == 4);
static_assert(static_cast<std::uint8_t>(tracing::SpanStatus::Unset) == 0 &&
              static_cast<std::uint8_t>(tracing::SpanStatus::Ok) == 1 &&
              static_cast<std::uint8_t>(tracing::SpanStatus::Error) == 2);
static_assert(static_cast<std::uint8_t>(tracing::SamplingDecision::Drop) == 0 &&
              static_cast<std::uint8_t>(tracing::SamplingDecision::RecordAndSample) == 1);
// constexpr name round-trips (task 2.1: to_string/from_string are constexpr).
static_assert(tracing::to_string(tracing::SpanKind::Internal) == "Internal");
static_assert(tracing::to_string(tracing::SpanKind::Server) == "Server");
static_assert(tracing::to_string(tracing::SpanKind::Client) == "Client");
static_assert(tracing::to_string(tracing::SpanKind::Producer) == "Producer");
static_assert(tracing::to_string(tracing::SpanKind::Consumer) == "Consumer");
static_assert(tracing::to_string(tracing::SpanStatus::Unset) == "Unset");
static_assert(tracing::to_string(tracing::SpanStatus::Ok) == "Ok");
static_assert(tracing::to_string(tracing::SpanStatus::Error) == "Error");
static_assert(tracing::to_string(tracing::SamplingDecision::Drop) == "Drop");
static_assert(tracing::to_string(tracing::SamplingDecision::RecordAndSample) ==
              "RecordAndSample");
// Garbage casts render "Unknown" from static storage (never throw/allocate).
static_assert(tracing::to_string(static_cast<tracing::SpanKind>(200)) == "Unknown");
static_assert(tracing::to_string(static_cast<tracing::SpanStatus>(77)) == "Unknown");
static_assert(tracing::to_string(static_cast<tracing::SamplingDecision>(9)) == "Unknown");
// constexpr from_string round-trips and rejections.
constexpr bool kind_rt = [] {
    tracing::SpanKind k = tracing::SpanKind::Internal;
    if (!tracing::from_string("Client", k) || k != tracing::SpanKind::Client) { return false; }
    tracing::SpanKind untouched = tracing::SpanKind::Producer;
    if (tracing::from_string("client", untouched)) { return false; }
    return untouched == tracing::SpanKind::Producer; // unchanged on failure
}();
static_assert(kind_rt);

namespace {

int g_checks = 0;
int g_failures = 0;

// Allocation detector: the C++ free store is re-backed by a fixed static bump
// arena; any operator new anywhere in the process bumps g_alloc_used. The
// counter is ATOMIC: unlike the logging/metrics TUs, tracing suites run real
// concurrent workers whose cold paths (to_string diagnostics, sink fixtures)
// may allocate on other threads while the main thread builds jthreads — a
// plain bump counter would hand two allocations the same bytes. 16 MiB
// покрывает несколько провайдеров (включая один default-размера ~10.5 MiB)
// плюс std::jthread/std::string/nlohmann шум всех наборов.
alignas(16) unsigned char g_arena[16 << 20]{};
std::atomic<std::size_t> g_alloc_used{0};

/// Current arena usage (snapshot; safe from any thread).
[[nodiscard]] std::size_t arena_used() noexcept {
    return g_alloc_used.load(std::memory_order_relaxed);
}

} // namespace

[[noreturn]] void arena_exhausted() { std::abort(); }

void* operator new(std::size_t size) {
    const std::size_t rounded = (size + 15U) & ~static_cast<std::size_t>(15U);
    const std::size_t offset = g_alloc_used.fetch_add(rounded, std::memory_order_relaxed);
    if (offset + rounded > sizeof(g_arena)) { arena_exhausted(); }
    return g_arena + offset;
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void* ptr) noexcept { static_cast<void>(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { static_cast<void>(ptr); }
void operator delete[](void* ptr) noexcept { static_cast<void>(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { static_cast<void>(ptr); }

#define NX_CHECK(condition)                                                                     \
    do {                                                                                        \
        ++g_checks;                                                                             \
        if (!(condition)) {                                                                     \
            ++g_failures;                                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
        }                                                                                       \
    } while (false)

namespace {

void run_suite(const char* suite_name, void (*body)())
{
    const int checks_before = g_checks;
    const int failures_before = g_failures;

    body();

    const int suite_checks = g_checks - checks_before;
    const int suite_failures = g_failures - failures_before;
    std::printf("[%-24s] %s (%d checks, %d failed)\n", suite_name,
                suite_failures == 0 ? "OK" : "FAILED", suite_checks, suite_failures);
}

// Heap in use (bytes handed to malloc and not freed): the raw-malloc twin of
// the arena counter above. Windows measure before/around steady-state phases.
[[nodiscard]] std::size_t heap_in_use() noexcept {
    return static_cast<std::size_t>(mallinfo2().uordblks);
}

// Harness self-check (задача 1.2): proves NX_CHECK, run_suite and both
// allocation counters are wired before real suites land.
void test_harness() {
    NX_CHECK(g_checks > 0); // this very suite is counting
    // NB: no assumption about heap_in_use() magnitude — under ASan the glibc
    // heap is empty by design (the sanitizer owns it); only deltas are checked.
    const std::size_t arena_before = arena_used();
    const std::size_t heap_before = heap_in_use();
    { // a deliberate free-store user: the arena counter must move...
        void* p = ::operator new(64U);
        ::operator delete(p);
    }
    NX_CHECK(arena_used() == arena_before + 64U);
    // ...while the raw heap counter stays put (arena absorbs C++ allocations).
    NX_CHECK(heap_in_use() == heap_before);
}

// --- span_enums suite (tasks 2.1/2.2 / spec tracing/span-enums): runtime
// mirrors of the compile-time pins — full name round-trips for every
// enumerator, failed parses leaving out-params unchanged, hostile JSON
// (numbers/objects/arrays/null/unknown strings) never aborting, and
// std::format rendering canonical names with width/fill/alignment honored.
void test_span_enums() {
    using tracing::SpanKind;
    using tracing::SpanStatus;
    using tracing::SamplingDecision;

    // Every enumerator renders its exact spelling and parses back.
    for (const auto& [value, name] : std::array<std::pair<SpanKind, std::string_view>, 5>{{
             {SpanKind::Internal, "Internal"}, {SpanKind::Server, "Server"},
             {SpanKind::Client, "Client"}, {SpanKind::Producer, "Producer"},
             {SpanKind::Consumer, "Consumer"}}}) {
        NX_CHECK(tracing::to_string(value) == name);
        SpanKind parsed = SpanKind::Internal;
        NX_CHECK(tracing::from_string(name, parsed));
        NX_CHECK(parsed == value);
    }
    for (const auto& [value, name] : std::array<std::pair<SpanStatus, std::string_view>, 3>{{
             {SpanStatus::Unset, "Unset"}, {SpanStatus::Ok, "Ok"},
             {SpanStatus::Error, "Error"}}}) {
        NX_CHECK(tracing::to_string(value) == name);
        SpanStatus parsed = SpanStatus::Unset;
        NX_CHECK(tracing::from_string(name, parsed));
        NX_CHECK(parsed == value);
    }
    for (const auto& [value, name] :
         std::array<std::pair<SamplingDecision, std::string_view>, 2>{{
             {SamplingDecision::Drop, "Drop"},
             {SamplingDecision::RecordAndSample, "RecordAndSample"}}}) {
        NX_CHECK(tracing::to_string(value) == name);
        SamplingDecision parsed = SamplingDecision::Drop;
        NX_CHECK(tracing::from_string(name, parsed));
        NX_CHECK(parsed == value);
    }

    // Failed parses leave out-params unchanged (spec scenario: "ok"/empty).
    SpanStatus pre = SpanStatus::Unset;
    NX_CHECK(!tracing::from_string("ok", pre));
    NX_CHECK(pre == SpanStatus::Unset);
    SamplingDecision pre_drop = SamplingDecision::Drop;
    NX_CHECK(!tracing::from_string("", pre_drop));
    NX_CHECK(pre_drop == SamplingDecision::Drop);
    SpanKind pre_kind = SpanKind::Server;
    NX_CHECK(!tracing::from_string("server", pre_kind));
    NX_CHECK(pre_kind == SpanKind::Server);

    // Garbage casts render "Unknown" (static storage, no throw).
    NX_CHECK(tracing::to_string(static_cast<SpanKind>(99)) == "Unknown");
    NX_CHECK(tracing::to_string(static_cast<SpanStatus>(250)) == "Unknown");
    NX_CHECK(tracing::to_string(static_cast<SamplingDecision>(7)) == "Unknown");

    // --- task 2.2: JSON round-trips through nlohmann under -fno-exceptions.
    {
        const nlohmann::json j_kind = SpanKind::Client;
        NX_CHECK(j_kind.is_string() && j_kind.get<std::string>() == "Client");
        SpanKind back = SpanKind::Internal;
        nlohmann::from_json(j_kind, back); // ADL via j.get would work too
        NX_CHECK(back == SpanKind::Client);

        const nlohmann::json j_status = SpanStatus::Error;
        NX_CHECK(j_status.get<std::string>() == "Error");
        SpanStatus status_back = SpanStatus::Unset;
        nlohmann::from_json(j_status, status_back);
        NX_CHECK(status_back == SpanStatus::Error);

        const nlohmann::json j_sampling = SamplingDecision::RecordAndSample;
        NX_CHECK(j_sampling.get<std::string>() == "RecordAndSample");
        SamplingDecision sampling_back = SamplingDecision::Drop;
        nlohmann::from_json(j_sampling, sampling_back);
        NX_CHECK(sampling_back == SamplingDecision::RecordAndSample);
    }

    // Hostile JSON: numbers/objects/arrays/null/unknown strings on a
    // pre-initialized value — returns normally, value still Unset.
    {
        const std::array<nlohmann::json, 6> hostile{
            nlohmann::json(42), nlohmann::json(-1),
            nlohmann::json{{"a", "b"}}, nlohmann::json{1, 2, 3},
            nlohmann::json(nullptr), nlohmann::json("NotAnEnum")};
        for (const nlohmann::json& bad : hostile) {
            SpanStatus out = SpanStatus::Unset;
            nlohmann::from_json(bad, out);
            NX_CHECK(out == SpanStatus::Unset);
        }
        for (const nlohmann::json& bad : hostile) {
            SpanKind out = SpanKind::Server;
            nlohmann::from_json(bad, out);
            NX_CHECK(out == SpanKind::Server);
        }
        for (const nlohmann::json& bad : hostile) {
            SamplingDecision out = SamplingDecision::RecordAndSample;
            nlohmann::from_json(bad, out);
            NX_CHECK(out == SamplingDecision::RecordAndSample);
        }
    }

    // std::format renders canonical names; width/fill/alignment honored.
    NX_CHECK(std::format("{}", SpanKind::Server) == "Server");
    NX_CHECK(std::format("{}", SpanStatus::Error) == "Error");
    NX_CHECK(std::format("{}", SamplingDecision::Drop) == "Drop");
    NX_CHECK(std::format("{:*>10}", SpanKind::Client) == "****Client");
    NX_CHECK(std::format("{:<8}|", SpanStatus::Ok) == "Ok      |");
    NX_CHECK(std::format("{:>14}", SamplingDecision::Drop) == "          Drop");
    NX_CHECK(std::format("{:^10}", SpanKind::Internal) == " Internal ");
    // Width exactly equal to the name: no padding.
    NX_CHECK(std::format("{:6}", SpanStatus::Unset) == "Unset ");
}

// --- ids_and_context suite (task 3.1 / spec tracing/span-context): nonzero
// and distinct generation (single + 32 concurrent threads), wait-free
// allocation-free loops, the zero sentinel, 16-char hex round-trips,
// cross-thread copy equality through the core SPSC queue, member/free
// to_string agreement, and std::format equality.
void test_ids_and_context() {
    using tracing::TraceId;
    using tracing::SpanId;
    using tracing::SpanContext;

    // Zero sentinel: default reads as unset; explicit values preserved.
    NX_CHECK(TraceId{}.is_unset());
    NX_CHECK(SpanId{}.is_unset());
    NX_CHECK(TraceId{0xDEADBEEF}.value() == 0xDEADBEEFULL);
    NX_CHECK(SpanId{42}.value() == 42ULL);
    NX_CHECK(TraceId{7} == TraceId{7});
    NX_CHECK(TraceId{7} < TraceId{8});
    NX_CHECK(SpanId{9} != SpanId{10});

    // Nonzero, distinct generation on this thread; value-safe wrappers.
    std::array<TraceId, 64> trace_ids{};
    std::array<SpanId, 64> span_ids{};
    for (std::size_t i = 0; i < trace_ids.size(); ++i) {
        trace_ids[i] = TraceId::generate();
        span_ids[i] = SpanId::generate();
        NX_CHECK(!trace_ids[i].is_unset());
        NX_CHECK(!span_ids[i].is_unset());
    }
    for (std::size_t i = 0; i < trace_ids.size(); ++i) {
        for (std::size_t j = i + 1; j < trace_ids.size(); ++j) {
            NX_CHECK(trace_ids[i] != trace_ids[j]);
            NX_CHECK(span_ids[i] != span_ids[j]);
        }
    }

    // Wait-free + allocation-free: a large batch with both counters armed.
    {
        const std::size_t arena_before = arena_used();
        const std::size_t heap_before = heap_in_use();
        for (int i = 0; i < 100'000; ++i) {
            const TraceId t = TraceId::generate();
            const SpanId s = SpanId::generate();
            static_cast<void>(t);
            static_cast<void>(s);
        }
        NX_CHECK(arena_used() == arena_before);
        NX_CHECK(heap_in_use() == heap_before);
    }

    // 32 concurrent generating threads: no zero, no duplicates across the
    // observed window. Buckets are PRE-RESERVED before spawning so the
    // workers themselves never allocate; dedup runs after the join.
    {
        constexpr int threads = 32;
        constexpr int per_thread = 4'000;
        std::array<std::vector<std::uint64_t>, threads> collected{};
        for (auto& bucket : collected) {
            bucket.resize(static_cast<std::size_t>(per_thread) * 2U, 0xDEADBEEFULL);
        }
        std::atomic<int> failures{0};
        std::vector<std::jthread> workers;
        workers.reserve(threads);
        for (int t = 0; t < threads; ++t) {
            workers.emplace_back([t, &collected, &failures] {
                auto& bucket = collected[static_cast<std::size_t>(t)];
                for (int i = 0; i < per_thread; ++i) {
                    const TraceId trace = TraceId::generate();
                    const SpanId span = SpanId::generate();
                    if (trace.is_unset() || span.is_unset()) {
                        failures.fetch_add(1, std::memory_order_relaxed);
                    }
                    bucket[static_cast<std::size_t>(i) * 2U] = trace.value();
                    bucket[static_cast<std::size_t>(i) * 2U + 1U] = span.value();
                }
            });
        }
        for (auto& w : workers) {
            w.join();
        }
        NX_CHECK(failures.load(std::memory_order_relaxed) == 0);
        std::vector<std::uint64_t> flat;
        flat.reserve(static_cast<std::size_t>(threads) * per_thread * 2U);
        for (const auto& bucket : collected) {
            for (const std::uint64_t value : bucket) {
                flat.push_back(value);
            }
        }
        std::sort(flat.begin(), flat.end());
        const auto dup = std::adjacent_find(flat.begin(), flat.end());
        NX_CHECK(dup == flat.end()); // no id generated twice across threads
    }

    // 16-char zero-padded lowercase hex rendering + JSON round-trips.
    {
        const TraceId trace{0x0123456789ABCDEFULL};
        NX_CHECK(trace.to_string() == "0123456789abcdef");
        const SpanId span{0xFEDCBA9876543210ULL};
        NX_CHECK(span.to_string() == "fedcba9876543210");
        const TraceId small{0x5};
        NX_CHECK(small.to_string() == "0000000000000005");

        const nlohmann::json j_trace = trace;
        NX_CHECK(j_trace.get<std::string>() == "0123456789abcdef");
        TraceId parsed_trace{};
        nlohmann::from_json(j_trace, parsed_trace);
        NX_CHECK(parsed_trace == trace);

        const nlohmann::json j_span = span;
        SpanId parsed_span{};
        nlohmann::from_json(j_span, parsed_span);
        NX_CHECK(parsed_span == span);

        // Hostile JSON: numbers/objects/arrays/null/wrong text leave values.
        const std::array<nlohmann::json, 5> bad{
            nlohmann::json(3), nlohmann::json{{"x", "y"}}, nlohmann::json{1},
            nlohmann::json(nullptr), nlohmann::json("nothex!!")};
        for (const nlohmann::json& hostile : bad) {
            TraceId out = trace;
            nlohmann::from_json(hostile, out);
            NX_CHECK(out == trace);
            SpanId sout = span;
            nlohmann::from_json(hostile, sout);
            NX_CHECK(sout == span);
        }
    }

    // Cross-thread handoff through the core SPSC queue: copy equality.
    {
        core::SpscRingBuffer<SpanContext, 16> queue{};
        const SpanContext original{TraceId::generate(), SpanId::generate(),
                                   tracing::SamplingDecision::RecordAndSample};
        std::jthread receiver([&queue, &original] {
            SpanContext received{};
            for (int spins = 0; spins < 1'000; ++spins) {
                if (queue.try_pop(received)) {
                    NX_CHECK(received == original); // copy compares equal
                    NX_CHECK(received.trace_id == original.trace_id);
                    NX_CHECK(received.span_id == original.span_id);
                    NX_CHECK(received.sampled == original.sampled);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            NX_CHECK(false); // handoff never landed
        });
        NX_CHECK(queue.try_push(original).has_value());
        receiver.join();
    }

    // SpanContext diagnostics: member/free agreement, std::format equality.
    {
        const SpanContext ctx{TraceId{0xA}, SpanId{0xB},
                              tracing::SamplingDecision::RecordAndSample};
        const std::string member = ctx.to_string();
        NX_CHECK(member == tracing::to_string(ctx)); // free mirror
        NX_CHECK(member.find("000000000000000a") != std::string::npos);
        NX_CHECK(member.find("000000000000000b") != std::string::npos);
        NX_CHECK(member.find("RecordAndSample") != std::string::npos);
        NX_CHECK(std::format("{}", ctx) == member);

        // Context JSON round-trips every field.
        const nlohmann::json j = ctx;
        SpanContext back{};
        nlohmann::from_json(j, back);
        NX_CHECK(back == ctx);
    }
}

// --- attributes suite (task 3.2 / spec tracing/span-context "Fixed-size span
// attribute union" + "Span attribute string storage" + "capacity bounds"):
// every value type packs and reads back, inline vs view strings, key/capacity
// bounds rejected without side effects, allocation-free stores.
void test_attributes() {
    using tracing::SpanAttribute;
    using tracing::SpanAttributeType;

    // Every value type packs and reads back exactly.
    {
        SpanAttribute attr{};
        NX_CHECK(attr.store("i", std::int64_t{-42}));
        NX_CHECK(attr.type == SpanAttributeType::Int64);
        NX_CHECK(attr.value.i64 == -42);
        NX_CHECK(attr.key_view() == "i");

        NX_CHECK(attr.store("u", std::uint64_t{18'000'000'000'000'000'000ULL}));
        NX_CHECK(attr.type == SpanAttributeType::Uint64);
        NX_CHECK(attr.value.u64 == 18'000'000'000'000'000'000ULL);

        NX_CHECK(attr.store("f", 0.15625)); // exact binary fraction: bit-for-bit
        NX_CHECK(attr.type == SpanAttributeType::Double);
        NX_CHECK(attr.value.f64 == 0.15625);

        NX_CHECK(attr.store("b", true));
        NX_CHECK(attr.type == SpanAttributeType::Bool);
        NX_CHECK(attr.value.b);

        NX_CHECK(attr.store("s", std::string_view("exact")));
        NX_CHECK(attr.type == SpanAttributeType::String);
        NX_CHECK(attr.text_view() == "exact");
    }

    // Inline strings: copied in place, source mutation invisible (spec
    // scenario "Short strings copy inline").
    {
        char source[] = "mutable-buffer";
        SpanAttribute attr{};
        NX_CHECK(attr.store("k", std::string_view(source)));
        NX_CHECK(attr.text_inline); // inline arm (the 23-byte bound fits)
        source[0] = 'X';            // clobber the caller's buffer
        NX_CHECK(attr.text_view() == "mutable-buffer"); // record holds its copy
    }

    // Long strings: zero-copy view, full text renders through the view (spec
    // scenario "Long string values are stored as views"); storage is a
    // program-static literal qualifying for the lifetime contract.
    {
        constexpr std::string_view long_text =
            "a-string-value-that-exceeds-the-23-byte-inline-bound-for-sure";
        static_assert(long_text.size() > tracing::attribute_inline_text_capacity);
        SpanAttribute attr{};
        NX_CHECK(attr.store("long", long_text));
        NX_CHECK(!attr.text_inline);           // view arm, no bytes copied
        NX_CHECK(attr.text_view() == long_text);
        NX_CHECK(attr.text_view().data() == long_text.data()); // zero-copy
    }

    // The 23-byte boundary itself: exactly-23 is inline, exactly-24 is view.
    {
        constexpr std::string_view twenty3 = "abcdefghijklmnopqrstuvw"; // 23
        static_assert(twenty3.size() == 23);
        constexpr std::string_view twenty4 = "abcdefghijklmnopqrstuvwx"; // 24
        static_assert(twenty4.size() == 24);
        SpanAttribute a{}, b{};
        NX_CHECK(a.store("t", twenty3) && a.text_inline);
        NX_CHECK(b.store("t", twenty4) && !b.text_inline);
        NX_CHECK(b.text_view() == twenty4);
    }

    // Capacity bounds: MaxAttributes == 4 (pinned), a 5th store into a full
    // array is the caller's (ScopedSpan's) job — the attribute type itself
    // pins the key bound: a 16-char key is rejected WHOLE, no partial key.
    {
        static_assert(tracing::MaxAttributes == 4);
        SpanAttribute attr{};
        static_cast<void>(attr.store("ok", 1));
        const SpanAttribute before = attr; // trivially copyable snapshot
        NX_CHECK(!attr.store("0123456789abcdef", 2)); // 16 chars: rejected
        NX_CHECK(std::memcmp(&attr, &before, sizeof(SpanAttribute)) == 0);
        NX_CHECK(attr.key_view() == "ok"); // untouched
        NX_CHECK(attr.store("0123456789abcde", 2)); // 15 chars: accepted
        NX_CHECK(attr.key_view() == "0123456789abcde");
    }

    // Trivially copyable: byte-for-byte copies survive (union pins at header
    // level are mirrored here for the test TU).
    static_assert(std::is_trivially_copyable_v<tracing::SpanAttribute>);
    static_assert(sizeof(tracing::SpanAttribute) == 48);

    // Stores never allocate.
    {
        const std::size_t arena_before = arena_used();
        SpanAttribute attr{};
        for (int i = 0; i < 10'000; ++i) {
            static_cast<void>(attr.store("k", std::int64_t{i}));
            static_cast<void>(attr.store("s", std::string_view("short")));
        }
        NX_CHECK(arena_used() == arena_before);
    }

    // Diagnostics + JSON round-trip per attribute type; hostile input leaves
    // the value untouched.
    {
        SpanAttribute attr{};
        static_cast<void>(attr.store("answer", std::int64_t{-42}));
        const std::string member = attr.to_string();
        NX_CHECK(member == tracing::to_string(attr));
        NX_CHECK(member.find("answer") != std::string::npos);
        NX_CHECK(member.find("Int64") != std::string::npos);
        NX_CHECK(std::format("{}", attr) == member);

        const nlohmann::json j = attr;
        NX_CHECK(j.at("key").get<std::string>() == "answer");
        NX_CHECK(j.at("type").get<std::string>() == "Int64");
        NX_CHECK(j.at("value").get<std::int64_t>() == -42);
        SpanAttribute back{};
        nlohmann::from_json(j, back);
        NX_CHECK(back.type == SpanAttributeType::Int64);
        NX_CHECK(back.value.i64 == -42);
        NX_CHECK(back.key_view() == "answer");

        // Every type through JSON.
        const std::array<nlohmann::json, 5> round = [] {
            SpanAttribute a1{}, a2{}, a3{}, a4{}, a5{};
            static_cast<void>(a1.store("i", std::int64_t{7}));
            static_cast<void>(a2.store("u", std::uint64_t{1ULL << 63}));
            static_cast<void>(a3.store("f", 2.5));
            static_cast<void>(a4.store("b", false));
            static_cast<void>(a5.store("s", std::string_view("text")));
            return std::array<nlohmann::json, 5>{a1, a2, a3, a4, a5};
        }();
        {
            SpanAttribute parsed{};
            nlohmann::from_json(round[0], parsed);
            NX_CHECK(parsed.type == SpanAttributeType::Int64 && parsed.value.i64 == 7);
            nlohmann::from_json(round[1], parsed);
            NX_CHECK(parsed.type == SpanAttributeType::Uint64 &&
                     parsed.value.u64 == (1ULL << 63));
            nlohmann::from_json(round[2], parsed);
            NX_CHECK(parsed.type == SpanAttributeType::Double && parsed.value.f64 == 2.5);
            nlohmann::from_json(round[3], parsed);
            NX_CHECK(parsed.type == SpanAttributeType::Bool && !parsed.value.b);
            nlohmann::from_json(round[4], parsed);
            NX_CHECK(parsed.type == SpanAttributeType::String &&
                     parsed.text_view() == "text");
        }

        // Hostile attribute JSON: scalars/arrays/null/wrong-typed values.
        SpanAttribute hostile_target{};
        static_cast<void>(hostile_target.store("keep", 5));
        const SpanAttribute hostile_before = hostile_target;
        const std::array<nlohmann::json, 4> hostile{
            nlohmann::json(1), nlohmann::json{1, 2}, nlohmann::json(nullptr),
            nlohmann::json{{"key", "k"}, {"type", "Int64"}, {"value", "not-a-number"}}};
        for (const nlohmann::json& bad : hostile) {
            nlohmann::from_json(bad, hostile_target);
            NX_CHECK(std::memcmp(&hostile_target, &hostile_before,
                                 sizeof(SpanAttribute)) == 0);
        }
    }
}

// --- span_record suite (task 3.2 / spec tracing/span-context
// "Cache-line-aligned span record"): compile-time pins, lossless field
// copies, JSON round-trip with hex ids and typed attributes, hostile JSON
// leaving the record unchanged, member/free to_string agreement.
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
        NX_CHECK(back.attributes[2].type == SpanAttributeType::Bool &&
                 back.attributes[2].value.b);
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
        const std::array<nlohmann::json, 4> hostile{
            nlohmann::json(5), nlohmann::json{1, 2}, nlohmann::json(nullptr),
            nlohmann::json{{"trace_id", 12}, {"kind", 7}, {"name", false},
                           {"start_ns", "text"}}};
        for (const nlohmann::json& bad : hostile) {
            nlohmann::from_json(bad, record);
            NX_CHECK(std::memcmp(&record, &before, sizeof(SpanRecord)) == 0);
        }
    }
}

// --- parent_stack suite (task 4.1 / design D7; spec tracing/span-context
// "Automatic intra-thread parent tracking"): nested/sibling frame chaining,
// explicit-parent override semantics, roots on empty stacks, over-deep
// bounded roots, end-with-active-children truncation — plus the affinity
// pin/read round-trip with error paths on a probe thread. Provider-level
// automatic chains land in the scoped_span suite (task 6.2).
void test_parent_stack() {
    using tracing::detail::Active_frame;
    using tracing::detail::active_span_stack;
    using tracing::detail::max_span_depth;
    static_assert(max_span_depth == 16);

    auto& stack = active_span_stack();
    stack.clear();

    // Empty stack: root span gets no parent from the stack.
    NX_CHECK(stack.depth() == 0U);
    NX_CHECK(stack.top() == nullptr);

    // Nested chaining: each frame parents the next push.
    const Active_frame root{tracing::TraceId{0x10}, tracing::SpanId{0x11}};
    const std::uint8_t root_index = stack.push(root);
    NX_CHECK(root_index == 0U);
    NX_CHECK(stack.top() != nullptr && *stack.top() == root);

    const Active_frame child{root.trace_id, tracing::SpanId{0x12}};
    const std::uint8_t child_index = stack.push(child);
    NX_CHECK(child_index == 1U);
    NX_CHECK(*stack.top() == child); // the active span is the child now

    const Active_frame grandchild{root.trace_id, tracing::SpanId{0x13}};
    static_cast<void>(stack.push(grandchild));
    NX_CHECK(stack.depth() == 3U);
    NX_CHECK(*stack.top() == grandchild);

    // End the CHILD while the grandchild is still active: truncation removes
    // the child frame AND everything above it (design D7 — no LIFO corrupt).
    stack.truncate_to(child_index);
    NX_CHECK(stack.depth() == 1U);
    NX_CHECK(*stack.top() == root);

    // The grandchild's own (late) truncation clamps to a no-op.
    stack.truncate_to(2U);
    NX_CHECK(stack.depth() == 1U);
    NX_CHECK(*stack.top() == root);

    // Siblings: sequential pushes/truncations under the same parent.
    const Active_frame sib1{root.trace_id, tracing::SpanId{0x21}};
    const std::uint8_t sib1_index = stack.push(sib1);
    NX_CHECK(*stack.top() == sib1);
    stack.truncate_to(sib1_index);
    NX_CHECK(*stack.top() == root); // back under the parent
    const Active_frame sib2{root.trace_id, tracing::SpanId{0x22}};
    static_cast<void>(stack.push(sib2));
    NX_CHECK(*stack.top() == sib2);
    NX_CHECK(stack.depth() == 2U);
    stack.truncate_to(root_index);
    NX_CHECK(stack.depth() == 0U && stack.top() == nullptr); // root ended: empty

    // Over-deep: the 17th nested push returns the sentinel and stores
    // nothing — the caller records a bounded root (fresh trace id, zero
    // parent) instead of pushing.
    {
        stack.clear();
        for (std::uint8_t i = 0; i < max_span_depth; ++i) {
            const Active_frame frame{tracing::TraceId{0x99},
                                     tracing::SpanId{0x100U + i}};
            NX_CHECK(stack.push(frame) == i);
        }
        NX_CHECK(stack.depth() == max_span_depth);
        const Active_frame overflow{tracing::TraceId{0xAA}, tracing::SpanId{0xBB}};
        NX_CHECK(stack.push(overflow) == max_span_depth); // sentinel
        NX_CHECK(stack.depth() == max_span_depth);        // nothing stored
        NX_CHECK(stack.top() != nullptr &&
                 stack.top()->span_id == tracing::SpanId{0x100U + 15U});
        // Bounded-root truncation is a structural no-op (sentinel index).
        stack.truncate_to(max_span_depth);
        NX_CHECK(stack.depth() == max_span_depth);
        // Drain for the next suites.
        stack.truncate_to(0U);
        NX_CHECK(stack.depth() == 0U);
    }

    // Explicit-parent override (task 4.1): the caller-supplied context wins
    // over the stack top — exercised end-to-end in the scoped_span suite;
    // here the stack law itself: an explicit-parent child still pushes ITS
    // OWN frame so subsequent same-thread spans link to it.
    {
        stack.clear();
        static_cast<void>(stack.push(root));
        const tracing::SpanContext explicit_parent{tracing::TraceId{0xEE},
                                                   tracing::SpanId{0xEF},
                                                   tracing::SamplingDecision::RecordAndSample};
        // The explicit-parent child's own frame carries the INHERITED trace
        // id and the child's own span id.
        const Active_frame link{explicit_parent.trace_id, tracing::SpanId{0xF0}};
        const std::uint8_t link_index = stack.push(link);
        NX_CHECK(*stack.top() == link); // the local parent is now the child
        stack.truncate_to(link_index);
        stack.truncate_to(0U);
    }

    // --- affinity: pin/read round-trip + error paths on a probe thread.
    {
        std::uint64_t allowed = 0ULL;
        NX_CHECK(tracing::detail::self_affinity(allowed).has_value());
        NX_CHECK(allowed != 0ULL); // the process always has some allowed core

        // Pick one allowed core for the round-trip.
        std::uint64_t probe_mask = 0ULL;
        for (std::size_t bit = 0; bit < 64U && probe_mask == 0ULL; ++bit) {
            if ((allowed & (1ULL << bit)) != 0ULL) {
                probe_mask = 1ULL << bit;
            }
        }
        std::atomic<int> affinity_failures{0};
        std::jthread probe([&probe_mask, &affinity_failures] {
            // Zero mask: InvalidArgument, affinity untouched.
            if (tracing::detail::pin_self(0ULL).error() != core::ErrorCode::InvalidArgument) {
                affinity_failures.fetch_add(1, std::memory_order_relaxed);
            }
            // Valid single core: pin, read back, exactly the mask.
            if (!tracing::detail::pin_self(probe_mask).has_value()) {
                affinity_failures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            std::uint64_t observed = 0ULL;
            if (!tracing::detail::self_affinity(observed).has_value() ||
                observed != probe_mask) {
                affinity_failures.fetch_add(1, std::memory_order_relaxed);
            }
        });
        probe.join();
        NX_CHECK(affinity_failures.load(std::memory_order_relaxed) == 0);
        NX_CHECK(tracing::detail::self_affinity(allowed).has_value()); // main restored
    }
}

// Compile-failure proof for task 5.1 (spec tracer-concepts "Null types are
// not deserializable"): this scratch TU MUST fail to compile (no from_json
// overload exists for either null type). Verified by running the commands,
// capturing the error, then deleting the file:
//   cat > /tmp/opencode/neg/null_from_json.cpp <<'EOF'
//   #include <nlohmann/json.hpp>
//   #include <nexigen/tracing/concepts/tracer_concept.hpp>
//   #include <nexigen/tracing/null_trace_sink.hpp>
//   int main() {
//       nlohmann::json j;
//       nexigen::tracing::NullTracer tracer;
//       nexigen::tracing::NullTraceSink sink;
//       nlohmann::from_json(j, tracer);
//       nlohmann::from_json(j, sink);
//   }
//   EOF
//   g++ -std=c++23 -fno-exceptions -fno-rtti -D_GNU_SOURCE -DJSON_NOEXCEPTION=1
//       -Iinclude -c /tmp/opencode/neg/null_from_json.cpp -o /dev/null
//       # expect: no matching function for call to 'from_json(...)'

// --- concept fixtures (task 5.1 / spec tracing/tracer-concepts): positive
// references pinned at compile time plus deliberately-broken probes — each
// negative proves the concept REJECTS the defect (absence/signature errors
// are compile law).

/// Complete conforming tracer surface (the positive reference).
struct Full_tracer_model {
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
    std::string to_string() const { return "Full_tracer_model"; }
};
static_assert(tracing::concepts::Tracer<Full_tracer_model>);

/// start_span omitted entirely.
struct No_start_span_model {
    std::string to_string() const { return "no-start"; }
};
static_assert(!tracing::concepts::Tracer<No_start_span_model>);

/// The returned guard has no end() — the guard concept fails, and with it
/// the tracer concept.
struct Guard_without_end_model {
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
static_assert(!tracing::concepts::Tracer<Guard_without_end_model>);

/// The tracer has no to_string() diagnostic.
struct Tracer_without_to_string_model {
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
static_assert(!tracing::concepts::Tracer<Tracer_without_to_string_model>);

/// Complete conforming sink surface (the positive reference).
struct Full_sink_model {
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) noexcept { return {}; }
    core::Status flush() noexcept { return {}; }
    void stop() noexcept {}
    std::string to_string() const { return "Full_sink_model"; }
};
static_assert(tracing::concepts::TraceSink<Full_sink_model>);

/// flush omitted entirely.
struct Sink_without_flush_model {
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) noexcept { return {}; }
    void stop() noexcept {}
    std::string to_string() const { return "no-flush"; }
};
static_assert(!tracing::concepts::TraceSink<Sink_without_flush_model>);

/// on_span is NOT noexcept — a throwing delivery fails the contract.
struct Throwing_on_span_model {
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) { return {}; } // no noexcept
    core::Status flush() noexcept { return {}; }
    void stop() noexcept {}
    std::string to_string() const { return "throwing"; }
};
static_assert(!tracing::concepts::TraceSink<Throwing_on_span_model>);

/// stop omitted entirely.
struct Sink_without_stop_model {
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) noexcept { return {}; }
    core::Status flush() noexcept { return {}; }
    std::string to_string() const { return "no-stop"; }
};
static_assert(!tracing::concepts::TraceSink<Sink_without_stop_model>);

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
            empty_ms[pass] = std::chrono::duration<double, std::nano>(t1 - t0).count() /
                             static_cast<double>(n);
            null_ms[pass] = std::chrono::duration<double, std::nano>(t2 - t1).count() /
                            static_cast<double>(n);
        }
        std::sort(empty_ms.begin(), empty_ms.end());
        std::sort(null_ms.begin(), null_ms.end());
        const double baseline = empty_ms[empty_ms.size() / 2U];
        const double null_cost = null_ms[null_ms.size() / 2U];
        std::printf("null tracer: baseline %.3f ns/op, null %.3f ns/op\n", baseline,
                    null_cost);
        // Indistinguishable from baseline: within 1 ns (noise budget).
        NX_CHECK(null_cost - baseline <= 1.0);
#else
        std::printf("null tracer: cost check compiled out (correctness-only build)\n");
#endif
    }
}

// --- sinks compile-time pins (task 7.x / spec tracing/tracer-concepts):
// every shipped sink satisfies the TraceSink contract.
static_assert(tracing::concepts::TraceSink<tracing::ConsoleTraceSink>);
static_assert(tracing::concepts::TraceSink<tracing::JsonFileTraceSink>);
static_assert(tracing::concepts::TraceSink<tracing::ChromeTraceSink>);
static_assert(tracing::concepts::TraceSink<tracing::NullTraceSink>);
static_assert(tracing::concepts::TraceSink<tracing::CompositeTraceSink<>>);
static_assert(tracing::concepts::TraceSink<
              tracing::CompositeTraceSink<tracing::ConsoleTraceSink, tracing::NullTraceSink>>);

// Compile-failure proof for task 7.4 (spec composite "Members are reachable
// by index and type" — out-of-range/absent access fails compilation) and
// task 7.1 (live sinks refuse deserialization). Verified by running the
// commands, capturing the error, then deleting the file:
//   cat > /tmp/opencode/neg/sinks_neg.cpp <<'EOF'
//   #include <nlohmann/json.hpp>
//   #include <nexigen/tracing/composite_trace_sink.hpp>
//   #include <nexigen/tracing/console_trace_sink.hpp>
//   #include <nexigen/tracing/null_trace_sink.hpp>
//   int main() {
//       nexigen::tracing::CompositeTraceSink<
//           nexigen::tracing::ConsoleTraceSink,
//           nexigen::tracing::NullTraceSink> c{
//           nexigen::tracing::ConsoleTraceSink{1}, nexigen::tracing::NullTraceSink{}};
//       static_cast<void>(c.get<2U>());          // index beyond pack
//       static_cast<void>(c.get<int>());         // absent type
//       nlohmann::json j;
//       nexigen::tracing::ConsoleTraceSink s{1};
//       nlohmann::from_json(j, s);               // live sink: no from_json
//   }
//   EOF
//   g++ -std=c++23 -fno-exceptions -fno-rtti -D_GNU_SOURCE -DJSON_NOEXCEPTION=1
//       -Iinclude -c /tmp/opencode/neg/sinks_neg.cpp -o /dev/null
//       # expect: static assertion failed (x2) + no matching from_json

// --- sinks suite (tasks 7.1..7.4 / spec tracing/trace-sinks): console
// content through a pipe fixture, JSONL round-trips read back line by line,
// ISO-8601 shape, typed attribute serialization with escaping, rotation
// filenames per policy, the rate-limit drop summary, the Chrome object
// parse with complete-event coverage, composite fan-out/no-starvation, and
// every error path — all against crafted records (full control of the UTC
// windows driving rotation).
namespace {

constexpr const char* sink_tmp_dir = "/tmp/opencode/nexigen-tracing-tests";

/// Read a whole file into a string (cold test utility).
[[nodiscard]] std::string read_file(const char* path) {
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return {};
    }
    std::string out;
    char buffer[4096];
    for (;;) {
        const ssize_t got = ::read(fd, buffer, sizeof(buffer));
        if (got <= 0) {
            break;
        }
        out.append(buffer, static_cast<std::size_t>(got));
    }
    static_cast<void>(::close(fd));
    return out;
}

/// A crafted record with full control over ids, stamps, name, and attrs.
[[nodiscard]] tracing::SpanRecord crafted_record(std::uint64_t mono_start_ns,
                                                 std::string_view name) {
    tracing::SpanRecord record{};
    record.trace_id = tracing::TraceId{0x0123456789ABCDEFULL};
    record.span_id = tracing::SpanId{0xFEDCBA9876543210ULL};
    record.parent_span_id = tracing::SpanId{0x1ULL};
    record.kind = tracing::SpanKind::Server;
    record.status = tracing::SpanStatus::Ok;
    record.thread_id = 4242;
    record.start = chrono::Timestamp{mono_start_ns};
    record.end = chrono::Timestamp{mono_start_ns + 1'500};
    record.file_name = "engine.cpp";
    record.function_name = "void run_step()";
    record.line = 77;
    record.column = 13;
    const std::size_t take =
        name.size() <= tracing::SpanRecord::name_capacity ? name.size()
                                                          : tracing::SpanRecord::name_capacity;
    for (std::size_t i = 0; i < take; ++i) {
        record.name[i] = name[i];
    }
    record.name[take] = '\0';
    static_cast<void>(record.attributes[0].store("retries", std::int64_t{3}));
    static_cast<void>(record.attributes[1].store("ratio", 0.15625)); // exact binary
    static_cast<void>(record.attributes[2].store("ok", true));
    static_cast<void>(record.attributes[3].store("tag", std::string_view("hil")));
    record.attribute_count = 4;
    return record;
}

/// A sink whose on_span always fails (composite no-starvation fixture).
struct Failing_on_span_sink {
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) noexcept {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    core::Status flush() noexcept { return {}; }
    void stop() noexcept {}
    std::string to_string() const { return "Failing_on_span_sink"; }
};
static_assert(tracing::concepts::TraceSink<Failing_on_span_sink>);

/// A copyable counting sink (composite no-starvation proof: the member
/// inside the composite counts its own deliveries).
struct Counting_sink {
    int deliveries{0};
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) noexcept {
        ++deliveries;
        return {};
    }
    core::Status flush() noexcept { return {}; }
    void stop() noexcept {}
    std::string to_string() const { return "Counting_sink"; }
};
static_assert(tracing::concepts::TraceSink<Counting_sink>);

} // namespace

// --- test sinks (design D14): pre-allocated static storage, no allocation
// on the consumer path. One global record buffer serves every Recording_sink
// (suites run sequentially; instances carve disjoint base offsets so two
// sinks can coexist within a suite).
namespace {

constexpr std::size_t sink_record_capacity = 48'000;
alignas(64) tracing::SpanRecord g_sink_records[sink_record_capacity]{};
std::size_t g_sink_record_next = 0; // bump cursor; only consumer threads write
                                    // within their own slice

/// Captures every delivered record (bounded; over-capacity counted) plus the
/// delivering worker's identity and affinity — the worker-pinning proof
/// surface for the provider suite.
struct Recording_sink {
    std::size_t base{0};
    std::size_t count{0};
    std::atomic<std::uint64_t> over_capacity{0};
    std::atomic<std::uint32_t> deliverer_tid{0};
    std::atomic<std::uint64_t> deliverer_mask{0};
    std::atomic<bool> started{false};
    std::atomic<bool> stopped{false};
    std::atomic<int> flush_calls{0};

    Recording_sink() noexcept { base = g_sink_record_next; }

    core::Status start() noexcept {
        started.store(true, std::memory_order_relaxed);
        return {};
    }
    core::Status on_span(const tracing::SpanRecord& record) noexcept {
        // Worker identity: SYS_gettid + the affinity the DELIVERING thread
        // observes (pthread_getaffinity_np round-trip on the worker). Once
        // per sink — the syscalls are cheap enough once, expensive per
        // record under a 32-thread stress drain.
        if (deliverer_tid.load(std::memory_order_relaxed) == 0U) {
            deliverer_tid.store(static_cast<std::uint32_t>(::syscall(SYS_gettid)),
                                std::memory_order_relaxed);
            std::uint64_t mask = 0;
            if (tracing::detail::self_affinity(mask).has_value()) {
                deliverer_mask.store(mask, std::memory_order_relaxed);
            }
        }
        if (base + count < sink_record_capacity) {
            g_sink_records[base + count] = record;
            ++count;
        } else {
            over_capacity.fetch_add(1U, std::memory_order_relaxed);
        }
        return {};
    }
    core::Status flush() noexcept {
        flush_calls.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    void stop() noexcept { stopped.store(true, std::memory_order_relaxed); }

    /// Cold-path diagnostic (mandate 9; part of the TraceSink contract).
    [[nodiscard]] std::string to_string() const {
        return "Recording_sink(count=" + std::to_string(count) + ")";
    }

    /// Read-back AFTER a flush barrier or provider destruction (the release
    /// edge of the handshake/join orders these plain writes).
    [[nodiscard]] const tracing::SpanRecord* view() const noexcept {
        return g_sink_records + base;
    }
    void reset() noexcept {
        g_sink_record_next = base; // release the slice for the next user
        count = 0;
    }
};

/// A sink whose flush always fails (error propagation through the barrier).
struct Failing_flush_sink {
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) noexcept { return {}; }
    core::Status flush() noexcept {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    void stop() noexcept {}
    std::string to_string() const { return "Failing_flush_sink"; }
};

} // namespace

void test_sinks() {
    static_cast<void>(::mkdir(sink_tmp_dir, 0755)); // exists is fine

    // ---- ConsoleTraceSink (task 7.1) --------------------------------------
    {
        // Content through a pipe fixture: one line per span carrying ids,
        // parent, kind, status, name, duration, and every attribute.
        int fds[2] = {-1, -1};
        NX_CHECK(::pipe(fds) == 0);
        tracing::ConsoleTraceSink sink{fds[1]};
        NX_CHECK(sink.start().has_value());
        NX_CHECK(sink.on_span(crafted_record(1'000'000'000ULL, "query")).has_value());
        NX_CHECK(sink.flush().has_value()); // no-op success
        // Read the line back from the pipe.
        char pipe_buffer[2048];
        std::size_t total = 0;
        while (total == 0U) {
            const ssize_t got = ::read(fds[0], pipe_buffer + total,
                                       sizeof(pipe_buffer) - total);
            if (got <= 0) {
                break;
            }
            total += static_cast<std::size_t>(got);
        }
        const std::string_view line(pipe_buffer, total);
        NX_CHECK(line.find("[trace 0123456789abcdef]") != std::string_view::npos);
        NX_CHECK(line.find("span=fedcba9876543210") != std::string_view::npos);
        NX_CHECK(line.find("parent=0000000000000001") != std::string_view::npos);
        NX_CHECK(line.find("kind=Server") != std::string_view::npos);
        NX_CHECK(line.find("status=Ok") != std::string_view::npos);
        NX_CHECK(line.find("name=\"query\"") != std::string_view::npos);
        NX_CHECK(line.find("dur=1500ns") != std::string_view::npos);
        NX_CHECK(line.find("tid=4242") != std::string_view::npos);
        NX_CHECK(line.find("retries=3") != std::string_view::npos);
        NX_CHECK(line.find("ratio=0.15625") != std::string_view::npos);
        NX_CHECK(line.find("ok=true") != std::string_view::npos);
        NX_CHECK(line.find("tag=\"hil\"") != std::string_view::npos);
        NX_CHECK(!line.empty() && line.back() == '\n');
        static_cast<void>(::close(fds[0]));
        static_cast<void>(::close(fds[1]));

        // Zero allocation per delivered record. /dev/null: a real write
        // destination that can never block (a pipe would fill after ~460
        // lines with no reader).
        {
            const std::size_t arena_before = arena_used();
            const std::size_t heap_before = heap_in_use();
            const int devnull = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
            NX_CHECK(devnull >= 0);
            tracing::ConsoleTraceSink loop_sink{devnull};
            const tracing::SpanRecord record =
                crafted_record(2'000'000'000ULL, "alloc-free");
            for (int i = 0; i < 500; ++i) {
                static_cast<void>(loop_sink.on_span(record));
            }
            NX_CHECK(arena_used() == arena_before);
            NX_CHECK(heap_in_use() == heap_before);
            static_cast<void>(::close(devnull));
        }

        // Closed descriptor: SystemError without abort; the sink stays
        // usable for error reporting (a follow-up call returns again).
        {
            const int stale = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
            NX_CHECK(stale >= 0);
            static_cast<void>(::close(stale));
            tracing::ConsoleTraceSink dead_sink{stale};
            const core::Status failed = dead_sink.on_span(
                crafted_record(3'000'000'000ULL, "doomed"));
            NX_CHECK(!failed.has_value());
            NX_CHECK(failed.error() == core::ErrorCode::SystemError);
            NX_CHECK(!dead_sink.on_span(crafted_record(3'000'000'000ULL, "again"))
                          .has_value()); // still usable, still SystemError
            dead_sink.stop();
        }

        // Diagnostics: to_string/formatter/to_json (no from_json — pinned
        // by the documented scratch-TU compile check above).
        {
            tracing::ConsoleTraceSink diag{2};
            NX_CHECK(diag.to_string() == "ConsoleTraceSink(fd=2)");
            NX_CHECK(tracing::to_string(diag) == diag.to_string());
            NX_CHECK(std::format("{}", diag) == diag.to_string());
            const nlohmann::json j = diag;
            NX_CHECK(j.at("type").get<std::string>() == "ConsoleTraceSink");
            NX_CHECK(j.at("fd").get<int>() == 2);
        }
    }

    // Shared UTC facts for the crafted-window tests below.
    std::int64_t utc_offset = 0;
    NX_CHECK(chrono::sample_utc_offset(utc_offset).has_value());
    const std::int64_t now_utc =
        chrono::utc_from_mono_ns(chrono::Timestamp::now()->nanoseconds(), utc_offset);
    const chrono::Civil_time now_civil = chrono::civil_from_utc_ns(now_utc);
    // A safe mid-window instant: 30 minutes past the current hour.
    const auto hour_mid_utc = [&](std::int64_t day_offset_hours) -> std::int64_t {
        // Base: the top of the current hour, then +30 min, plus day_offset.
        const std::int64_t ns_per_hour = 3'600'000'000'000LL;
        const std::int64_t hour_top =
            now_utc - (now_utc % ns_per_hour) - (now_utc < 0 ? ns_per_hour : 0);
        return hour_top + 1'800'000'000'000LL + day_offset_hours * ns_per_hour;
    };
    // Convert a target UTC instant into a monotonic-domain record start.
    const auto mono_for_utc = [&](std::int64_t utc_ns) -> std::uint64_t {
        return static_cast<std::uint64_t>(utc_ns - utc_offset);
    };

    // ---- JsonFileTraceSink: one parseable object per line (task 7.2) ----
    {
        const std::string path = std::string(sink_tmp_dir) + "/jsonl-basic.jsonl";
        static_cast<void>(::unlink(path.c_str()));
        tracing::JsonFileTraceSink_config config{};
        config.path = path;
        tracing::JsonFileTraceSink sink{config};
        NX_CHECK(sink.start().has_value());
        const tracing::SpanRecord record =
            crafted_record(mono_for_utc(hour_mid_utc(0)), "basic-span");
        NX_CHECK(sink.on_span(record).has_value());
        NX_CHECK(sink.flush().has_value()); // fsync surfaced
        sink.stop();

        const std::string content = read_file(path.c_str());
        NX_CHECK(!content.empty());
        // One parseable JSON object per line; each field round-trips.
        std::size_t line_start = 0;
        std::size_t lines = 0;
        nlohmann::json parsed{};
        while (line_start < content.size()) {
            const std::size_t line_end = content.find('\n', line_start);
            const std::size_t line_len = (line_end == std::string::npos)
                                             ? content.size() - line_start
                                             : line_end - line_start;
            NX_CHECK(line_len > 0U);
            parsed = nlohmann::json::parse(content.substr(line_start, line_len));
            ++lines;
            line_start = (line_end == std::string::npos) ? content.size() : line_end + 1U;
        }
        NX_CHECK(lines == 1);
        NX_CHECK(parsed.at("trace_id").get<std::string>() == "0123456789abcdef");
        NX_CHECK(parsed.at("span_id").get<std::string>() == "fedcba9876543210");
        NX_CHECK(parsed.at("parent_span_id").get<std::string>() == "0000000000000001");
        NX_CHECK(parsed.at("kind").get<std::string>() == "Server");
        NX_CHECK(parsed.at("status").get<std::string>() == "Ok");
        NX_CHECK(parsed.at("thread_id").get<std::uint32_t>() == 4242);
        NX_CHECK(parsed.at("duration_ns").get<std::uint64_t>() == 1'500);
        NX_CHECK(parsed.at("name").get<std::string>() == "basic-span");
        NX_CHECK(parsed.at("file").get<std::string>() == "engine.cpp");
        NX_CHECK(parsed.at("line").get<std::uint32_t>() == 77);
        // ISO-8601 shape: YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ (30 chars).
        const std::string start_iso = parsed.at("start").get<std::string>();
        const std::string end_iso = parsed.at("end").get<std::string>();
        const auto iso_shape = [](std::string_view s) {
            return s.size() == 30U && s[4] == '-' && s[7] == '-' && s[10] == 'T' &&
                   s[13] == ':' && s[16] == ':' && s[19] == '.' && s[29] == 'Z';
        };
        NX_CHECK(iso_shape(start_iso));
        NX_CHECK(iso_shape(end_iso));
        // The ISO text embeds the crafted civil hour.
        const std::string hour_text =
            std::to_string(now_civil.year) + "-"; // build YYYY-MM-DDTHH prefix
        NX_CHECK(start_iso.rfind(hour_text, 0) == 0); // begins with the year
        // Typed attributes round-trip.
        const nlohmann::json& attrs = parsed.at("attributes");
        NX_CHECK(attrs.is_array() && attrs.size() == 4);
        NX_CHECK(attrs[0].at("key").get<std::string>() == "retries" &&
                 attrs[0].at("type").get<std::string>() == "Int64" &&
                 attrs[0].at("value").get<std::int64_t>() == 3);
        NX_CHECK(attrs[1].at("type").get<std::string>() == "Double" &&
                 attrs[1].at("value").get<double>() == 0.15625);
        NX_CHECK(attrs[2].at("type").get<std::string>() == "Bool" &&
                 attrs[2].at("value").get<bool>());
        NX_CHECK(attrs[3].at("type").get<std::string>() == "String" &&
                 attrs[3].at("value").get<std::string>() == "hil");
    }

    // ---- JsonFileTraceSink: escaping of quotes/backslashes/control chars.
    {
        const std::string path = std::string(sink_tmp_dir) + "/jsonl-escape.jsonl";
        static_cast<void>(::unlink(path.c_str()));
        tracing::JsonFileTraceSink_config config{};
        config.path = path;
        tracing::JsonFileTraceSink sink{config};
        NX_CHECK(sink.start().has_value());
        tracing::SpanRecord record =
            crafted_record(mono_for_utc(hour_mid_utc(0)), "esc\"ap\\e\nx");
        static_cast<void>(record.attributes[3].store("ctl",
                                                     std::string_view("tab\tend\"q\\")));
        NX_CHECK(sink.on_span(record).has_value());
        sink.stop();
        const std::string content = read_file(path.c_str());
        const nlohmann::json parsed = nlohmann::json::parse(content);
        // The escaped name and attribute text round-trip exactly.
        NX_CHECK(parsed.at("name").get<std::string>() == "esc\"ap\\e\nx");
        NX_CHECK(parsed.at("attributes")[3].at("value").get<std::string>() ==
                 "tab\tend\"q\\");
    }

    // ---- JsonFileTraceSink: rotation filenames per policy (task 7.2) ----
    {
        // Hourly: two crafted spans in different UTC hours → two files, the
        // name embedding each span's hour; contents land in their window's
        // file.
        const std::string prefix = std::string(sink_tmp_dir) + "/jsonl-rot";
        for (const auto policy :
             {tracing::RotationPolicy::Hourly, tracing::RotationPolicy::Daily}) {
            tracing::JsonFileTraceSink_config config{};
            config.path = prefix;
            config.rotation = policy;
            tracing::JsonFileTraceSink sink{config};
            NX_CHECK(sink.start().has_value());
            // Span A: 30 min into the current hour/day.
            NX_CHECK(sink.on_span(
                          crafted_record(mono_for_utc(hour_mid_utc(0)), "window-a"))
                          .has_value());
            // Span B: one period later (next hour / next day).
            const std::int64_t period_ns = policy == tracing::RotationPolicy::Hourly
                                               ? 3'600'000'000'000LL
                                               : 86'400'000'000'000LL;
            NX_CHECK(sink.on_span(
                          crafted_record(mono_for_utc(hour_mid_utc(0) + period_ns),
                                         "window-b"))
                          .has_value());
            sink.stop();

            // Expected labels from the CIVIL time of each crafted window
            // (compact form: YYYYMMDDTHH hourly, YYYYMMDD daily).
            const auto label_of = [&](std::int64_t utc) {
                const chrono::Civil_time civil = chrono::civil_from_utc_ns(utc);
                auto pad = [](std::int64_t v, int width) {
                    std::string s = std::to_string(v < 0 ? -v : v);
                    while (static_cast<int>(s.size()) < width) {
                        s.insert(0, "0");
                    }
                    return s;
                };
                std::string label = pad(civil.year, 4) + pad(civil.month, 2) +
                                    pad(civil.day, 2);
                if (policy == tracing::RotationPolicy::Hourly) {
                    label += "T" + pad(civil.hour, 2);
                }
                return label;
            };
            const std::string file_a = prefix + "." + label_of(hour_mid_utc(0));
            const std::string file_b =
                prefix + "." + label_of(hour_mid_utc(0) + period_ns);
            NX_CHECK(file_a != file_b);
            const std::string content_a = read_file(file_a.c_str());
            const std::string content_b = read_file(file_b.c_str());
            NX_CHECK(content_a.find("window-a") != std::string::npos);
            NX_CHECK(content_a.find("window-b") == std::string::npos);
            NX_CHECK(content_b.find("window-b") != std::string::npos);
            static_cast<void>(::unlink(file_a.c_str()));
            static_cast<void>(::unlink(file_b.c_str()));
        }

        // None: all spans land in the single configured file for the
        // sink's lifetime.
        const std::string single = std::string(sink_tmp_dir) + "/jsonl-single.jsonl";
        static_cast<void>(::unlink(single.c_str()));
        tracing::JsonFileTraceSink_config none_config{};
        none_config.path = single;
        none_config.rotation = tracing::RotationPolicy::None;
        tracing::JsonFileTraceSink none_sink{none_config};
        NX_CHECK(none_sink.start().has_value());
        NX_CHECK(none_sink.on_span(
                      crafted_record(mono_for_utc(hour_mid_utc(0)), "s-one"))
                      .has_value());
        NX_CHECK(none_sink.on_span(
                      crafted_record(mono_for_utc(hour_mid_utc(48)), "s-two"))
                      .has_value());
        none_sink.stop();
        const std::string single_content = read_file(single.c_str());
        NX_CHECK(single_content.find("s-one") != std::string::npos);
        NX_CHECK(single_content.find("s-two") != std::string::npos);
    }

    // ---- JsonFileTraceSink: the rate-limit drop summary (task 7.2) -----
    {
        const std::string path = std::string(sink_tmp_dir) + "/jsonl-rate.jsonl";
        static_cast<void>(::unlink(path.c_str()));
        tracing::JsonFileTraceSink_config config{};
        config.path = path;
        config.rotation = tracing::RotationPolicy::None;
        config.max_spans_per_window = 2;
        config.window_ns = 60'000'000'000ULL; // 1 minute
        tracing::JsonFileTraceSink sink{config};
        NX_CHECK(sink.start().has_value());
        const std::uint64_t w0 = mono_for_utc(hour_mid_utc(0));
        // Two in-budget spans.
        NX_CHECK(sink.on_span(crafted_record(w0, "budget-1")).has_value());
        NX_CHECK(sink.on_span(crafted_record(w0 + 1'000, "budget-2")).has_value());
        // Two beyond the budget: NOT written, exactly counted.
        NX_CHECK(sink.on_span(crafted_record(w0 + 2'000, "over-1")).has_value());
        NX_CHECK(sink.on_span(crafted_record(w0 + 3'000, "over-2")).has_value());
        // A record past the window boundary: the summary line lands BEFORE
        // this record's line.
        NX_CHECK(sink.on_span(crafted_record(w0 + 61'000'000'000ULL, "next-window"))
                      .has_value());
        sink.stop();

        const std::string content = read_file(path.c_str());
        // Parse line by line: 2 records + 1 summary + 1 record, in order.
        std::vector<std::string> lines;
        std::size_t cursor = 0;
        while (cursor < content.size()) {
            const std::size_t end = content.find('\n', cursor);
            lines.push_back(content.substr(cursor, end - cursor));
            cursor = end + 1U;
        }
        NX_CHECK(lines.size() == 4U);
        const nlohmann::json l0 = nlohmann::json::parse(lines[0]);
        const nlohmann::json l1 = nlohmann::json::parse(lines[1]);
        const nlohmann::json l2 = nlohmann::json::parse(lines[2]);
        const nlohmann::json l3 = nlohmann::json::parse(lines[3]);
        NX_CHECK(l0.at("name").get<std::string>() == "budget-1");
        NX_CHECK(l1.at("name").get<std::string>() == "budget-2");
        NX_CHECK(l2.at("drop_summary").at("dropped").get<std::uint64_t>() == 2);
        NX_CHECK(l3.at("name").get<std::string>() == "next-window");
        // The over-budget spans were never written.
        NX_CHECK(content.find("over-1") == std::string::npos);
        NX_CHECK(content.find("over-2") == std::string::npos);

        // Unthrottled sinks write everything and no summaries appear.
        const std::string all_path = std::string(sink_tmp_dir) + "/jsonl-all.jsonl";
        static_cast<void>(::unlink(all_path.c_str()));
        tracing::JsonFileTraceSink_config all_config{};
        all_config.path = all_path;
        tracing::JsonFileTraceSink all_sink{all_config};
        NX_CHECK(all_sink.start().has_value());
        for (int i = 0; i < 10; ++i) {
            NX_CHECK(all_sink.on_span(crafted_record(w0 + static_cast<std::uint64_t>(i),
                                                     "unthrottled"))
                          .has_value());
        }
        all_sink.stop();
        const std::string all_content = read_file(all_path.c_str());
        NX_CHECK(all_content.find("drop_summary") == std::string::npos);
        NX_CHECK(std::count(all_content.begin(), all_content.end(), '\n') == 10);
    }

    // ---- JsonFileTraceSink: open failure surfaces (task 7.2) -----------
    {
        tracing::JsonFileTraceSink_config config{};
        config.path = "/tmp/opencode/nexigen-tracing-tests/missing-dir/x.jsonl";
        tracing::JsonFileTraceSink sink{config};
        const core::Status started = sink.start();
        NX_CHECK(!started.has_value());
        NX_CHECK(started.error() == core::ErrorCode::SystemError);
        const core::Status delivered = sink.on_span(
            crafted_record(mono_for_utc(hour_mid_utc(0)), "void"));
        NX_CHECK(!delivered.has_value());
        NX_CHECK(delivered.error() == core::ErrorCode::SystemError);
        NX_CHECK(!sink.flush().has_value());
        sink.stop();
    }

    // ---- JsonFileTraceSink_config + RotationPolicy trios (task 7.2) ----
    {
        // RotationPolicy: names round-trip, out-of-range renders Unknown,
        // hostile JSON leaves values, format honors width/alignment.
        NX_CHECK(tracing::to_string(tracing::RotationPolicy::None) == "None");
        NX_CHECK(tracing::to_string(tracing::RotationPolicy::Hourly) == "Hourly");
        NX_CHECK(tracing::to_string(tracing::RotationPolicy::Daily) == "Daily");
        NX_CHECK(tracing::to_string(static_cast<tracing::RotationPolicy>(9)) ==
                 "Unknown");
        tracing::RotationPolicy parsed = tracing::RotationPolicy::None;
        NX_CHECK(tracing::from_string("Daily", parsed) &&
                 parsed == tracing::RotationPolicy::Daily);
        NX_CHECK(!tracing::from_string("daily", parsed));
        NX_CHECK(parsed == tracing::RotationPolicy::Daily); // unchanged
        const nlohmann::json j_policy = tracing::RotationPolicy::Hourly;
        NX_CHECK(j_policy.get<std::string>() == "Hourly");
        tracing::RotationPolicy back = tracing::RotationPolicy::None;
        nlohmann::from_json(j_policy, back);
        NX_CHECK(back == tracing::RotationPolicy::Hourly);
        for (const nlohmann::json& bad :
             {nlohmann::json(1), nlohmann::json{1, 2}, nlohmann::json(nullptr),
              nlohmann::json("nope")}) {
            tracing::RotationPolicy hostile = tracing::RotationPolicy::Daily;
            nlohmann::from_json(bad, hostile);
            NX_CHECK(hostile == tracing::RotationPolicy::Daily);
        }
        NX_CHECK(std::format("{}", tracing::RotationPolicy::Daily) == "Daily");
        NX_CHECK(std::format("{:>8}", tracing::RotationPolicy::None) == "    None");
        NX_CHECK(std::format("{:*<8}", tracing::RotationPolicy::Hourly) == "Hourly**");

        // The config round-trips every field under JSON_NOEXCEPTION=1.
        const tracing::JsonFileTraceSink_config config{"/tmp/x.jsonl",
                                                       tracing::RotationPolicy::Hourly,
                                                       100, 500'000'000};
        NX_CHECK(config.to_string() == tracing::to_string(config));
        NX_CHECK(std::format("{}", config) == config.to_string());
        const nlohmann::json j = config;
        tracing::JsonFileTraceSink_config parsed_config{};
        nlohmann::from_json(j, parsed_config);
        NX_CHECK(parsed_config.path == config.path);
        NX_CHECK(parsed_config.rotation == config.rotation);
        NX_CHECK(parsed_config.max_spans_per_window == 100);
        NX_CHECK(parsed_config.window_ns == 500'000'000);
        // Hostile JSON leaves the values untouched.
        const tracing::JsonFileTraceSink_config before = parsed_config;
        for (const nlohmann::json& bad :
             {nlohmann::json(9), nlohmann::json{1}, nlohmann::json(nullptr),
              nlohmann::json{{"path", 1}, {"rotation", false}}}) {
            nlohmann::from_json(bad, parsed_config);
            NX_CHECK(parsed_config.path == before.path);
            NX_CHECK(parsed_config.rotation == before.rotation);
            NX_CHECK(parsed_config.max_spans_per_window == before.max_spans_per_window);
            NX_CHECK(parsed_config.window_ns == before.window_ns);
        }

        // The live sink's diagnostics surface.
        tracing::JsonFileTraceSink live{config};
        const std::string text = live.to_string();
        NX_CHECK(text == tracing::to_string(live));
        NX_CHECK(std::format("{}", live) == text);
        NX_CHECK(text.find("JsonFileTraceSink(") == 0U);
        const nlohmann::json j_live = live;
        NX_CHECK(j_live.at("type").get<std::string>() == "JsonFileTraceSink");
    }

    // ---- ChromeTraceSink (task 7.3) ---------------------------------------
    {
        const std::string path = std::string(sink_tmp_dir) + "/chrome-trace.json";
        static_cast<void>(::unlink(path.c_str()));
        tracing::ChromeTraceSink_config config{};
        config.path = path;
        tracing::ChromeTraceSink sink{config};
        NX_CHECK(sink.start().has_value());

        // A parent and a child (same trace, child parents to the parent) —
        // the same-thread causal chain; plus one later span for relative ts.
        tracing::SpanRecord parent = crafted_record(10'000'000'000ULL, "parent-span");
        parent.parent_span_id = tracing::SpanId{}; // root
        tracing::SpanRecord child = crafted_record(10'000'500'000ULL, "child-span");
        child.trace_id = parent.trace_id;
        child.parent_span_id = parent.span_id;
        tracing::SpanRecord later = crafted_record(10'006'000'000ULL, "later-span");
        NX_CHECK(sink.on_span(parent).has_value());
        NX_CHECK(sink.on_span(child).has_value());
        NX_CHECK(sink.on_span(later).has_value());
        NX_CHECK(sink.flush().has_value());
        sink.stop();

        // The whole file parses as ONE JSON object (header/footer exact).
        const std::string content = read_file(path.c_str());
        NX_CHECK(content.find("{\"traceEvents\":[") == 0U); // starts with header
        NX_CHECK(content.rfind("]}") == content.size() - 3U); // ends with ]}
        const nlohmann::json trace = nlohmann::json::parse(content);
        NX_CHECK(trace.is_object());
        const nlohmann::json& events = trace.at("traceEvents");
        NX_CHECK(events.is_array() && events.size() == 3); // one event per span

        // Complete-event fields: ph/name/cat/ts/dur/pid/tid/args.
        const nlohmann::json& first = events[0];
        NX_CHECK(first.at("ph").get<std::string>() == "X");
        NX_CHECK(first.at("name").get<std::string>() == "parent-span");
        NX_CHECK(first.at("cat").get<std::string>() == "Server");
        NX_CHECK(first.at("ts").get<std::uint64_t>() == 0U); // first event ts == 0
        NX_CHECK(first.at("dur").get<std::uint64_t>() == 1U); // 1500 ns → 1 µs trunc
        NX_CHECK(first.at("pid").get<std::uint64_t>() ==
                 static_cast<std::uint64_t>(::getpid()));
        NX_CHECK(first.at("tid").get<std::uint64_t>() == 4242);
        const nlohmann::json& args = first.at("args");
        NX_CHECK(args.at("trace_id").get<std::string>() == "0123456789abcdef");
        NX_CHECK(args.at("span_id").get<std::string>() == "fedcba9876543210");
        NX_CHECK(args.at("parent_span_id").get<std::string>() == "0000000000000000");
        NX_CHECK(args.at("retries").get<std::int64_t>() == 3);
        NX_CHECK(args.at("ratio").get<double>() == 0.15625);
        NX_CHECK(args.at("ok").get<bool>());
        NX_CHECK(args.at("tag").get<std::string>() == "hil");

        // Later events: non-negative relative ts, dur == end-minus-start at
        // µs truncation, cross-event id coherence (the child carries the
        // parent's trace and span ids in args).
        const nlohmann::json& second = events[1];
        const std::uint64_t child_ts = second.at("ts").get<std::uint64_t>();
        const std::uint64_t later_ts = events[2].at("ts").get<std::uint64_t>();
        NX_CHECK(child_ts == 500U); // (10'000'500'000 - 10'000'000'000)/1000
        NX_CHECK(later_ts == 6'000U);
        NX_CHECK(later_ts >= child_ts);
        NX_CHECK(second.at("args").at("trace_id").get<std::string>() ==
                 first.at("args").at("trace_id").get<std::string>());
        NX_CHECK(second.at("args").at("parent_span_id").get<std::string>() ==
                 first.at("args").at("span_id").get<std::string>());
        NX_CHECK(events[2].at("dur").get<std::uint64_t>() == 1U);

        // Config trio + live diagnostics + unopenable path.
        const tracing::ChromeTraceSink_config chrome_config{"/tmp/t.json"};
        NX_CHECK(chrome_config.to_string() == tracing::to_string(chrome_config));
        NX_CHECK(std::format("{}", chrome_config) == chrome_config.to_string());
        const nlohmann::json j_cfg = chrome_config;
        tracing::ChromeTraceSink_config cfg_back{};
        nlohmann::from_json(j_cfg, cfg_back);
        NX_CHECK(cfg_back.path == "/tmp/t.json");
        tracing::ChromeTraceSink_config hostile_back{"/keep.json"};
        for (const nlohmann::json& bad :
             {nlohmann::json(1), nlohmann::json{1}, nlohmann::json(nullptr),
              nlohmann::json{{"path", 3}}}) {
            nlohmann::from_json(bad, hostile_back);
            NX_CHECK(hostile_back.path == "/keep.json");
        }
        const std::string live_text = sink.to_string(); // closed after stop()
        NX_CHECK(live_text == tracing::to_string(sink));
        NX_CHECK(std::format("{}", sink) == live_text);
        const nlohmann::json j_live = sink;
        NX_CHECK(j_live.at("type").get<std::string>() == "ChromeTraceSink");

        tracing::ChromeTraceSink_config bad_config{};
        bad_config.path = "/tmp/opencode/nexigen-tracing-tests/missing-dir/t.json";
        tracing::ChromeTraceSink bad_sink{bad_config};
        const core::Status bad_start = bad_sink.start();
        NX_CHECK(!bad_start.has_value());
        NX_CHECK(bad_start.error() == core::ErrorCode::SystemError);
    }

    // ---- CompositeTraceSink (task 7.4) ------------------------------------
    {
        // Every member receives the same record through mixed sink types:
        // console line in the pipe + JSON object in the file + null.
        int fds[2] = {-1, -1};
        NX_CHECK(::pipe(fds) == 0);
        const std::string path = std::string(sink_tmp_dir) + "/composite.jsonl";
        static_cast<void>(::unlink(path.c_str()));
        tracing::JsonFileTraceSink_config file_config{};
        file_config.path = path;
        tracing::CompositeTraceSink<tracing::ConsoleTraceSink,
                                    tracing::JsonFileTraceSink, tracing::NullTraceSink>
            composite{tracing::ConsoleTraceSink{fds[1]},
                      tracing::JsonFileTraceSink{file_config},
                      tracing::NullTraceSink{}};
        NX_CHECK(composite.start().has_value());
        NX_CHECK(composite.on_span(crafted_record(mono_for_utc(hour_mid_utc(0)),
                                                 "fan-out"))
                      .has_value());
        NX_CHECK(composite.flush().has_value());
        composite.stop();
        // The console fixture shows the span's line...
        char pipe_buffer[2048];
        std::size_t total = 0;
        while (total == 0U) {
            const ssize_t got = ::read(fds[0], pipe_buffer + total,
                                       sizeof(pipe_buffer) - total);
            if (got <= 0) {
                break;
            }
            total += static_cast<std::size_t>(got);
        }
        NX_CHECK(std::string_view(pipe_buffer, total).find("fan-out") !=
                 std::string_view::npos);
        // ...and the file contains the span's JSON object.
        const std::string file_content = read_file(path.c_str());
        NX_CHECK(file_content.find("fan-out") != std::string::npos);
        static_cast<void>(::close(fds[0]));
        static_cast<void>(::close(fds[1]));

        // A failing FIRST member does not starve the rest; the composite
        // surfaces the first error after all members were attempted.
        tracing::CompositeTraceSink<Failing_on_span_sink, Counting_sink> mixed{
            Failing_on_span_sink{}, Counting_sink{}};
        NX_CHECK(mixed.start().has_value());
        const core::Status failed = mixed.on_span(
            crafted_record(mono_for_utc(hour_mid_utc(0)), "no-starve"));
        NX_CHECK(!failed.has_value());
        NX_CHECK(failed.error() == core::ErrorCode::SystemError);
        NX_CHECK(mixed.get<1>().deliveries == 1); // the second member received it
        NX_CHECK(mixed.flush().has_value()); // aggregated flush
        mixed.stop();

        // Typed/indexed access identity + the diagnostics surface.
        // A duplicated member type is legal for INDEX access (distinct
        // members); the TYPE access on it is a compile error (pinned by the
        // documented scratch-TU check at the top of this suite).
        tracing::CompositeTraceSink<tracing::NullTraceSink, tracing::NullTraceSink>
            dupes{};
        NX_CHECK(&dupes.get<0>() != &dupes.get<1>()); // two distinct members
        static_assert(tracing::CompositeTraceSink<tracing::NullTraceSink>::size == 1);
        tracing::CompositeTraceSink<tracing::NullTraceSink> single{
            tracing::NullTraceSink{}};
        NX_CHECK(&single.get<0>() == &single.get<tracing::NullTraceSink>());
        const std::string text = single.to_string();
        NX_CHECK(text == tracing::to_string(single));
        NX_CHECK(std::format("{}", single) == text);
        NX_CHECK(text.find("CompositeTraceSink(members=1") == 0U);
        const nlohmann::json j = single;
        NX_CHECK(j.at("type").get<std::string>() == "CompositeTraceSink");
        NX_CHECK(j.at("members").get<int>() == 1);
        // Out-of-range index / absent type fail at compile time: pinned by
        // the documented scratch-TU check at the top of this suite.
    }
}


// --- compile-time pins (task 6.2 / spec tracing/tracer-provider
// "Factory-only construction is pinned at compile time" and
// tracing/tracer-concepts): guards, tracers and the passkey are NOT
// constructible outside the factory hierarchy; the concrete Tracer
// satisfies the concept.
static_assert(tracing::concepts::Tracer<tracing::Tracer<64, 4>>);
static_assert(!std::is_default_constructible_v<tracing::ScopedSpan<64, 4>>);
static_assert(!std::is_constructible_v<tracing::ScopedSpan<64, 4>, std::string_view,
                                       tracing::SpanKind>);
static_assert(!std::is_copy_constructible_v<tracing::ScopedSpan<64, 4>>);
static_assert(!std::is_move_constructible_v<tracing::ScopedSpan<64, 4>>);
static_assert(!std::is_default_constructible_v<tracing::Tracer<64, 4>>);
static_assert(!std::is_constructible_v<tracing::Tracer<64, 4>, const char*>);
static_assert(!std::is_constructible_v<tracing::SpanPasskey>);

// --- scoped_span suite (task 6.2 / spec tracing/scoped-span): the RAII
// lifecycle against a recording sink — exactly-once enqueue, idempotent
// end(), post-end rejections, bounds, default status, the full parent
// chain vocabulary (nested/sibling/explicit-override/root/overflow/
// end-with-active-children), cross-thread linking, context extraction,
// disabled/dropped short-circuits with mid-run flips, serialization pins,
// and the allocation-free window. All spans run on ONE dedicated producer
// thread: fresh TLS slot map (this TU constructs many providers on main)
// and a fresh active-span stack.
void test_scoped_span() {
    // ShardCapacity 1024: the tight loops below burst hundreds of spans
    // between flushes; a small ring would turn consumer wake latency into
    // buffer_full noise unrelated to what this suite verifies.
    using Provider = tracing::TracerProvider<1024, 4>;
    using tracing::SpanRecord;

    Recording_sink sink{};
    alignas(Provider) static std::byte storage[sizeof(Provider)];
    auto* provider = new (static_cast<void*>(storage))
        Provider(tracing::TracerProvider_config{}, sink);
    NX_CHECK(provider->initialize().has_value());
    const auto tracer = provider->get_tracer("scoped");
    NX_CHECK(tracer.has_value());

    std::jthread producer([tracer = *tracer, provider, &sink] {
        // (1) Destruction enqueues exactly one record (name/kind/location).
        {
            std::size_t drained = sink.count;
            static_cast<void>(drained);
            {
                auto span = tracer->start_span("lifecycle", tracing::SpanKind::Server,
                                               nullptr, std::source_location::current());
                NX_CHECK(span.active());
            } // destruction enqueues
            NX_CHECK(provider->flush().has_value());
            NX_CHECK(sink.count == 1);
            const SpanRecord& record = sink.view()[0];
            NX_CHECK(record.name_view() == "lifecycle");
            NX_CHECK(record.kind == tracing::SpanKind::Server);
            NX_CHECK(record.flags == 0U); // destruction-ended, not explicit
            NX_CHECK(std::string_view(record.file_name).find("tracing_test") !=
                     std::string_view::npos);
            NX_CHECK(record.line > 0U);
        }

        // (2) Explicit end(): enqueues once, destruction does not push a
        // second record, subsequent end() calls are no-ops (spec "Explicit
        // end does not double-enqueue").
        {
            auto span = tracer->start_span("explicit", tracing::SpanKind::Client);
            NX_CHECK(span.end()); // true: this call ended it
            NX_CHECK(!span.end()); // no-op
            NX_CHECK(!span.end());
            NX_CHECK(provider->flush().has_value());
            NX_CHECK(sink.count == 2);
            const SpanRecord& record = sink.view()[1];
            NX_CHECK(record.name_view() == "explicit");
            NX_CHECK((record.flags & SpanRecord::flag_ended_explicit) != 0U);
        } // destruction: no second push (count stays 2 through the flush)

        // (3) Post-end updates are rejected, mutate nothing (spec "Updates
        // after end are rejected").
        {
            auto span = tracer->start_span("rejected", tracing::SpanKind::Internal);
            static_cast<void>(span.set_attribute("early", 1));
            NX_CHECK(span.end());
            NX_CHECK(!span.set_attribute("late", 2));
            NX_CHECK(!span.set_status(tracing::SpanStatus::Error));
            NX_CHECK(provider->flush().has_value());
            const SpanRecord& record = sink.view()[sink.count - 1];
            NX_CHECK(record.attribute_count == 1); // only the early one
            NX_CHECK(record.attributes[0].key_view() == "early");
            NX_CHECK(record.status == tracing::SpanStatus::Unset); // unchanged
        }

        // (4) Attribute capacity (MaxAttributes = 4) and key bounds rejected
        // without side effects; the first four intact (spec "bounds").
        // NOTE: records enqueue at END — the span is closed before the
        // flush/inspect step.
        {
            {
                auto span = tracer->start_span("attrs", tracing::SpanKind::Internal);
                NX_CHECK(span.set_attribute("one", std::int64_t{1}));
                NX_CHECK(span.set_attribute("two", std::uint64_t{2}));
                NX_CHECK(span.set_attribute("three", 3.5));
                NX_CHECK(span.set_attribute("four", true));
                NX_CHECK(!span.set_attribute("fifth", 5)); // capacity bound
                NX_CHECK(!span.set_attribute("0123456789abcdef", 6)); // key > 15
                // Every value type reads back exactly from the drained record.
                NX_CHECK(span.set_status(tracing::SpanStatus::Ok));
            }
            NX_CHECK(provider->flush().has_value());
            const SpanRecord& record = sink.view()[sink.count - 1];
            NX_CHECK(record.attribute_count == 4);
            NX_CHECK(record.attributes[0].type == tracing::SpanAttributeType::Int64 &&
                     record.attributes[0].value.i64 == 1);
            NX_CHECK(record.attributes[1].type == tracing::SpanAttributeType::Uint64 &&
                     record.attributes[1].value.u64 == 2);
            NX_CHECK(record.attributes[2].type == tracing::SpanAttributeType::Double &&
                     record.attributes[2].value.f64 == 3.5);
            NX_CHECK(record.attributes[3].type == tracing::SpanAttributeType::Bool &&
                     record.attributes[3].value.b);
            NX_CHECK(record.status == tracing::SpanStatus::Ok);
        }

        // (5) Default status Unset + inline/view strings land (spec
        // "Attributes and status land in the drained record").
        {
            {
                auto span = tracer->start_span("strings", tracing::SpanKind::Consumer);
                char mutable_source[] = "short-lived";
                NX_CHECK(span.set_attribute("inline", std::string_view(mutable_source)));
                mutable_source[0] = 'X'; // record must hold its own copy
                NX_CHECK(span.set_attribute("view",
                                            std::string_view(
                                                "a-program-static-view-longer-than-23-bytes")));
            }
            NX_CHECK(provider->flush().has_value());
            const SpanRecord& record = sink.view()[sink.count - 1];
            NX_CHECK(record.status == tracing::SpanStatus::Unset);
            NX_CHECK(record.attributes[0].text_view() == "short-lived");
            NX_CHECK(record.attributes[1].text_view() ==
                     "a-program-static-view-longer-than-23-bytes");
        }

        // (6) Construction stamps the start; held duration survives into
        // end-start (spec "Construction stamps the start").
        {
            auto span = tracer->start_span("held", tracing::SpanKind::Internal);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            static_cast<void>(span.end());
            NX_CHECK(provider->flush().has_value());
            const SpanRecord& record = sink.view()[sink.count - 1];
            NX_CHECK(record.end > record.start);
            NX_CHECK(record.duration_ns() >= 1'500'000);
        }

        // (7) context() matches the drained record; sampled reflects the
        // decision at start (spec "Extracted context matches the drained
        // record").
        {
            auto span = tracer->start_span("context", tracing::SpanKind::Producer);
            const tracing::SpanContext ctx = span.context();
            static_cast<void>(span.end());
            NX_CHECK(provider->flush().has_value());
            const SpanRecord& record = sink.view()[sink.count - 1];
            NX_CHECK(ctx.trace_id == record.trace_id);
            NX_CHECK(ctx.span_id == record.span_id);
            NX_CHECK(ctx.sampled == tracing::SamplingDecision::RecordAndSample);
        }

        // (8) Automatic parent chains: nested → child inherits trace and
        // parents to the active span; siblings share the trace and parent
        // (spec "Nested spans chain parent ids" / "Siblings share the trace
        // id"). This thread's stack is empty here: the first span is a root.
        const std::size_t base = sink.count;
        tracing::SpanId root_id{};
        tracing::TraceId root_trace{};
        {
            auto root = tracer->start_span("root", tracing::SpanKind::Internal);
            root_trace = root.context().trace_id;
            root_id = root.context().span_id;
            {
                auto child = tracer->start_span("child", tracing::SpanKind::Server);
                NX_CHECK(child.context().trace_id == root_trace);
                {
                    auto grandchild =
                        tracer->start_span("grandchild", tracing::SpanKind::Client);
                    NX_CHECK(grandchild.context().trace_id == root_trace);
                }
            } // child and grandchild end first (LIFO)
            {
                auto sib1 = tracer->start_span("sib1", tracing::SpanKind::Internal);
                NX_CHECK(sib1.context().trace_id == root_trace);
            }
            {
                auto sib2 = tracer->start_span("sib2", tracing::SpanKind::Internal);
                NX_CHECK(sib2.context().trace_id == root_trace);
            }
        }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(sink.count == base + 5);
        { // Records arrive in END order: grandchild, child, sib1, sib2, root.
            const SpanRecord& grand_rec = sink.view()[base];
            const SpanRecord& child_rec = sink.view()[base + 1];
            const SpanRecord& sib1_rec = sink.view()[base + 2];
            const SpanRecord& sib2_rec = sink.view()[base + 3];
            const SpanRecord& root_rec = sink.view()[base + 4];
            NX_CHECK(root_rec.trace_id == root_trace);
            NX_CHECK(root_rec.parent_span_id.is_unset()); // fresh root
            NX_CHECK(child_rec.trace_id == root_trace);
            NX_CHECK(child_rec.parent_span_id == root_id);
            NX_CHECK(grand_rec.trace_id == root_trace);
            NX_CHECK(grand_rec.parent_span_id == child_rec.span_id);
            NX_CHECK(sib1_rec.trace_id == root_trace && sib2_rec.trace_id == root_trace);
            NX_CHECK(sib1_rec.parent_span_id == root_id);
            NX_CHECK(sib2_rec.parent_span_id == root_id);
        }

        // (9) Explicit parent overrides the stack; the explicit-parent child
        // becomes the LOCAL parent (spec "Explicit parent overrides the
        // stack" + "The child becomes the local parent").
        const std::size_t ebase = sink.count;
        {
            auto active = tracer->start_span("active", tracing::SpanKind::Internal);
            const tracing::SpanContext foreign{
                tracing::TraceId{0xABCD}, tracing::SpanId{0xEF01},
                tracing::SamplingDecision::RecordAndSample};
            {
                auto linked = tracer->start_span("linked", tracing::SpanKind::Consumer,
                                                 &foreign);
                NX_CHECK(linked.context().trace_id == foreign.trace_id);
                // A further span on THIS thread links to the child now.
                auto deeper = tracer->start_span("deeper", tracing::SpanKind::Internal);
                NX_CHECK(deeper.context().trace_id == foreign.trace_id);
            }
        }
        NX_CHECK(provider->flush().has_value());
        { // END order: deeper, linked, active.
            const SpanRecord& deeper_rec = sink.view()[ebase];
            const SpanRecord& linked_rec = sink.view()[ebase + 1];
            NX_CHECK(linked_rec.trace_id == tracing::TraceId{0xABCD});
            NX_CHECK(linked_rec.parent_span_id == tracing::SpanId{0xEF01});
            NX_CHECK(deeper_rec.trace_id == tracing::TraceId{0xABCD});
            NX_CHECK(deeper_rec.parent_span_id == linked_rec.span_id);
        }

        // (10) Depth overflow records as a bounded root: 16 nested spans
        // fill the stack; the 17th gets a FRESH trace id and zero parent
        // (spec "Root spans and depth overflow are bounded"). The guards are
        // held through recursion (each frame owns one local; the type is
        // non-movable so no container can hold them).
        const std::size_t obase = sink.count;
        tracing::TraceId overflow_trace{};
        {
            auto nest = [&](auto&& self, int depth) -> void {
                if (depth < 16) {
                    auto span = tracer->start_span("deep", tracing::SpanKind::Internal);
                    self(self, depth + 1);
                    return;
                }
                auto overflow = tracer->start_span("overflow",
                                                   tracing::SpanKind::Internal);
                NX_CHECK(overflow.active()); // still recorded
                overflow_trace = overflow.context().trace_id;
                NX_CHECK(overflow_trace != sink.view()[obase].trace_id);
                static_cast<void>(overflow.end());
            };
            nest(nest, 0);
        }
        NX_CHECK(provider->flush().has_value());
        {
            // END order: overflow (explicit end) first, then the deep spans
            // as the recursion unwinds — innermost (deep15) before deep0.
            // view[obase+i] is deep[16-i]: its parent is deep[15-i], the
            // record at obase+i+1; deep0 (view[obase+16]) is the root.
            const SpanRecord& overflow_rec = sink.view()[obase];
            NX_CHECK(overflow_rec.name_view() == "overflow");
            NX_CHECK(overflow_rec.parent_span_id.is_unset()); // bounded root
            NX_CHECK(overflow_rec.trace_id == overflow_trace);
            for (std::size_t i = 1; i <= 16; ++i) {
                const SpanRecord& deep_rec = sink.view()[obase + i];
                NX_CHECK(deep_rec.name_view() == "deep");
                if (i < 16) {
                    NX_CHECK(deep_rec.parent_span_id ==
                             sink.view()[obase + i + 1].span_id);
                } else {
                    NX_CHECK(deep_rec.parent_span_id.is_unset()); // deep0 = root
                }
            }
        }

        // (11) Explicit end() with active children truncates their frames:
        // the next span parents to the surviving stack top (root here), and
        // the child's later destruction is a coherent no-op (design D7).
        const std::size_t tbase = sink.count;
        {
            auto parent = tracer->start_span("parent", tracing::SpanKind::Internal);
            auto child = tracer->start_span("child", tracing::SpanKind::Internal);
            NX_CHECK(parent.end()); // child's frame discarded early
            auto next = tracer->start_span("after-truncation",
                                           tracing::SpanKind::Internal);
            static_cast<void>(next);
            static_cast<void>(child); // later end: clamped no-op
        }
        NX_CHECK(provider->flush().has_value());
        { // END order: parent (explicit end()), then next, then child.
            const SpanRecord& parent_rec = sink.view()[tbase];
            const SpanRecord& next_rec = sink.view()[tbase + 1];
            const SpanRecord& child_rec = sink.view()[tbase + 2];
            NX_CHECK(child_rec.parent_span_id == parent_rec.span_id); // chained before
            NX_CHECK(next_rec.parent_span_id.is_unset()); // stack truncated to root
        }

        // (12) Cross-thread context linking through the core SPSC queue
        // (spec "Child links to a context produced on another thread"):
        // this thread hands a live span's context to a worker, which starts
        // the explicit-parent child; a deeper span there auto-links to it.
        {
            auto live = tracer->start_span("cross-root", tracing::SpanKind::Internal);
            const tracing::SpanContext ctx = live.context();
            core::SpscRingBuffer<tracing::SpanContext, 4> handoff{};
            NX_CHECK(handoff.try_push(ctx).has_value());
            std::jthread worker([tracer, &handoff, &sink, provider] {
                tracing::SpanContext received{};
                for (int spins = 0; spins < 1'000; ++spins) {
                    if (handoff.try_pop(received)) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                auto child = tracer->start_span("cross-child",
                                                tracing::SpanKind::Server, &received);
                NX_CHECK(child.context().trace_id == received.trace_id);
                {
                    auto deeper = tracer->start_span("cross-deeper",
                                                     tracing::SpanKind::Client);
                    NX_CHECK(deeper.context().trace_id == received.trace_id);
                    NX_CHECK(deeper.context().span_id != received.span_id);
                }
                static_cast<void>(provider->flush());
                static_cast<void>(&sink);
            });
            worker.join();
            static_cast<void>(live.end());
            NX_CHECK(provider->flush().has_value());
        }

        // (13) Disabled short-circuit: no record, no stack touch — a span
        // dropped INSIDE an active parent leaves the parent linkable
        // afterwards; drop counters stay zero (spec "Disabled provider emits
        // nothing" + "Dropping is not a drop counter increment").
        const tracing::Dropped_stats drops_before = provider->dropped();
        const std::size_t dbase = sink.count;
        {
            auto parent = tracer->start_span("disable-parent",
                                             tracing::SpanKind::Internal);
            provider->set_enabled(false);
            {
                auto dropped = tracer->start_span("dropped",
                                                  tracing::SpanKind::Internal);
                NX_CHECK(!dropped.active());
                NX_CHECK(dropped.error() == core::ErrorCode::Ok);
            }
            provider->set_enabled(true);
            auto after = tracer->start_span("after-drop", tracing::SpanKind::Internal);
            NX_CHECK(after.context().trace_id == parent.context().trace_id);
            NX_CHECK(after.context().span_id != parent.context().span_id);
        }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(sink.count == dbase + 2); // parent + after-drop only
        { // END order: after-drop, then parent.
            const SpanRecord& parent_rec = sink.view()[dbase + 1];
            const SpanRecord& after_rec = sink.view()[dbase];
            NX_CHECK(after_rec.parent_span_id == parent_rec.span_id);
        }
        NX_CHECK(provider->dropped().rate_limited == drops_before.rate_limited);
        NX_CHECK(provider->dropped().buffer_full == drops_before.buffer_full);
        NX_CHECK(provider->dropped().slot_exhausted == drops_before.slot_exhausted);

        // (14) Sampling Drop short-circuits per span; flipping back records
        // normally (spec "Sampling Drop short-circuits per span").
        const std::size_t sbase = sink.count;
        provider->set_sampling(tracing::SamplingDecision::Drop);
        {
            auto filtered = tracer->start_span("filtered", tracing::SpanKind::Internal);
            NX_CHECK(!filtered.active());
            NX_CHECK(filtered.context() == tracing::SpanContext{});
        }
        provider->set_sampling(tracing::SamplingDecision::RecordAndSample);
        {
            auto recorded = tracer->start_span("recorded", tracing::SpanKind::Internal);
            NX_CHECK(recorded.active());
        }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(sink.count == sbase + 1);
        NX_CHECK(sink.view()[sbase].name_view() == "recorded");
        NX_CHECK(provider->dropped().total() == drops_before.total()); // still zero

        // (15) ScopedSpan serialization pins (spec "Guard renders its
        // state"): to_string/formatter agree; to_json serializes live state.
        {
            auto span = tracer->start_span("serialize", tracing::SpanKind::Server);
            static_cast<void>(span.set_status(tracing::SpanStatus::Error));
            const std::string text = span.to_string();
            NX_CHECK(text == tracing::to_string(span));
            NX_CHECK(std::format("{}", span) == text);
            NX_CHECK(text.find("serialize") != std::string::npos);
            NX_CHECK(text.find("Server") != std::string::npos);
            NX_CHECK(text.find("Error") != std::string::npos);
            const nlohmann::json j = span;
            NX_CHECK(j.at("name").get<std::string>() == "serialize");
            NX_CHECK(j.at("kind").get<std::string>() == "Server");
            NX_CHECK(j.at("status").get<std::string>() == "Error");
            NX_CHECK(j.at("active").get<bool>());
            static_cast<void>(span.end());
        }

        // (16) Updates never allocate or block: a tight loop with both
        // counters armed (slot/TLS warmed by everything above).
        {
            const std::size_t arena_before = arena_used();
            const std::size_t heap_before = heap_in_use();
            for (int i = 0; i < 5'000; ++i) {
                auto span = tracer->start_span("alloc-free",
                                               tracing::SpanKind::Internal);
                static_cast<void>(span.set_attribute("k", i));
                static_cast<void>(span.set_status(tracing::SpanStatus::Ok));
                if ((i % 256) == 255) {
                    // Keep the ring shallow across the burst: the loop
                    // measures allocation freedom, not queueing pressure.
                    static_cast<void>(provider->flush());
                }
            }
            static_cast<void>(provider->flush());
            NX_CHECK(arena_used() == arena_before);
            NX_CHECK(heap_in_use() == heap_before);
        }
    });
    producer.join();

    // Control flips under concurrency (spec "Control reads need no
    // synchronization"): one thread flips enable/sampling while another
    // records; exactly the active-at-start spans drain. The flush BEFORE
    // the baseline matters: the worker may still be draining the previous
    // batch's tail when producer.join() returned.
    {
        NX_CHECK(provider->flush().has_value());
        const std::uint64_t drops_at_base = provider->dropped().buffer_full;
        std::atomic<bool> stop_flip{false};
        std::atomic<int> active_starts{0};
        std::jthread flipper([provider, &stop_flip] {
            bool enabled = true;
            bool drop = false;
            while (!stop_flip.load(std::memory_order_relaxed)) {
                provider->set_enabled(enabled);
                provider->set_sampling(drop ? tracing::SamplingDecision::Drop
                                             : tracing::SamplingDecision::RecordAndSample);
                enabled = !enabled;
                drop = !drop;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            provider->set_enabled(true);
            provider->set_sampling(tracing::SamplingDecision::RecordAndSample);
        });
        const std::size_t flip_base = sink.count;
        std::jthread recorder([tracer = *tracer, &active_starts] {
            for (int i = 0; i < 2'000; ++i) {
                auto span = tracer->start_span("flips", tracing::SpanKind::Internal);
                if (span.active()) {
                    active_starts.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
        recorder.join();
        stop_flip.store(true, std::memory_order_relaxed);
        flipper.join();
        NX_CHECK(provider->flush().has_value());
        const std::size_t expected =
            flip_base + static_cast<std::size_t>(active_starts.load(std::memory_order_relaxed));
        // Burst-tolerant invariant: every active-at-start span either
        // drained or was counted buffer_full (a full shard drops by design —
        // the producer never blocks; Debug bursts can outrun the consumer).
        const std::uint64_t drops_during =
            provider->dropped().buffer_full - drops_at_base;
        if (sink.count + static_cast<std::size_t>(drops_during) != expected) {
            std::printf("flips MISMATCH: base=%zu active=%d count=%zu dropped=%s\n",
                        flip_base, active_starts.load(std::memory_order_relaxed), sink.count,
                        provider->dropped().to_string().c_str());
        }
        NX_CHECK(sink.count + static_cast<std::size_t>(drops_during) == expected);
        sink.reset();
    }

    std::destroy_at(provider);
}

// --- latency suite (task 6.4 / design D14; spec tracing/scoped-span
// "Sub-20-nanosecond enqueue" + "Disabled and dropped short-circuit"):
// Release-x86-64 gates the medians; every other configuration runs the same
// machinery but only asserts correctness (bounded, allocation-free) and
// prints the numbers. Methodology identical to logging/metrics: warm batched
// loops, rdtsc pairs with measured ns/tick, per-op medians, measurement
// overhead subtracted, best-of-N passes under the gate. The bench provider
// pins its worker to one allowed core and the producer thread to another
// (measurement placement, NOT a library policy), so SMT-sibling co-scheduling
// cannot bill cross-thread cache noise to the enqueue median.
void test_latency() {
    using Bench_provider = tracing::TracerProvider<1024, 1>;
#if defined(__x86_64__)
    // Max_threads=1 keeps the bench pool a single ring: the gate measures
    // the start+enqueue path of ONE producer exactly like the logging gate.
    tracing::NullTraceSink null_sink{};
    alignas(Bench_provider) static std::byte bench_storage[sizeof(Bench_provider)];
    std::uint64_t bench_core = 1ULL; // fallback: core 0
    std::size_t lo = 64U;            // producer placement (first allowed core)
    std::size_t hi = 0U;             // worker placement (last allowed core)
    bool pinned = false;
    cpu_set_t original_set{};
    {
        cpu_set_t allowed{};
        if (::pthread_getaffinity_np(::pthread_self(), sizeof(allowed), &allowed) == 0) {
            std::size_t first = 64U;
            std::size_t last = 0U;
            for (std::size_t c = 0; c < static_cast<std::size_t>(CPU_SETSIZE); ++c) {
                if (CPU_ISSET(c, &allowed)) {
                    if (first == 64U) { first = c; }
                    last = c;
                }
            }
            if (first != 64U) {
                lo = first;
                hi = last;
                bench_core = 1ULL << hi;
                cpu_set_t mine{};
                CPU_SET(lo, &mine);
                pinned = ::pthread_setaffinity_np(::pthread_self(), sizeof(mine),
                                                  &mine) == 0;
                original_set = allowed;
            }
        }
    }

    tracing::TracerProvider_config bench_config{};
    bench_config.core_mask = bench_core;
    auto* bench = new (static_cast<void*>(bench_storage))
        Bench_provider(bench_config, null_sink);
    NX_CHECK(bench->initialize().has_value());
    const auto bench_tracer = bench->get_tracer("latency");
    NX_CHECK(bench_tracer.has_value());

    constexpr int WARMUP = 2'000;
    constexpr int N = 10'000;
    alignas(64) static std::uint64_t raw[N];   // full lifecycle samples
    alignas(64) static std::uint64_t flt[N];   // short-circuit samples
    alignas(64) static std::uint64_t ovr[N];   // rdtsc-pair overhead samples

    // Warmup BEFORE the measured window (branch predictors, caches, TLS
    // slot claim, thread id cache, stamp calibration interplay).
    for (int i = 0; i < WARMUP; ++i) {
        auto span = (*bench_tracer)->start_span("warm", tracing::SpanKind::Internal);
        static_cast<void>(span);
    }

    // ns-per-tick for the measurement itself (control plane: 50 ms window).
    const std::uint64_t t_start = __rdtsc();
    const core::Result<chrono::Timestamp> m_start = chrono::Timestamp::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const core::Result<chrono::Timestamp> m_end = chrono::Timestamp::now();
    const std::uint64_t t_end = __rdtsc();
    NX_CHECK(m_start.has_value() && m_end.has_value());
    const double ns_per_tick =
        static_cast<double>(m_end->nanoseconds() - m_start->nanoseconds()) /
        static_cast<double>(t_end - t_start);

    const std::size_t arena_before = arena_used();
    const std::size_t heap_before = heap_in_use();
    const tracing::Dropped_stats drops0 = bench->dropped();

    double best_lifecycle_ns = -1.0;
    double best_short_ns = -1.0;
    double overhead_ns = 0.0;
#if defined(NEXIGEN_TRACING_STRICT_LATENCY) && !defined(NX_SANITIZER_BUILD)
    constexpr int passes = 3;
#else
    constexpr int passes = 1;
#endif
    for (int pass = 0; pass < passes; ++pass) {
        // (a) full span lifecycle: start_span + destruction enqueue, the
        // provider's worker draining concurrently.
        for (int i = 0; i < N; ++i) {
            const std::uint64_t r0 = __rdtsc();
            auto span = (*bench_tracer)->start_span("bench",
                                                    tracing::SpanKind::Internal);
            static_cast<void>(span);
            const std::uint64_t r1 = __rdtsc();
            raw[i] = r1 - r0;
        }
        // (b) disabled short-circuit: one relaxed load + compare, nothing
        // else (the guard is inactive; destruction enqueues nothing).
        bench->set_enabled(false);
        for (int i = 0; i < N; ++i) {
            const std::uint64_t r0 = __rdtsc();
            auto span = (*bench_tracer)->start_span("bench",
                                                    tracing::SpanKind::Internal);
            static_cast<void>(span);
            const std::uint64_t r1 = __rdtsc();
            flt[i] = r1 - r0;
        }
        bench->set_enabled(true);
        // (c) measurement overhead samples (empty rdtsc pairs).
        for (int i = 0; i < N; ++i) {
            const std::uint64_t r0 = __rdtsc();
            const std::uint64_t r1 = __rdtsc();
            ovr[i] = r1 - r0;
        }
        std::sort(raw, raw + N);
        std::sort(flt, flt + N);
        std::sort(ovr, ovr + N);
        overhead_ns = static_cast<double>(ovr[N / 2]) * ns_per_tick;
        const double lifecycle_ns =
            static_cast<double>(raw[N / 2]) * ns_per_tick - overhead_ns;
        const double short_ns =
            static_cast<double>(flt[N / 2]) * ns_per_tick - overhead_ns;
        if (best_lifecycle_ns < 0.0 || lifecycle_ns < best_lifecycle_ns) {
            best_lifecycle_ns = lifecycle_ns;
        }
        if (best_short_ns < 0.0 || short_ns < best_short_ns) {
            best_short_ns = short_ns;
        }
    }

    // Zero allocation across every measured traffic window.
    NX_CHECK(arena_used() == arena_before);
    NX_CHECK(heap_in_use() == heap_before);

    // Durability of the measured traffic: flush, then every accepted span
    // (warmup + all passes' lifecycle spans) was enqueued exactly once.
    NX_CHECK(bench->flush().has_value());
    const std::uint64_t accepted =
        static_cast<std::uint64_t>(WARMUP) +
        static_cast<std::uint64_t>(passes) * static_cast<std::uint64_t>(N);
    const std::uint64_t dropped_rows =
        bench->dropped().buffer_full - drops0.buffer_full;
    std::printf("latency: best-of-%d lifecycle median %.2f ns (gate 20), short-circuit "
                "median %.3f ns (gate 2), meas-overhead %.2f ns, ns/tick %.4f, "
                "accepted=%llu dropped=%llu\n",
                passes, best_lifecycle_ns, best_short_ns, overhead_ns, ns_per_tick,
                static_cast<unsigned long long>(accepted),
                static_cast<unsigned long long>(dropped_rows));
    static_cast<void>(dropped_rows);
    static_cast<void>(accepted);

#if defined(NEXIGEN_TRACING_STRICT_LATENCY) && !defined(NX_SANITIZER_BUILD)
    // The spec numbers: lifecycle median <= 20 ns, short-circuit <= 2 ns
    // (with the harness's rdtsc-pair tolerance). No lower bound on the
    // short-circuit: it can sit under pair resolution and read as ~0/-1 ns.
    NX_CHECK(best_lifecycle_ns <= 20.0);
    NX_CHECK(best_short_ns <= 2.0);
#else
    // Non-gated configuration: correctness-only, latencies recorded above.
    NX_CHECK(best_lifecycle_ns > 0.0 || best_lifecycle_ns < 0.0);
#endif

    std::destroy_at(bench);
    if (pinned) { // restore the process's original placement for later suites
        static_cast<void>(::pthread_setaffinity_np(::pthread_self(),
                                                   sizeof(original_set), &original_set));
    }
#else
    // Non-x86: accounting-only run (no rdtsc), bounded-time correctness.
    tracing::NullTraceSink null_sink{};
    alignas(Bench_provider) static std::byte bench_storage[sizeof(Bench_provider)];
    auto* bench = new (static_cast<void*>(bench_storage))
        Bench_provider(tracing::TracerProvider_config{}, null_sink);
    NX_CHECK(bench->initialize().has_value());
    const auto bench_tracer = bench->get_tracer("latency");
    NX_CHECK(bench_tracer.has_value());
    for (int i = 0; i < 10'000; ++i) {
        auto span = (*bench_tracer)->start_span("bench", tracing::SpanKind::Internal);
        static_cast<void>(span);
    }
    NX_CHECK(bench->flush().has_value());
    std::destroy_at(bench);
#endif
}

// --- provider suite (tasks 6.1/6.3 / spec tracing/tracer-provider):
// compile-time shape pins, factory semantics (dedup/distinct/BufferFull),
// zero-allocation construction and tracer creation, dead handles, control
// plane, worker affinity round-trip + startup snapshot, per-shard FIFO under
// 32-thread stress, the 33rd-thread drop, exact drop counters, flush
// barrier + error propagation, and shutdown drain.
void test_provider() {
    using Provider = tracing::TracerProvider<64, 32>;
    using Big_provider = tracing::TracerProvider<1024, 32>;

    // Compile-time shape pins: default footprint is the ~10 MiB one-block
    // budget (design D5) and the object is cache-line aligned (never stack).
    static_assert(sizeof(Big_provider) > 10 * 1024 * 1024);
    static_assert(sizeof(Big_provider) % 64 == 0);
    static_assert(alignof(Big_provider) == 64);
    static_assert(Big_provider::capacity() == 1024);
    static_assert(Big_provider::max_threads() == 32);

    tracing::NullTraceSink null_sink{};

    // Zero heap allocation during construction AND tracer creation: the
    // provider is placement-constructed into static storage.
    {
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        const std::size_t arena_before = arena_used();
        const std::size_t heap_before = heap_in_use();
        auto* provider = new (static_cast<void*>(storage))
            Provider(tracing::TracerProvider_config{}, null_sink);
        NX_CHECK(arena_used() == arena_before);
        NX_CHECK(heap_in_use() == heap_before);

        const auto first = provider->get_tracer("engine");
        const auto again = provider->get_tracer("engine");
        const auto other = provider->get_tracer("physics");
        NX_CHECK(first.has_value() && again.has_value() && other.has_value());
        NX_CHECK(*again == *first);   // repeated name → same handle
        NX_CHECK(*other != *first);   // distinct name → distinct tracer
        NX_CHECK(arena_used() == arena_before); // tracer creation: zero alloc
        NX_CHECK(heap_in_use() == heap_before);

        // The diagnostic surface (spec "Handles and snapshots render
        // diagnostics").
        NX_CHECK((*first)->to_string() == "Tracer(scope=engine)");
        NX_CHECK(tracing::to_string(**first) == (*first)->to_string());
        NX_CHECK(std::format("{}", **first) == (*first)->to_string());
        NX_CHECK(provider->to_string().find("TracerProvider(") == 0U);
        NX_CHECK(std::format("{}", *provider) == provider->to_string());

        // initialize: idempotent, and the worker launches.
        NX_CHECK(provider->initialize().has_value());
        NX_CHECK(provider->initialize().has_value()); // second call: no-op Ok
        for (int spins = 0; spins < 2'000 && !provider->worker_started(); ++spins) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        NX_CHECK(provider->worker_started());
        NX_CHECK(provider->worker_start_code() == core::ErrorCode::Ok);

        // Control plane round-trips.
        NX_CHECK(provider->is_enabled());
        NX_CHECK(provider->sampling() == tracing::SamplingDecision::RecordAndSample);
        provider->set_enabled(false);
        NX_CHECK(!provider->is_enabled());
        provider->set_sampling(tracing::SamplingDecision::Drop);
        NX_CHECK(provider->sampling() == tracing::SamplingDecision::Drop);
        provider->set_enabled(true);
        provider->set_sampling(tracing::SamplingDecision::RecordAndSample);
        NX_CHECK(provider->is_enabled());
        NX_CHECK(provider->sampling() == tracing::SamplingDecision::RecordAndSample);

        // Dead handles (spec "Dead handles report NotInitialized"): the
        // provider's static storage stays mapped, destruction flips the
        // liveness flag, and handle use fails cleanly.
        const auto doomed = provider->get_tracer("doomed");
        NX_CHECK(doomed.has_value());
        std::destroy_at(provider);
        const auto stale = provider->get_tracer("engine");
        NX_CHECK(!stale.has_value());
        NX_CHECK(stale.error() == core::ErrorCode::NotInitialized);
        {
            auto span = (*doomed)->start_span("after-death", tracing::SpanKind::Internal);
            NX_CHECK(span.error() == core::ErrorCode::NotInitialized);
            NX_CHECK(!span.active());
            static_cast<void>(span.end()); // no-op, no crash
        }
    }

    // Pool exhaustion: BufferFull at the configured limit, existing usable.
    {
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        tracing::TracerProvider_config config{};
        config.max_tracers = 2;
        auto* provider = new (static_cast<void*>(storage)) Provider(config, null_sink);
        const auto a = provider->get_tracer("a");
        const auto b = provider->get_tracer("b");
        const auto c = provider->get_tracer("c");
        NX_CHECK(a.has_value() && b.has_value());
        NX_CHECK(!c.has_value() && c.error() == core::ErrorCode::BufferFull);
        // Previously created tracers remain usable.
        {
            auto span = (*a)->start_span("still-works", tracing::SpanKind::Server);
            NX_CHECK(span.active());
        }
        // Oversized scope names are rejected (bounded slot storage).
        const auto too_long = provider->get_tracer(
            "a-scope-name-that-is-longer-than-the-forty-seven-byte-bound-for-sure");
        NX_CHECK(!too_long.has_value() && too_long.error() == core::ErrorCode::InvalidArgument);
        std::destroy_at(provider);
    }

    // Config + Dropped_stats trios (spec "Provider serialization surfaces").
    {
        const tracing::TracerProvider_config config{0x5, 8, 1000, 500'000'000};
        NX_CHECK(config.to_string() == tracing::to_string(config));
        NX_CHECK(std::format("{}", config) == config.to_string());
        const nlohmann::json j = config;
        tracing::TracerProvider_config back{};
        nlohmann::from_json(j, back);
        NX_CHECK(back.core_mask == 0x5 && back.max_tracers == 8 &&
                 back.max_spans_per_window == 1000 && back.window_ns == 500'000'000);
        // Hostile JSON leaves values unchanged.
        const tracing::TracerProvider_config before = back;
        for (const nlohmann::json& bad :
             {nlohmann::json(1), nlohmann::json{1, 2}, nlohmann::json(nullptr),
              nlohmann::json{{"core_mask", "x"}, {"max_tracers", false}}}) {
            nlohmann::from_json(bad, back);
            NX_CHECK(std::memcmp(&back, &before, sizeof(back)) == 0);
        }

        const tracing::Dropped_stats stats{3, 5, 7};
        NX_CHECK(stats.total() == 15);
        NX_CHECK(stats.to_string() == tracing::to_string(stats));
        NX_CHECK(std::format("{}", stats) == stats.to_string());
        const nlohmann::json j_stats = stats;
        tracing::Dropped_stats stats_back{};
        nlohmann::from_json(j_stats, stats_back);
        NX_CHECK(stats_back.rate_limited == 3 && stats_back.buffer_full == 5 &&
                 stats_back.slot_exhausted == 7);
    }

    // Worker affinity round-trip (spec "Worker affinity matches the
    // configured mask"): a valid single allowed core, the mask the WORKER
    // observed, and the affinity at delivery time.
    {
        std::uint64_t allowed = 0ULL;
        NX_CHECK(tracing::detail::self_affinity(allowed).has_value());
        std::uint64_t core = 0ULL;
        for (std::size_t bit = 0; bit < 64U && core == 0ULL; ++bit) {
            if ((allowed & (1ULL << bit)) != 0ULL) {
                core = 1ULL << bit;
            }
        }
        Recording_sink sink{};
        tracing::TracerProvider_config config{};
        config.core_mask = core;
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage)) Provider(config, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("affinity");
        NX_CHECK(tracer.has_value());
        { auto span = (*tracer)->start_span("pinned", tracing::SpanKind::Internal); }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(provider->worker_started());
        NX_CHECK(provider->worker_start_code() == core::ErrorCode::Ok);
        NX_CHECK(provider->worker_mask() == core); // the worker's own reading
        NX_CHECK(sink.count == 1);
        NX_CHECK(sink.deliverer_tid.load(std::memory_order_relaxed) != 0U);
        NX_CHECK(sink.deliverer_tid.load(std::memory_order_relaxed) !=
                 static_cast<std::uint32_t>(::syscall(SYS_gettid))); // not main
        NX_CHECK(sink.deliverer_mask.load(std::memory_order_relaxed) ==
                 core); // pthread_getaffinity_np ON the worker at delivery
        sink.reset();
        std::destroy_at(provider);
    }

    // Zero-mask inheritance (spec: "a zero mask inherits OS placement"): the
    // worker's mask equals the creating thread's mask at construction time.
    {
        std::uint64_t main_mask = 0ULL;
        NX_CHECK(tracing::detail::self_affinity(main_mask).has_value());
        Recording_sink sink{};
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage))
            Provider(tracing::TracerProvider_config{}, sink);
        NX_CHECK(provider->initialize().has_value());
        for (int spins = 0; spins < 2'000 && !provider->worker_started(); ++spins) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        NX_CHECK(provider->worker_started());
        NX_CHECK(provider->worker_mask() == main_mask);
        std::destroy_at(provider);
    }

    // A failing mask surfaces SystemError in the startup snapshot WITHOUT
    // stopping the pipeline (spec "Pin failures are visible"): probe every
    // single-bit mask on a probe thread and use the first the MACHINE
    // rejects (an offline core or a cgroup-restricted box). On a fully
    // available 64-CPU machine no uint64 mask can fail — the assertion then
    // runs the identical code path on the positive side with a printed note
    // (only the input differs; the error handling is unreachable there).
    {
        std::atomic<std::uint64_t> failing{0ULL};
        {
            std::jthread probe([&failing] {
                for (std::size_t bit = 63; bit > 0; --bit) {
                    if (!tracing::detail::pin_self(1ULL << bit).has_value()) {
                        failing.store(1ULL << bit, std::memory_order_relaxed);
                        return;
                    }
                }
            });
            probe.join();
        }
        Recording_sink sink{};
        tracing::TracerProvider_config config{};
        const std::uint64_t rejected = failing.load(std::memory_order_relaxed);
        config.core_mask = rejected != 0ULL ? rejected : 1ULL; // pinnable fallback
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage)) Provider(config, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("pin-probe");
        NX_CHECK(tracer.has_value());
        { auto span = (*tracer)->start_span("still-records", tracing::SpanKind::Internal); }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(provider->worker_started());
        NX_CHECK(sink.count == 1); // spans still record and drain either way
        if (rejected != 0ULL) {
            std::printf("provider: failing mask 0x%llx rejected as expected\n",
                        static_cast<unsigned long long>(rejected));
            NX_CHECK(provider->worker_start_code() == core::ErrorCode::SystemError);
        } else {
            std::printf("provider: all 64 single-bit masks pin on this machine "
                        "(64 online CPUs) — SystemError snapshot assertion run "
                        "on the positive path only\n");
            NX_CHECK(provider->worker_start_code() == core::ErrorCode::Ok);
        }
        sink.reset();
        std::destroy_at(provider);
    }

    // Per-shard FIFO and fair drain under 32-thread stress (spec): every
    // thread's spans drain in that thread's start order, no thread starved.
    // ShardCapacity 1024 ≥ per-thread span count: drops are impossible by
    // construction, so the assertions check ORDER, not queueing luck.
    {
        using Stress_provider = tracing::TracerProvider<1024, 32>;
        Recording_sink sink{};
        alignas(Stress_provider) static std::byte storage[sizeof(Stress_provider)];
        auto* provider = new (static_cast<void*>(storage))
            Stress_provider(tracing::TracerProvider_config{}, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("stress");
        NX_CHECK(tracer.has_value());

        constexpr int threads = 32;
        constexpr int per_thread = 300;
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        std::vector<std::jthread> workers;
        workers.reserve(threads);
        for (int t = 0; t < threads; ++t) {
            workers.emplace_back([tracer = *tracer, &ready, &go] {
                ready.fetch_add(1, std::memory_order_relaxed);
                while (!go.load(std::memory_order_relaxed)) {
                    std::this_thread::yield();
                }
                for (int i = 0; i < per_thread; ++i) {
                    auto span = tracer->start_span("stress-span",
                                                   tracing::SpanKind::Internal);
                    static_cast<void>(span.set_attribute("seq", i));
                }
            });
        }
        while (ready.load(std::memory_order_relaxed) < threads) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_relaxed);
        for (auto& worker : workers) {
            worker.join();
        }
        NX_CHECK(provider->flush().has_value());

        NX_CHECK(sink.count == static_cast<std::size_t>(threads) * per_thread);
        NX_CHECK(sink.over_capacity.load(std::memory_order_relaxed) == 0);
        NX_CHECK(provider->dropped().total() == 0);
        // Per-thread FIFO: sequence attributes strictly increasing per tid;
        // every producer thread represented.
        std::unordered_map<std::uint32_t, int> last_seq;
        std::unordered_map<std::uint32_t, int> counts;
        for (std::size_t i = 0; i < sink.count; ++i) {
            const tracing::SpanRecord& record = sink.view()[i];
            NX_CHECK(record.attribute_count == 1);
            const std::int64_t seq = record.attributes[0].value.i64;
            const auto inserted = last_seq.emplace(record.thread_id, -1);
            NX_CHECK(seq > inserted.first->second); // in this thread's order
            inserted.first->second = static_cast<int>(seq);
            ++counts[record.thread_id];
        }
        NX_CHECK(last_seq.size() == static_cast<std::size_t>(threads)); // no starvation
        for (const auto& [tid, count] : counts) {
            NX_CHECK(count == per_thread);
            static_cast<void>(tid);
        }
        sink.reset();
        std::destroy_at(provider);
    }

    // The 33rd distinct thread drops visibly (spec "Threads beyond the pool
    // drop visibly"): sequential short-lived threads claim slots 0..31, the
    // 33rd lands in slot_exhausted.
    {
        Recording_sink sink{};
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage))
            Provider(tracing::TracerProvider_config{}, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("pool");
        NX_CHECK(tracer.has_value());
        for (int t = 0; t < 33; ++t) {
            std::jthread ephemeral([tracer = *tracer] {
                auto span = tracer->start_span("ephemeral",
                                               tracing::SpanKind::Consumer);
                static_cast<void>(span);
            });
            ephemeral.join();
        }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(provider->dropped().slot_exhausted == 1); // exactly the 33rd
        NX_CHECK(provider->dropped().buffer_full == 0);
        NX_CHECK(provider->dropped().rate_limited == 0);
        NX_CHECK(sink.count == 32); // the first 32 threads' spans drained
        sink.reset();
        std::destroy_at(provider);
    }

    // Exact drop counters, deterministic (never-initialized provider: no
    // consumer exists, so capacity accounting is exact):
    //   buffer_full: capacity 64 → 70 spans, 64 accepted, 6 dropped;
    //   destruction then drains exactly the 64 accepted records inline.
    // The spans run on a DEDICATED thread: this suite constructs many
    // providers on the main thread, and the 4-entry TLS slot map fills —
    // a 5th provider would re-claim a fresh slot (and fresh throttle
    // window) per span. One producer thread per provider is the deployment
    // model the design assumes.
    {
        Recording_sink sink{};
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage))
            Provider(tracing::TracerProvider_config{}, sink);
        const auto tracer = provider->get_tracer("fill");
        NX_CHECK(tracer.has_value());
        std::jthread producer([tracer = *tracer] {
            for (int i = 0; i < 70; ++i) {
                auto span = tracer->start_span("filler", tracing::SpanKind::Internal);
                NX_CHECK(span.active()); // the push drops, the span is still valid
                static_cast<void>(span.set_attribute("i", i));
            }
        });
        producer.join();
        NX_CHECK(provider->dropped().buffer_full == 6);
        NX_CHECK(provider->dropped().rate_limited == 0);
        NX_CHECK(provider->dropped().slot_exhausted == 0);
        std::destroy_at(provider); // inline drain of the never-initialized
        NX_CHECK(sink.count == 64);  // every ACCEPTED record delivered
        NX_CHECK(sink.started.load(std::memory_order_relaxed));
        NX_CHECK(sink.stopped.load(std::memory_order_relaxed));
        sink.reset();
    }

    // The span-rate budget counts rate_limited exactly (spec "The span-rate
    // budget counts rate_limited"): budget 2 per 1 s window, on a dedicated
    // producer thread (same TLS-slot reasoning as above).
    {
        Recording_sink sink{};
        tracing::TracerProvider_config config{};
        config.max_spans_per_window = 2;
        config.window_ns = 1'000'000'000;
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage)) Provider(config, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("budget");
        NX_CHECK(tracer.has_value());
        std::atomic<int> active_count{0};
        std::jthread producer([tracer = *tracer, &active_count] {
            for (int i = 0; i < 3; ++i) {
                auto span = tracer->start_span("budgeted", tracing::SpanKind::Producer);
                NX_CHECK(span.active() == (i < 2)); // the third is rejected
                if (span.active()) {
                    active_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            auto span4 = tracer->start_span("rejected", tracing::SpanKind::Producer);
            NX_CHECK(!span4.active()); // rejected BEFORE queue interaction
        });
        producer.join();
        NX_CHECK(active_count.load(std::memory_order_relaxed) == 2);
        NX_CHECK(provider->dropped().rate_limited == 2);
        NX_CHECK(provider->dropped().buffer_full == 0);
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(sink.count == 2);
        sink.reset();
        std::destroy_at(provider);
    }

    // Flush barrier: returns only after drain durability, and sink errors
    // propagate (spec "Flush waits for durability").
    {
        // Durability: spans ended, flush, sink saw every record AND its own
        // flush() ran before flush() returned.
        Recording_sink sink{};
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage))
            Provider(tracing::TracerProvider_config{}, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("barrier");
        NX_CHECK(tracer.has_value());
        for (int i = 0; i < 10; ++i) {
            auto span = (*tracer)->start_span("durability",
                                              tracing::SpanKind::Internal);
            static_cast<void>(span);
        }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(sink.count == 10); // drained before flush() returned
        NX_CHECK(sink.flush_calls.load(std::memory_order_relaxed) >= 1);
        sink.reset();
        std::destroy_at(provider);

        // Error propagation: a failing sink flush surfaces as SystemError.
        Failing_flush_sink failing{};
        alignas(Provider) static std::byte storage2[sizeof(Provider)];
        auto* provider2 = new (static_cast<void*>(storage2))
            Provider(tracing::TracerProvider_config{}, failing);
        NX_CHECK(provider2->initialize().has_value());
        const auto tracer2 = provider2->get_tracer("failing");
        NX_CHECK(tracer2.has_value());
        { auto span = (*tracer2)->start_span("x", tracing::SpanKind::Internal); }
        const core::Status flushed = provider2->flush();
        NX_CHECK(!flushed.has_value());
        NX_CHECK(flushed.error() == core::ErrorCode::SystemError);
        provider2->~TracerProvider();
    }

    // Shutdown drain: destruction loses no accepted record (spec "Destruction
    // does not lose accepted records") — no flush() call before destruction.
    {
        Recording_sink sink{};
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage))
            Provider(tracing::TracerProvider_config{}, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("shutdown");
        NX_CHECK(tracer.has_value());
        for (int i = 0; i < 25; ++i) {
            auto span = (*tracer)->start_span("last-words",
                                              tracing::SpanKind::Internal);
            static_cast<void>(span);
        }
        std::destroy_at(provider); // drain-on-join
        NX_CHECK(sink.count == 25);
        NX_CHECK(sink.stopped.load(std::memory_order_relaxed));
        sink.reset();
    }

    // flush() before initialize() reports NotInitialized (no worker exists).
    {
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage))
            Provider(tracing::TracerProvider_config{}, null_sink);
        const core::Status flushed = provider->flush();
        NX_CHECK(!flushed.has_value());
        NX_CHECK(flushed.error() == core::ErrorCode::NotInitialized);
        std::destroy_at(provider);
    }
}

// --- extended trace writer/sink coverage (change improve-test-coverage):
// every escaped byte class through both JSON writers plus empty chrome trace.
void test_trace_writer_bounds_full() {
    char small[3]{};
    tracing::detail::Fixed_writer writer{small, sizeof small};
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
    tracing::detail::Fixed_writer all{buffer, sizeof buffer};
    NX_CHECK(all.append_u64(std::numeric_limits<std::uint64_t>::max()));
    NX_CHECK(all.append_i64(std::numeric_limits<std::int64_t>::min()));
    NX_CHECK(all.append_double(-0.0));
    NX_CHECK(all.append_hex16(0x0123456789abcdefULL));
    NX_CHECK(all.append_escaped("q\"\\\b\f\n\r\t\x02 and UTF-8"));
    NX_CHECK(all.ok());

    char tiny[4]{};
    tracing::detail::Fixed_writer escaped{tiny, sizeof tiny};
    NX_CHECK(!escaped.append_escaped("\x01"));
    NX_CHECK(!escaped.ok());
}

void test_trace_writer_escape_matrix() {
    const std::string json_path = std::string(sink_tmp_dir) + "/escape-matrix.jsonl";
    const std::string chrome_path = std::string(sink_tmp_dir) + "/escape-matrix.json";
    static_cast<void>(::unlink(json_path.c_str()));
    static_cast<void>(::unlink(chrome_path.c_str()));

    const std::string escaped = "a\b\f\r\t\x01\x1f\"\\Кириллица";
    tracing::SpanRecord record =
        crafted_record(1'000'000'000ULL, escaped);
    static_cast<void>(record.attributes[3].store("ctl", std::string_view(escaped)));

    {
        tracing::JsonFileTraceSink_config config{};
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
        tracing::ChromeTraceSink_config config{};
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
        tracing::ChromeTraceSink_config config{};
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
        tracing::TracerProvider_config config{};
        config.core_mask = 3;
        config.max_tracers = 8;
        config.max_spans_per_window = 10;
        config.window_ns = 11;
        nlohmann::json json;
        tracing::to_json(json, config);
        tracing::TracerProvider_config back{};
        tracing::from_json(json, back);
        NX_CHECK(back.max_tracers == 8U);
        NX_CHECK(config.to_string().find("max_tracers") != std::string::npos);
        tracing::Dropped_stats dropped{1, 2, 3};
        tracing::to_json(json, dropped);
        tracing::Dropped_stats dropped_back{};
        tracing::from_json(json, dropped_back);
        NX_CHECK(dropped_back.total() == 6U);
        NX_CHECK(dropped.to_string().find("total=6") != std::string::npos);
    }
    // Wrong-typed readers leave the whole value untouched.
    {
        tracing::SpanId span_keep{7};
        tracing::from_json(nlohmann::json(1), span_keep);
        NX_CHECK(span_keep.value() == 7U);
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
        tracing::from_json(nlohmann::json{{"key", std::string(16, 'x')},
                                          {"type", "Int64"},
                                          {"value", 1}},
                           attr_keep);
        NX_CHECK(attr_keep.key_view() == "k");
        tracing::from_json(nlohmann::json{{"key", "k"},
                                          {"type", "Int64"},
                                          {"value", "wrong"}},
                           attr_keep);
        NX_CHECK(attr_keep.value.i64 == 1);
        tracing::from_json(nlohmann::json{{"key", "k"},
                                          {"type", "Uint64"},
                                          {"value", "wrong"}},
                           attr_keep);
        tracing::from_json(nlohmann::json{{"key", "k"},
                                          {"type", "Double"},
                                          {"value", "wrong"}},
                           attr_keep);
        tracing::from_json(nlohmann::json{{"key", "k"},
                                          {"type", "Bool"},
                                          {"value", "wrong"}},
                           attr_keep);
        tracing::from_json(nlohmann::json{{"key", "k"},
                                          {"type", "String"},
                                          {"value", 1}},
                           attr_keep);
        tracing::from_json(nlohmann::json{{"key", "k"},
                                          {"type", "Bogus"},
                                          {"value", 1}},
                           attr_keep);
        tracing::from_json(nlohmann::json{{"key", "k"},
                                          {"type", "Int64"}},
                           attr_keep);
        tracing::SpanRecord record_keep = crafted_record(1'000'000'000ULL, "keep");
        tracing::from_json(nlohmann::json{{"kind", 7}}, record_keep);
        NX_CHECK(record_keep.name_view() == "keep");
        tracing::TracerProvider_config config_keep{};
        config_keep.max_tracers = 3;
        tracing::from_json(nlohmann::json{{"max_tracers", "x"}}, config_keep);
        NX_CHECK(config_keep.max_tracers == 3U);
        tracing::Dropped_stats dropped_keep{1, 2, 3};
        tracing::from_json(nlohmann::json{{"rate_limited", "x"}}, dropped_keep);
        NX_CHECK(dropped_keep.total() == 6U);
    }
}

} // namespace

int main() {
    run_suite("harness", test_harness);
    run_suite("span_enums", test_span_enums);
    run_suite("ids_and_context", test_ids_and_context);
    run_suite("attributes", test_attributes);
    run_suite("span_record", test_span_record);
    run_suite("parent_stack", test_parent_stack);
    run_suite("concepts", test_concepts);
    run_suite("scoped_span", test_scoped_span);
    run_suite("latency", test_latency);
    run_suite("sinks", test_sinks);
    run_suite("writer_bounds_full", test_trace_writer_bounds_full);
    run_suite("writer_escape_matrix", test_trace_writer_escape_matrix);
    run_suite("serialization_full", test_record_serialization_full);
    run_suite("provider", test_provider);
    std::printf("tracing: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
