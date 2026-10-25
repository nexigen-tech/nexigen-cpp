// Тесты nexigen::chrono — C++23, без сторонних фреймворков (харнес как в
// core_test.cpp / design.md D4): NX_CHECK ведёт счёт проверок и падений с
// файлом/строкой, run_suite печатает строку-резюме на набор, main возвращает
// число падений (ненулевой код выхода роняет тест в CTest).
//
// Наборы: timestamp (переехал из tests/core/core_test.cpp), stamp и time_format
// (переехали из tests/logging/logging_test.cpp), utc_offset (публичный контракт
// sample_utc_offset/utc_from_mono_ns, открытый переездом из logging::detail).
//
// Правило Result (core/result.hpp): ветвиться по has_value(), никогда не
// звать .value().

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <chrono>
#include <thread>
#include <type_traits>

#include <nexigen/core/result.hpp>
#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/chrono/time_format.hpp>
#include <nexigen/chrono/timestamp.hpp>

// --- Compile-time claims (relocated with the type): Timestamp shape.
static_assert(sizeof(nexigen::chrono::Timestamp) == 8);
static_assert(std::is_trivially_copyable_v<nexigen::chrono::Timestamp>);

using namespace nexigen::core;   // test TU: exercise the error model unqualified
using namespace nexigen::chrono; // test TU: exercise the API unqualified

namespace {

int g_checks = 0;
int g_failures = 0;

// Allocation detector (перенесён из core_test.cpp): the C++ free store is
// re-backed by a fixed static bump arena. Any dynamic allocation anywhere in
// the process bumps g_alloc_used — hot-path code must leave it untouched.
// This is the harness's measurement hook, not a use of dynamic allocation
// itself.
alignas(16) unsigned char g_arena[1 << 16]{};
std::size_t g_alloc_used = 0;

} // namespace

[[noreturn]] void arena_exhausted() { std::abort(); }

