// Тесты nexigen::diagnostics — C++23, без сторонних фреймворков (харнес как в
// chrono_test.cpp / design.md D4): NX_CHECK ведёт счёт проверок и падений с
// файлом/строкой, run_suite печатает строку-резюме на набор, main возвращает
// число падений (ненулевой код выхода роняет тест в CTest).
//
// Наборы: stopwatch_state (enum + сериализационный контракт манифестов 9-11),
// stopwatch (жизненный цикл, круги, запросы, ворота латентности), guard
// (ScopedStopwatch: RAII, бюджет) — добавляются по мере групп задач.
//
// Правило Result (core/result.hpp): ветвиться по has_value(), никогда не
// звать .value().

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/diagnostics/scoped_stopwatch.hpp>
#include <nexigen/diagnostics/stopwatch.hpp>
#include <nexigen/diagnostics/stopwatch_state.hpp>

/// Named sink shape for compile-time guard claims (a lambda type would work
/// too; a struct keeps the static_assert messages readable).
struct ProbeSink {
    void operator()(std::uint64_t) const noexcept {}
};

// --- Compile-time claims: enum shape and name spellings (design D5).
static_assert(sizeof(nexigen::diagnostics::StopwatchState) == 1U);
static_assert(nexigen::diagnostics::to_string(nexigen::diagnostics::StopwatchState::Idle) == "Idle");
static_assert(nexigen::diagnostics::to_string(nexigen::diagnostics::StopwatchState::Running) == "Running");
static_assert(nexigen::diagnostics::to_string(nexigen::diagnostics::StopwatchState::Stopped) == "Stopped");
// from_string is the exact inverse of to_string (constexpr exercised).
static_assert([]() {
    nexigen::diagnostics::StopwatchState s{};
    return nexigen::diagnostics::from_string("Stopped", s) && s == nexigen::diagnostics::StopwatchState::Stopped;
}());