void* operator new(std::size_t size) {
    const std::size_t rounded = (size + 15U) & ~static_cast<std::size_t>(15U);
    if (g_alloc_used + rounded > sizeof(g_arena)) { arena_exhausted(); }
    void* p = g_arena + g_alloc_used;
    g_alloc_used += rounded;
    return p;
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

// --- timestamp suite (moved from tests/core/core_test.cpp): value shape,
// clock sourcing, arithmetic.

void test_timestamp() {
    // Construct / read-back: the raw count round-trips exactly.
    const Timestamp t{1'234'567'890ULL};
    NX_CHECK(t.nanoseconds() == 1'234'567'890ULL);
    NX_CHECK(Timestamp{}.nanoseconds() == 0);

    // Copy preserves the count; comparison reflects only the count.
    const Timestamp copy = t;
    NX_CHECK(copy.nanoseconds() == t.nanoseconds());
    NX_CHECK(copy == t);

    // Ordering for deadline checks compares nanoseconds directly.
    NX_CHECK(Timestamp{10} < Timestamp{20});
    NX_CHECK(Timestamp{20} > Timestamp{10});
    NX_CHECK(Timestamp{10} == Timestamp{10});

    // Elapsed interval: a - b equals the stored nanosecond difference exactly.
    const Timestamp early{1000};
    const Timestamp late{1500};
    NX_CHECK((late - early).nanoseconds() == late.nanoseconds() - early.nanoseconds());
    NX_CHECK((late - early).nanoseconds() == 500);

    // Saturating delta: b > a yields 0, never a wrapped ~584-year interval.
    NX_CHECK((early - late).nanoseconds() == 0);

    // Offset arithmetic for deadlines.
    NX_CHECK((early + 500ULL).nanoseconds() == 1500);
    NX_CHECK((late - 500ULL).nanoseconds() == 1000);
    NX_CHECK((early - 999'999ULL).nanoseconds() == 0); // offset subtraction saturates too

    // now(): engaged Result on every read; successive reads never decrease.
    const Result<Timestamp> first = Timestamp::now();
    NX_CHECK(first.has_value());

    volatile std::uint64_t work = 0; // "work performed in between" (spec scenario)
    for (int i = 0; i < 10'000; ++i) { work += static_cast<std::uint64_t>(i); }
    static_cast<void>(work);

    const Result<Timestamp> second = Timestamp::now();
    NX_CHECK(second.has_value());
    if (first.has_value() && second.has_value()) {
        NX_CHECK(*second >= *first); // monotonic: never lower than the previous read
        NX_CHECK((*second - *first).nanoseconds() ==
                 second->nanoseconds() - first->nanoseconds());
    }

    // Repeated reads allocate nothing: the arena bump-counter (operator new
    // hook) must not move across 1000 now() calls. Clock failure is non-fatal:
    // every iteration returns (no abort) with the Result engaged.
    const std::size_t arena_before = g_alloc_used;
    int engaged_reads = 0;
    for (int i = 0; i < 1000; ++i) {
        const Result<Timestamp> r = Timestamp::now();
        if (r.has_value()) { ++engaged_reads; }
    }
    NX_CHECK(engaged_reads == 1000);
    NX_CHECK(g_alloc_used == arena_before);
}

// --- stamp suite (moved from tests/logging/logging_test.cpp; task 3.2 /
// logging design D6 / spec chrono/stamp-source).
void test_stamp() {
    Stamp_source src{};
    NX_CHECK(src.calibrate().has_value());

    // Non-decreasing per source object (spec scenario "Stamps are
    // non-decreasing per source object").
    Timestamp prev = src.sample();
    int regressions = 0;
    for (int i = 0; i < 1000; ++i) {
        const Timestamp t = src.sample();
        if (t < prev) { ++regressions; }
        prev = t;
    }
    NX_CHECK(regressions == 0);

    // Duration fidelity: a 20 ms sleep must read back 20 ms ±30 %
    // (spec scenario "Stamps reflect real elapsed time").
    const Timestamp a = src.sample();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const Timestamp b = src.sample();
    const std::uint64_t delta_ns = (b - a).nanoseconds();
    NX_CHECK(delta_ns >= 14'000'000ULL && delta_ns <= 26'000'000ULL);
    std::printf("stamp: 20ms window measured as %.3f ms\n",
                static_cast<double>(delta_ns) / 1'000'000.0);

    // Independent sources keep independent clamps (each logger owns one).
    Stamp_source other{};
    NX_CHECK(other.calibrate().has_value());
    NX_CHECK(other.sample() >= a); // both monotonic on the same clock domain
}

// --- utc_offset suite (spec chrono/stamp-source "UTC offset snapshot
// calibration" / "Monotonic-to-UTC conversion": public surface since the
// extract-chrono-module relocation).

// now() helper for the harness: branch on has_value(), never call .value().
[[nodiscard]] Timestamp mono_now_or_zero() noexcept {
    const Result<Timestamp> r = Timestamp::now();
    return r ? *r : Timestamp{0};
}

// Whole-process wall reading for the bracket check (libc-std only here,
// never in the library itself).
[[nodiscard]] std::int64_t system_clock_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void test_utc_offset() {
    std::int64_t off1 = 0;
    std::int64_t off2 = 0;
    NX_CHECK(sample_utc_offset(off1).has_value());
    NX_CHECK(sample_utc_offset(off2).has_value());

    // Both snapshots measure (REALTIME - MONOTONIC_RAW) on the same machine:
    // back-to-back samples may differ only by NTP slew/settime drift.
    const std::int64_t drift = off2 > off1 ? off2 - off1 : off1 - off2;
    NX_CHECK(drift < 1'000'000'000LL);

    // Deterministic conversion: same inputs, same output; later monotonic
    // counts convert to later UTC (order-preserving, pure arithmetic).
    const Timestamp mono_a = mono_now_or_zero();
    const std::int64_t u1 = utc_from_mono_ns(mono_a.nanoseconds(), off1);
    const std::int64_t u2 = utc_from_mono_ns(mono_a.nanoseconds(), off1);
    NX_CHECK(u1 == u2);
    volatile std::uint64_t spin = 0; // tiny work between stamps
    for (int i = 0; i < 1000; ++i) { spin += static_cast<std::uint64_t>(i); }
    static_cast<void>(spin);
    const Timestamp mono_b = mono_now_or_zero();
    NX_CHECK(utc_from_mono_ns(mono_b.nanoseconds(), off1) >= u1);

    // The converted stamp lands inside the wall-clock bracket taken around
    // the whole sequence: the offset's (rt, mono) pair lags its realtime read
    // by a couple of clock-read latencies, and CLOCK_REALTIME itself may slew
    // microseconds while the test runs — 1 ms bounds both effects (spec
    // "Offset sampled successfully": reproduce wall time within the two-read
    // latency).
    const std::int64_t wall_before = system_clock_ns();
    NX_CHECK(sample_utc_offset(off1).has_value());
    const Timestamp mono = mono_now_or_zero();
    const std::int64_t wall_after = system_clock_ns();
    const std::int64_t utc = utc_from_mono_ns(mono.nanoseconds(), off1);
    NX_CHECK(utc >= wall_before - 1'000'000LL && utc <= wall_after + 1'000'000LL);
    std::printf("utc_offset: offset=%lld ns, utc=%lld in [%lld, %lld]\n",
                static_cast<long long>(off1), static_cast<long long>(utc),
                static_cast<long long>(wall_before), static_cast<long long>(wall_after));
}

// --- time format suite (moved from tests/logging/logging_test.cpp; task 5.1 /
// logging design D10/D11): byte-exact fixtures for known epochs, leap day,
// pre-epoch floor division.
void test_time_format() {
    char buf[31]{};
    format_utc_iso8601_ns(1'784'850'120'321'841'133LL, buf);
    NX_CHECK(std::strcmp(buf, "2026-07-23T23:42:00.321841133Z") == 0);
    NX_CHECK(buf[30] == '\0');
    format_utc_iso8601_ns(0LL, buf);
    NX_CHECK(std::strcmp(buf, "1970-01-01T00:00:00.000000000Z") == 0);
    format_utc_iso8601_ns(1'709'164'800'000'000'000LL, buf); // leap day
    NX_CHECK(std::strcmp(buf, "2024-02-29T00:00:00.000000000Z") == 0);
    format_utc_iso8601_ns(1'704'067'199'999'999'999LL, buf); // year end
    NX_CHECK(std::strcmp(buf, "2023-12-31T23:59:59.999999999Z") == 0);
    format_utc_iso8601_ns(-1LL, buf); // pre-epoch floor division
    NX_CHECK(std::strcmp(buf, "1969-12-31T23:59:59.999999999Z") == 0);

    const Civil_time ct = civil_from_utc_ns(1'784'850'120'321'841'133LL);
    NX_CHECK(ct.year == 2026 && ct.month == 7 && ct.day == 23);
    NX_CHECK(ct.hour == 23 && ct.minute == 42 && ct.second == 0);
    NX_CHECK(ct.nanosecond == 321'841'133U);

    // Every rendered timestamp has the exact field layout (30 visible chars).
    for (const std::int64_t probe : {-1LL, 0LL, 999'999'999LL, 1'784'851'199'999'999'999LL}) {
        format_utc_iso8601_ns(probe, buf);
        NX_CHECK(std::strlen(buf) == 30);
    }
}

// --- diagnostics suite (change add-serialization-and-wait-free-logging, task
// 3.2): universal to_string() const + free helper for Timestamp, Stamp_source,
// Civil_time. Cold-path strings land in the bump arena; member==free always.

void test_diag_strings() {
    // Timestamp: ns count rendered (sec.ns9) and exact.
    const Timestamp ts{1'000'000'001ULL};
    const std::string ts_dump = ts.to_string();
    NX_CHECK(ts_dump.find("1.000000001") != std::string::npos);
    NX_CHECK(ts_dump.find("monotonic") != std::string::npos);
    NX_CHECK(to_string(ts) == ts_dump); // free == member
    NX_CHECK(Timestamp{0}.to_string().find("0.000000000") != std::string::npos);

    // Stamp_source: uncalibrated state is identifiable before calibrate().
    Stamp_source src{};
    const std::string pre = src.to_string();
    NX_CHECK(pre.find("calibrated=false") != std::string::npos);
    NX_CHECK(to_string(src) == pre);
    const Status calibrated = src.calibrate(); // cold sleep ~2 ms
    NX_CHECK(calibrated.has_value());
    static_cast<void>(src.sample());
    static_cast<void>(src.sample());
    const std::string post = src.to_string();
    NX_CHECK(post.find("calibrated=true") != std::string::npos);
    NX_CHECK(post.find("last_ns=") != std::string::npos);
    NX_CHECK(post.find("mono_anchor_ns=") != std::string::npos);
    NX_CHECK(to_string(src) == post);

    // Civil_time: full field decomposition renders.
    const Civil_time ct = civil_from_utc_ns(1'784'850'120'321'841'133LL);
    const std::string ct_dump = ct.to_string();
    NX_CHECK(ct_dump.find("2026-07-23") != std::string::npos);
    NX_CHECK(ct_dump.find("23:42:00") != std::string::npos);
    NX_CHECK(ct_dump.find("321841133") != std::string::npos);
    NX_CHECK(ct_dump.find("UTC") != std::string::npos);
    NX_CHECK(to_string(ct) == ct_dump);

    std::printf("diag_strings: Timestamp/Stamp_source/Civil_time dumps verified\n");
}

} // namespace

int main()
{
    run_suite("timestamp", test_timestamp);
    run_suite("stamp", test_stamp);
    run_suite("utc_offset", test_utc_offset);
    run_suite("time_format", test_time_format);
    run_suite("diag_strings", test_diag_strings);

    const int failures = g_failures;
    std::printf("total: %d checks, %d failed\n", g_checks, failures);
    return failures;
}