// --- Compile-time claims: Stopwatch / LapSnapshot layout (design D2/D6).
static_assert(sizeof(nexigen::diagnostics::Stopwatch) == 64);
static_assert(alignof(nexigen::diagnostics::Stopwatch) == 64);
static_assert(std::is_trivially_copyable_v<nexigen::diagnostics::Stopwatch>);
static_assert(sizeof(nexigen::diagnostics::LapSnapshot) == 24);
static_assert(nexigen::diagnostics::Stopwatch::no_lap_value ==
              0xFFFF'FFFF'FFFF'FFFFULL);

// --- Compile-time claims: the guard is not transferable (design D7; spec
// diagnostics/scoped-stopwatch "Guard is not transferable") — copy and move
// both fail compilation, for both destination kinds.
using OutRefGuard =
    nexigen::diagnostics::ScopedStopwatch<nexigen::diagnostics::detail::OutRefSink>;
using SinkGuard = nexigen::diagnostics::ScopedStopwatch<ProbeSink>;
static_assert(!std::is_copy_constructible_v<OutRefGuard>);
static_assert(!std::is_copy_assignable_v<OutRefGuard>);
static_assert(!std::is_move_constructible_v<OutRefGuard>);
static_assert(!std::is_move_assignable_v<OutRefGuard>);
static_assert(!std::is_copy_constructible_v<SinkGuard>);
static_assert(!std::is_move_constructible_v<SinkGuard>);

using namespace nexigen::diagnostics; // test TU: exercise the API unqualified

namespace {

int g_checks = 0;
int g_failures = 0;

// Allocation detector (как в chrono_test.cpp): the C++ free store is
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

// --- stopwatch_state suite (spec diagnostics/stopwatch-state; task 2.2 /
// design D5): enum contract — constexpr names, strict parse, non-throwing
// JSON, formatter.
void test_stopwatch_state() {
    // Every enumerator round-trips (spec "Every enumerator round-trips").
    for (const StopwatchState state : {StopwatchState::Idle, StopwatchState::Running,
                                       StopwatchState::Stopped}) {
        const std::string_view name = to_string(state);
        NX_CHECK(!name.empty() && name != "Unknown");
        StopwatchState round = StopwatchState::Idle;
        NX_CHECK(from_string(name, round));
        NX_CHECK(round == state);
    }

    // Unknown input is rejected without mutation (spec scenario).
    StopwatchState out = StopwatchState::Running;
    NX_CHECK(!from_string("running ", out));       // trailing space
    NX_CHECK(out == StopwatchState::Running);      // out-parameter untouched
    NX_CHECK(!from_string("idle", out));           // different case
    NX_CHECK(out == StopwatchState::Running);
    NX_CHECK(!from_string("", out));               // empty name
    NX_CHECK(out == StopwatchState::Running);
    NX_CHECK(!from_string("Unknown", out));        // rendering is not parseable
    NX_CHECK(out == StopwatchState::Running);

    // Garbage-cast values stay well-defined (spec scenario): no UB, "Unknown".
    const auto garbage = static_cast<StopwatchState>(99U);
    NX_CHECK(to_string(garbage) == "Unknown");
    NX_CHECK(!from_string("Unknown", out)); // "Unknown" round-trips nothing

    // JSON round-trip preserves the state (spec scenario).
    nlohmann::json j = StopwatchState::Idle;
    NX_CHECK(j.is_string() && j.get<std::string_view>() == "Idle");
    StopwatchState back = StopwatchState::Stopped;
    from_json(j, back);
    NX_CHECK(back == StopwatchState::Idle);

    // Non-string and malformed JSON are silently rejected (spec scenario).
    StopwatchState keep = StopwatchState::Running;
    from_json(nlohmann::json(7), keep);
    NX_CHECK(keep == StopwatchState::Running);
    from_json(nlohmann::json(nullptr), keep);
    NX_CHECK(keep == StopwatchState::Running);
    from_json(nlohmann::json(true), keep);
    NX_CHECK(keep == StopwatchState::Running);
    from_json(nlohmann::json("Bogus"), keep); // unknown name
    NX_CHECK(keep == StopwatchState::Running);
    from_json(nlohmann::json("Running"), keep); // the one accepted form
    NX_CHECK(keep == StopwatchState::Running);

    // Direct formatting works without conversion (spec scenario).
    NX_CHECK(std::format("{}|{}", StopwatchState::Running, StopwatchState::Idle) == "Running|Idle");

    // Fill and alignment apply (spec scenario).
    NX_CHECK(std::format("{:>8}", StopwatchState::Idle) == "    Idle");
    NX_CHECK(std::format("{:^9}", StopwatchState::Running) == " Running ");

    std::printf("  stopwatch_state: round-trip/parse/JSON/formatter verified\n");
}

// --- stopwatch suite (spec diagnostics/stopwatch; task 3.3 / design D3/D4):
// lifecycle, restart, laps, snapshot, queries, sources, allocations.

// Deterministic piece of "work performed" that cannot be optimized away:
// roughly a microsecond per unit argument, over an observable volatile sink.
[[nodiscard]] std::uint64_t spin_us(int micros) noexcept {
    volatile std::uint64_t sink = 0;
    const int iters = micros * 600;
    for (int i = 0; i < iters; ++i) { sink += static_cast<std::uint64_t>(i); }
    return sink;
}

void test_stopwatch_lifecycle() {
    // Fresh stopwatch is idle with zero elapsed (spec scenario).
    Stopwatch sw{};
    NX_CHECK(sw.state() == StopwatchState::Idle);
    NX_CHECK(!sw.is_running());
    NX_CHECK(sw.elapsed_ns() == 0U);
    NX_CHECK(sw.elapsed_us() == 0.0);
    NX_CHECK(sw.elapsed_us_count() == 0U);
    NX_CHECK(sw.elapsed_ms() == 0.0);
    NX_CHECK(sw.elapsed_seconds() == 0.0);

    // Start running transitions from idle (spec scenario).
    sw.start();
    NX_CHECK(sw.state() == StopwatchState::Running);
    NX_CHECK(sw.is_running());

    // Elapsed remains non-decreasing while running (spec scenario).
    std::uint64_t prev = sw.elapsed_ns();
    bool monotone = true;
    for (int i = 0; i < 100; ++i) {
        static_cast<void>(spin_us(1));
        const std::uint64_t now_reading = sw.elapsed_ns();
        if (now_reading < prev) { monotone = false; }
        prev = now_reading;
    }
    NX_CHECK(monotone);
    NX_CHECK(prev > 0U); // observed elapsed grew with performed work

    // Stop freezes while preserving the reading (spec scenario): repeated
    // queries return the same value, no sampling advances it.
    sw.stop();
    NX_CHECK(sw.state() == StopwatchState::Stopped);
    NX_CHECK(!sw.is_running());
    const std::uint64_t frozen = sw.elapsed_ns();
    NX_CHECK(frozen == sw.elapsed_ns());
    NX_CHECK(frozen == sw.elapsed_ns());

    // Resume accumulates across stop/start cycles (spec scenario).
    sw.start();
    NX_CHECK(sw.is_running());
    static_cast<void>(spin_us(2));
    sw.stop();
    const std::uint64_t after_resume = sw.elapsed_ns();
    NX_CHECK(after_resume > frozen); // interval two stacked on interval one

    // Restart from stopped clears and resumes (spec scenario).
    const std::uint64_t reported = sw.restart();
    NX_CHECK(reported == after_resume);
    NX_CHECK(sw.state() == StopwatchState::Running);
    NX_CHECK(sw.elapsed_ns() < reported); // fresh interval from ~zero

    // Reset clears everything (spec scenario) — from Running this time.
    sw.reset();
    NX_CHECK(sw.state() == StopwatchState::Idle);
    NX_CHECK(sw.elapsed_ns() == 0U);
    NX_CHECK(sw.lap_count() == 0U);
    NX_CHECK(!sw.is_running());
}

void test_stopwatch_restart_zero_gap() {
    // Zero-gap continuity (spec "Restart anchors the next interval at the
    // same sample"): a spanning stopwatch must read the restart-reported
    // total + the fresh interval's growth within a read-overhead bracket,
    // and a second restart must report only the fresh growth (no
    // pre-restart wall time leaks into a restarted interval).
    Stopwatch sw{};
    sw.start();
    static_cast<void>(spin_us(3));
    sw.stop();
    const std::uint64_t before_restart = sw.elapsed_ns();

    sw.start(); // resume
    const std::uint64_t picked = sw.restart();
    static_cast<void>(spin_us(3));
    const std::uint64_t fresh_growth = sw.elapsed_ns();
    const std::uint64_t picked_again = sw.restart(); // closes ONLY fresh time

    // Reported total includes the closing interval (spec scenario).
    NX_CHECK(picked >= before_restart);
    // Fresh-interval independence: the second restart reports essentially
    // only what grew after the first restart (sampling-tolerance bracket).
    NX_CHECK(picked_again > 0U && picked_again <= fresh_growth + 5'000U);

    // Sum continuity against a spanning stopwatch (spec scenario).
    Stopwatch sw2{};
    Stopwatch spanning{};
    spanning.start();
    sw2.start();
    static_cast<void>(spin_us(2));
    sw2.stop();
    sw2.start();
    const std::uint64_t total2 = sw2.restart();
    static_cast<void>(spin_us(2));
    const std::uint64_t growth2 = sw2.elapsed_ns();
    sw2.stop();
    const std::uint64_t spanning_total = spanning.elapsed_ns();
    const std::uint64_t sum2 = total2 + growth2;
    // spanning's own reads put a small overhead on both sides: 10 us bracket
    // (underflow-safe form: measured intervals here are ~us-scale).
    NX_CHECK(sum2 <= spanning_total + 10'000U && sum2 + 10'000U >= spanning_total);
    NX_CHECK(spanning_total >= total2);
}

void test_stopwatch_laps() {
    Stopwatch sw{};
    sw.start();

    // No-lap defaults are queryable (spec scenario).
    NX_CHECK(sw.lap_count() == 0U);
    NX_CHECK(sw.last_lap_ns() == 0U);
    NX_CHECK(sw.max_lap_ns() == 0U);
    NX_CHECK(sw.min_lap_ns() == Stopwatch::no_lap_value);

    // First lap measures from the segment start (spec scenario); run stays.
    static_cast<void>(spin_us(1));
    const std::uint64_t first = sw.lap();
    NX_CHECK(first > 0U);
    NX_CHECK(sw.is_running()); // no stop() state change
    NX_CHECK(sw.lap_count() == 1U);
    NX_CHECK(sw.last_lap_ns() == first);
    NX_CHECK(sw.min_lap_ns() == first);
    NX_CHECK(sw.max_lap_ns() == first);

    // Consecutive laps measure only their own segment; second is longer.
    static_cast<void>(spin_us(3));
    const std::uint64_t second = sw.lap();
    NX_CHECK(second > first);                     // segment lengths grew
    NX_CHECK(sw.lap_count() == 2U);               // count grew by one
    NX_CHECK(sw.last_lap_ns() == second);
    NX_CHECK(sw.min_lap_ns() == first);           // min brackets the laps
    NX_CHECK(sw.max_lap_ns() == second);

    // Total still covers the sum of laps (within sampled-running tolerance).
    NX_CHECK(sw.elapsed_ns() >= first + second);

    // Calling lap while stopped is a no-op (spec scenario).
    sw.stop();
    const std::uint64_t phantom = sw.lap();
    NX_CHECK(phantom == 0U);
    NX_CHECK(sw.lap_count() == 2U);        // no phantom lap recorded
    NX_CHECK(sw.last_lap_ns() == second);  // untouched
    NX_CHECK(sw.min_lap_ns() == first);    // untouched
}

void test_lap_snapshot() {
    // Snapshot with no laps reports the empty state (spec scenario).
    Stopwatch idle{};
    const LapSnapshot empty = idle.lap_snapshot();
    NX_CHECK(empty.lap_number == 0U);
    NX_CHECK(empty.lap_ns == 0U);
    NX_CHECK(empty.total_ns == 0U);
    NX_CHECK(empty.state == StopwatchState::Idle);

    // Snapshot reflects the latest lap (spec scenario).
    Stopwatch sw{};
    sw.start();
    static_cast<void>(spin_us(1));
    static_cast<void>(sw.lap());
    static_cast<void>(spin_us(2));
    const std::uint64_t second_lap = sw.lap();
    const LapSnapshot snap = sw.lap_snapshot();
    NX_CHECK(snap.lap_number == 2U);
    NX_CHECK(snap.lap_ns == second_lap); // matches the last lap reading
    NX_CHECK(snap.state == StopwatchState::Running);
    const std::uint64_t total_at_snapshot = snap.total_ns;
    NX_CHECK(total_at_snapshot >= second_lap); // total covers both laps
    NX_CHECK(sw.lap_count() == 2U);            // snapshotting took no lap

    // Snapshotting does not mutate the stopwatch (spec scenario).
    const LapSnapshot stopped_snap = [&] {
        sw.stop();
        return sw.lap_snapshot();
    }();
    NX_CHECK(stopped_snap.lap_number == 2U);
    NX_CHECK(stopped_snap.state == StopwatchState::Stopped);
    NX_CHECK(stopped_snap.total_ns == sw.elapsed_ns()); // pure reads match

    // Trivially copyable semantics ride the struct (design D6).
    const LapSnapshot copy = stopped_snap;
    NX_CHECK(copy.lap_number == stopped_snap.lap_number);
    NX_CHECK(copy == stopped_snap);
}

void test_stopwatch_elapsed_units() {
    Stopwatch sw{};
    sw.start();
    static_cast<void>(spin_us(2));
    sw.stop();
    const std::uint64_t ns = sw.elapsed_ns();
    NX_CHECK(ns > 0U);

    // Unit conversions scale correctly against the nanosecond total
    // (spec scenario): mm — all from the one ns reading.
    const double us = sw.elapsed_us();            // ns/1000
    const std::uint64_t us_count = sw.elapsed_us_count(); // truncated
    const double ms = sw.elapsed_ms();
    const double secs = sw.elapsed_seconds();
    NX_CHECK(us >= static_cast<double>(ns) / 1000.0 - 1.0);
    NX_CHECK(us <= static_cast<double>(ns) / 1000.0 + 1.0);
    NX_CHECK(us_count == ns / 1'000U); // integer form truncates exactly
    NX_CHECK(ms >= static_cast<double>(ns) / 1'000'000.0 - 0.001);
    NX_CHECK(secs >= static_cast<double>(ns) / 1'000'000'000.0 - 0.000001);
    NX_CHECK(ms * 1'000.0 >= us - 1.0);
    NX_CHECK(secs * 1'000.0 >= ms - 0.001);

    // Queries are valid and non-mutating mid-run too (spec scenario).
    sw.start();
    static_cast<void>(spin_us(1));
    const StopwatchState running_state = sw.state();
    NX_CHECK(running_state == StopwatchState::Running);
    NX_CHECK(sw.is_running());
    static_cast<void>(sw.elapsed_us());
    static_cast<void>(sw.elapsed_ms());
    static_cast<void>(sw.elapsed_seconds());
    static_cast<void>(sw.elapsed_us_count());
    NX_CHECK(sw.lap_count() == 0U);       // queries recorded nothing
    NX_CHECK(sw.state() == running_state); // state unchanged by queries
    sw.reset();
}

void test_stopwatch_clock_sources() {
    // Default construction uses the raw monotonic clock (spec scenario):
    // serial intervals measured on the default source never decrease.
    std::uint64_t prev_interval = 0U;
    for (int i = 0; i < 5; ++i) {
        Stopwatch sw{};
        sw.start();
        static_cast<void>(spin_us(1));
        sw.stop();
        const std::uint64_t one = sw.elapsed_ns();
        const std::uint64_t two = sw.elapsed_ns();
        NX_CHECK(one > 0U);
        NX_CHECK(two == one); // stopped: frozen, no decrease
        if (i > 0) {
            // no monotonic violation across serial loops (sampling tolerance:
            // a 1 us interval re-measured can wobble only microseconds;
            // underflow-safe form: intervals are ns-scale here)
            NX_CHECK(one + 10'000ULL >= prev_interval);
        }
        prev_interval = one;
    }

    // Explicit Stamp_source stamps drive the same semantics (spec scenario).
    nexigen::chrono::Stamp_source source{};
    NX_CHECK(source.calibrate().has_value());
    Stopwatch calibrated{&source};
    NX_CHECK(calibrated.source() == &source);
    calibrated.start();
    static_cast<void>(spin_us(1));
    calibrated.stop();
    const std::uint64_t interval = calibrated.elapsed_ns();
    NX_CHECK(interval > 0U); // positive and consistent on the TSC domain

    // Null source pointer falls back to the default clock (spec scenario).
    Stopwatch fallback{nullptr};
    NX_CHECK(fallback.source() == nullptr);
    fallback.start();
    static_cast<void>(spin_us(1));
    fallback.stop();
    NX_CHECK(fallback.elapsed_ns() > 0U);

    // Elapsed never reports negative or wrapped values (spec requirement):
    // running reads are monotone per-source by construction.
    // (Exercised above by the non-decreasing loops.)
}

void test_stopwatch_diag_strings() {
    // Diagnostic rendering (mandate 9; spec "Rendering reports live state").
    Stopwatch sw{};
    sw.start();
    static_cast<void>(spin_us(1));
    static_cast<void>(sw.lap());

    // Live content on a running stopwatch: type name, state name, total
    // elapsed and the lap counter (spec scenario).
    const std::string member = sw.to_string();
    NX_CHECK(member.find("Stopwatch(") != std::string::npos);
    NX_CHECK(member.find("state=Running") != std::string::npos);
    NX_CHECK(member.find("laps=1") != std::string::npos);
    NX_CHECK(member.find("elapsed_ns=") != std::string::npos);

    // LapSnapshot rendering + member-free parity (a snapshot is a frozen
    // value: two renders of one snapshot are exactly equal).
    const LapSnapshot snap = sw.lap_snapshot();
    const std::string snap_member = snap.to_string();
    NX_CHECK(to_string(snap) == snap_member);
    NX_CHECK(snap_member.find("LapSnapshot(number=1") != std::string::npos);
    NX_CHECK(snap_member.find("state=Running") != std::string::npos);

    // Free/member/formatter parity is byte-exact on a stable reading: while
    // Running each render samples live time independently (only the elapsed
    // digits may differ, the fields above cover that face), so exact
    // equality is pinned after stop() freezes the reading (spec scenario:
    // "identical to the free-function rendering of the same object").
    sw.stop();
    const std::string frozen = sw.to_string();
    NX_CHECK(to_string(sw) == frozen);        // free == member
    NX_CHECK(std::format("{}", sw) == frozen); // formatter == member
    NX_CHECK(frozen.find("state=Stopped") != std::string::npos);

    // Cold-path no throw/UB while re-rendering repeatedly.
    for (int i = 0; i < 10; ++i) { static_cast<void>(sw.to_string()); }
    sw.reset();
    NX_CHECK(sw.to_string().find("state=Idle") != std::string::npos);
}

void test_stopwatch_allocation_free_hot_path() {
    // Hot operations never allocate (spec scenario): the arena bump-counter
    // must not move across a full lifecycle loop.
    const std::size_t arena_before = g_alloc_used;
    Stopwatch sw{};
    constexpr int loops = 1'000;
    for (int i = 0; i < loops; ++i) {
        sw.start();
        static_cast<void>(sw.lap());
        static_cast<void>(sw.restart());
        static_cast<void>(sw.elapsed_ns());
        static_cast<void>(sw.elapsed_us_count());
        sw.stop();
        static_cast<void>(sw.elapsed_ns());
        sw.reset();
    }
    NX_CHECK(g_alloc_used == arena_before); // zero heap across 1000 loops

    // std::string rendering is permitted to allocate (cold path only):
    static_cast<void>(sw.to_string());
    NX_CHECK(g_alloc_used > arena_before); // the cold string landed
}

// --- guard suite (spec diagnostics/scoped-stopwatch; task 4.2 / design D7–D9):
// RAII lifecycle and exactly-once delivery, budget checks, constructor
// matrix, allocation freedom, rendering.

void test_guard_lifecycle() {
    // Out-reference destination receives the elapsed value (spec scenario):
    // the guard's scope strictly contains the inner measurement on the same
    // clock, so the delivered count is at least the performed work — and
    // consistent across repeated scopes.
    for (int round = 0; round < 3; ++round) {
        std::uint64_t out = 0;
        Stopwatch inner{};
        {
            ScopedStopwatch g{out};
            inner.start();
            static_cast<void>(spin_us(1));
            inner.stop();
        }
        NX_CHECK(out > 0U);                  // strictly positive
        NX_CHECK(out >= inner.elapsed_ns()); // covers the inner work
    }

    // Sink destination is invoked exactly once (spec scenario): never at
    // construction, never mid-scope (budget checks deliver nothing early),
    // exactly once at destruction with the scope's elapsed.
    int calls = 0;
    std::uint64_t last = 0;
    {
        ScopedStopwatch g{[&](std::uint64_t ns) noexcept { ++calls; last = ns; }};
        NX_CHECK(calls == 0); // never at construction
        static_cast<void>(spin_us(1));
        NX_CHECK(calls == 0); // nor mid-scope
        static_cast<void>(g.has_exceeded_budget(0ULL));
        NX_CHECK(calls == 0); // checks never report early
    }
    NX_CHECK(calls == 1);  // exactly once at destruction
    NX_CHECK(last > 0U);   // with the scope's elapsed nanoseconds

    // Constructor overload matrix (spec "Construction overloads and
    // default-clock fallback"): every call form resolves to its clear
    // overload — proven by the source/budget readbacks — and reports at
    // destruction.
    nexigen::chrono::Stamp_source src{};
    NX_CHECK(src.calibrate().has_value());
    std::uint64_t out = 0;
    int sink_calls = 0;
    auto sink = [&](std::uint64_t) noexcept { ++sink_calls; };
    {
        ScopedStopwatch g{out};
        NX_CHECK(g.source() == nullptr);
        NX_CHECK(g.default_budget() == 0U);
    }
    NX_CHECK(out > 0U);
    {
        ScopedStopwatch g{out, 7U};
        NX_CHECK(g.source() == nullptr);
        NX_CHECK(g.default_budget() == 7U);
    }
    {
        ScopedStopwatch g{&src, out};
        NX_CHECK(g.source() == &src);
        NX_CHECK(g.default_budget() == 0U);
    }
    {
        ScopedStopwatch g{&src, out, 9U};
        NX_CHECK(g.source() == &src);
        NX_CHECK(g.default_budget() == 9U);
    }
    {
        ScopedStopwatch g{sink};
        NX_CHECK(g.source() == nullptr);
        NX_CHECK(g.default_budget() == 0U);
    }
    {
        ScopedStopwatch g{sink, 7U};
        NX_CHECK(g.source() == nullptr);
        NX_CHECK(g.default_budget() == 7U);
    }
    {
        ScopedStopwatch g{&src, sink};
        NX_CHECK(g.source() == &src);
        NX_CHECK(g.default_budget() == 0U);
    }
    {
        ScopedStopwatch g{&src, sink, 9U};
        NX_CHECK(g.source() == &src);
        NX_CHECK(g.default_budget() == 9U);
    }
    NX_CHECK(sink_calls == 4); // each sink guard delivered exactly once
}

void test_guard_budget() {
    // Within budget reports false (spec scenario): a bounded query
    // immediately after construction against a threshold the elapsed cannot
    // have reached.
    constexpr std::uint64_t sentinel = 0xDEAD'BEEF'CAFE'F00DULL;
    std::uint64_t out = sentinel;
    {
        ScopedStopwatch g{out};
        NX_CHECK(!g.has_exceeded_budget(1'000'000'000ULL)); // 1 s unreachable

        // Exceeded budget reports true during the scope (spec scenario) with
        // strictly-greater semantics — and the destination has received
        // nothing yet: destruction still holds the final report.
        static_cast<void>(spin_us(1));
        NX_CHECK(g.has_exceeded_budget(1ULL)); // work clearly exceeded 1 ns
        NX_CHECK(g.has_exceeded_budget(0ULL)); // strictly greater than zero
        NX_CHECK(out == sentinel);             // no early delivery
    }
    NX_CHECK(out != sentinel); // destruction held the final report
    NX_CHECK(out > 0U);

    // Default budget participates when configured (spec scenario): the same
    // body of work — a guard with a tiny stored budget reports true through
    // the no-argument overload while an unconfigured guard reports false.
    std::uint64_t configured_out = sentinel;
    std::uint64_t plain_out = sentinel;
    {
        ScopedStopwatch configured{configured_out, 1ULL}; // 1 ns default
        ScopedStopwatch plain{plain_out};                 // none configured
        static_cast<void>(spin_us(1));
        NX_CHECK(configured.has_exceeded_budget()); // true: work > 1 ns
        NX_CHECK(!plain.has_exceeded_budget());     // false: unconfigured
        NX_CHECK(configured.default_budget() == 1ULL);
        NX_CHECK(plain.default_budget() == 0U);
    }
    NX_CHECK(configured_out > 0U);
    NX_CHECK(plain_out > 0U);
}

void test_guard_allocation_free_hot_path() {
    // Guard lifecycles allocate nothing (spec "Guard lifecycle allocates
    // nothing"): the arena bump-counter must not move across repeated
    // construct/check/destruct cycles with both destination kinds.
    nexigen::chrono::Stamp_source src{};
    NX_CHECK(src.calibrate().has_value());
    const std::size_t arena_before = g_alloc_used;
    std::uint64_t out = 0;
    int sink_calls = 0;
    for (int i = 0; i < 1'000; ++i) {
        {
            ScopedStopwatch g{&src, out, 1'000'000ULL};
            static_cast<void>(g.has_exceeded_budget());
            static_cast<void>(g.has_exceeded_budget(1ULL));
        }
        {
            ScopedStopwatch g{&src, [&](std::uint64_t) noexcept { ++sink_calls; }};
        }
    }
    NX_CHECK(g_alloc_used == arena_before); // zero heap across 2000 lifecycles
    NX_CHECK(sink_calls == 1'000);

    // Rendering is permitted to allocate (cold path only; spec "Guard format
    // stays cold-path" — the hot loop above never rendered).
    {
        ScopedStopwatch g{out};
        static_cast<void>(g.to_string());
    }
    NX_CHECK(g_alloc_used > arena_before);
}

void test_guard_strings() {
    // Guard renders its live measurement (spec scenario): active status,
    // elapsed-so-far and the pending default budget through member, free
    // helper and std::format. Live renders each re-sample elapsed (cold
    // path), so parity is structural — identical shape and byte-identical
    // status/budget fields, matching the live-object pattern the tracing
    // tests use; byte-equality is pinned there on frozen values instead.
    std::uint64_t out = 0;
    ScopedStopwatch g{out, 42ULL};
    static_cast<void>(spin_us(1));
    const std::string member = g.to_string();
    const std::string free_text = to_string(g);
    const std::string fmt = std::format("{}", g);
    const std::string prefix = "ScopedStopwatch(active=true, elapsed_ns=";
    const std::string suffix = ", budget_ns=42)";
    for (const std::string& text : {member, free_text, fmt}) {
        NX_CHECK(text.rfind(prefix, 0) == 0); // active status + elapsed readout
        NX_CHECK(text.size() > prefix.size() + suffix.size());
        NX_CHECK(text.compare(text.size() - suffix.size(), suffix.size(),
                              suffix) == 0); // pending default budget
        NX_CHECK(text.find('\\') == std::string::npos); // no JSON-style escapes
    }
}

#ifdef NEXIGEN_DIAGNOSTICS_STRICT_LATENCY
namespace {

// Sanitizer detection (design D10): under ASan/TSan the numeric latency gate
// is compiled out — instrumented timings are not representative.
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

[[nodiscard]] inline double now_ns_f() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<double>(ts.tv_sec) * 1e9 + static_cast<double>(ts.tv_nsec);
}

template <class F>
[[nodiscard]] double median_ns_per_op(F&& body, int batches, int ops) {
    body(ops / 10 + 1); // warm-up
    double samples[21];
    for (int b = 0; b < batches; ++b) {
        const double start = now_ns_f();
        body(ops);
        const double stop = now_ns_f();
        samples[b] = (stop - start) / static_cast<double>(ops);
    }
    // Insertion sort over the median window (21 elements).
    for (int i = 1; i < batches; ++i) {
        const double key = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > key) {
            samples[j + 1] = samples[j];
            --j;
        }
        samples[j + 1] = key;
    }
    return samples[batches / 2];
}

} // namespace
#endif

// --- Hot-path latency gates (spec diagnostics/stopwatch "Hot path performs
// zero allocation and meets latency budgets" + diagnostics/scoped-stopwatch
// same-named requirement; task 3.3/4.2 / design D10): compiled only under
// NEXIGEN_DIAGNOSTICS_STRICT_LATENCY — Debug and sanitizer builds run the
// correctness-only mode.

[[maybe_unused]] void test_latency() {
#ifndef NEXIGEN_DIAGNOSTICS_STRICT_LATENCY
    std::printf("  latency gates compiled out (correctness-only mode)\n");
    NX_CHECK(true);
#elif defined(NX_SANITIZER_BUILD)
    std::printf("  latency medians not gated under instrumented build\n");
    NX_CHECK(true);
#else
    nexigen::chrono::Stamp_source calibrated{};
    NX_CHECK(calibrated.calibrate().has_value());

    constexpr int batches = 21;
    constexpr int ops = 100'000;

    // start() + stop() on the default clock: two Timestamp::now() reads.
    Stopwatch default_sw{};
    const double default_start_stop = median_ns_per_op(
        [&default_sw](int n) {
            for (int i = 0; i < n; ++i) {
                default_sw.start();
                default_sw.stop();
            }
        },
        batches, ops);

    // start() + stop() on the calibrated Stamp_source (~3-5 ns/sample).
    Stopwatch cal_sw{&calibrated};
    const double cal_start_stop = median_ns_per_op(
        [&cal_sw](int n) {
            for (int i = 0; i < n; ++i) {
                cal_sw.start();
                cal_sw.stop();
            }
        },
        batches, ops);

    // restart() = close + re-open at the same stamp.
    cal_sw.start();
    const double cal_restart = median_ns_per_op(
        [&cal_sw](int n) {
            for (int i = 0; i < n; ++i) {
                static_cast<void>(cal_sw.restart());
            }
        },
        batches, ops);

    // lap() on the calibrated source.
    const double cal_lap = median_ns_per_op(
        [&cal_sw](int n) {
            for (int i = 0; i < n; ++i) {
                static_cast<void>(cal_sw.lap());
            }
        },
        batches, ops);

    // elapsed query while running.
    const double cal_elapsed = median_ns_per_op(
        [&cal_sw](int n) {
            for (int i = 0; i < n; ++i) {
                static_cast<void>(cal_sw.elapsed_ns());
            }
        },
        batches, ops);
    cal_sw.stop();

    // The overhead benchmark report (task 6.2 verifies these numbers).
    std::printf("  start+stop medians (ns/op): default=%.2f calibrated=%.2f | "
                "calibrated restart=%.2f lap=%.2f elapsed=%.2f\n",
                default_start_stop, cal_start_stop, cal_restart, cal_lap, cal_elapsed);

    // Gates (design D10): generous headroom over the one-stamp costs.
    NX_CHECK(default_start_stop > 0.0);  // genuine measurement, not optimized out
    NX_CHECK(cal_start_stop > 0.0);
    NX_CHECK(default_start_stop <= 100.0); // two vDSO reads ≈ 40-50 ns
    NX_CHECK(cal_start_stop <= 20.0);      // two TSC stamps ≈ 6-10 ns
    NX_CHECK(cal_restart <= 20.0);
    NX_CHECK(cal_lap <= 20.0);
    NX_CHECK(cal_elapsed <= 20.0);
#endif // sanitizer / strict split
}

// --- Guard lifecycle latency gate (spec diagnostics/scoped-stopwatch "Hot
// path and layout guarantees" scenario "Release latency gate holds for guard
// lifecycle"; task 4.2 / design D10): construct + destruct ≤ 20 ns calibrated.
[[maybe_unused]] void test_guard_latency() {
#ifndef NEXIGEN_DIAGNOSTICS_STRICT_LATENCY
    std::printf("  guard latency gate compiled out (correctness-only mode)\n");
    NX_CHECK(true);
#elif defined(NX_SANITIZER_BUILD)
    std::printf("  guard latency medians not gated under instrumented build\n");
    NX_CHECK(true);
#else
    nexigen::chrono::Stamp_source calibrated{};
    NX_CHECK(calibrated.calibrate().has_value());

    constexpr int batches = 21;
    constexpr int ops = 100'000;
    std::uint64_t sink_out = 0;
    const double guard_cd = median_ns_per_op(
        [&calibrated, &sink_out](int n) {
            for (int i = 0; i < n; ++i) {
                ScopedStopwatch g{&calibrated, sink_out};
                static_cast<void>(g);
            }
        },
        batches, ops);
    std::printf("  guard construct+destruct median (ns/op): calibrated=%.2f\n",
                guard_cd);

    // Gates (design D10): two TSC stamps + fixed stores per lifecycle.
    NX_CHECK(guard_cd > 0.0);   // genuine measurement, not optimized out
    NX_CHECK(guard_cd <= 20.0); // construct + destruct budget
#endif // sanitizer / strict split
}

} // namespace

int main()
{
    run_suite("stopwatch_state", test_stopwatch_state);
    run_suite("lifecycle", test_stopwatch_lifecycle);
    run_suite("restart_zero_gap", test_stopwatch_restart_zero_gap);
    run_suite("laps", test_stopwatch_laps);
    run_suite("lap_snapshot", test_lap_snapshot);
    run_suite("elapsed_units", test_stopwatch_elapsed_units);
    run_suite("clock_sources", test_stopwatch_clock_sources);
    run_suite("diag_strings", test_stopwatch_diag_strings);
    run_suite("hot_path_allocs", test_stopwatch_allocation_free_hot_path);
    run_suite("guard_lifecycle", test_guard_lifecycle);
    run_suite("guard_budget", test_guard_budget);
    run_suite("guard_allocs", test_guard_allocation_free_hot_path);
    run_suite("guard_strings", test_guard_strings);
#ifdef NEXIGEN_DIAGNOSTICS_STRICT_LATENCY
    run_suite("latency", test_latency);
    run_suite("guard_latency", test_guard_latency);
#endif

    const int failures = g_failures;
    std::printf("total: %d checks, %d failed\n", g_checks, failures);
    return failures;
}
