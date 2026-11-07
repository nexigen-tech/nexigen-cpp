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

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../support/test_temp.hpp"

#include <nexigen/core/result.hpp>
#include <nexigen/chrono/chrono_provider.hpp>
#include <nexigen/chrono/chrono_settings.hpp>
#include <nexigen/chrono/chrono_snapshot.hpp>
#include <nexigen/chrono/clock_id.hpp>
#include <nexigen/chrono/composite_chrono_sink.hpp>
#include <nexigen/chrono/concepts/chrono_sink_concept.hpp>
#include <nexigen/chrono/console_chrono_sink.hpp>
#include <nexigen/chrono/detail/affinity.hpp>
#include <nexigen/chrono/json_file_chrono_sink.hpp>
#include <nexigen/chrono/null_chrono_sink.hpp>
#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/chrono/time_format.hpp>
#include <nexigen/chrono/timestamp.hpp>

// --- Compile-time claims (relocated with the type): Timestamp shape.
static_assert(sizeof(nexigen::chrono::Timestamp) == 8);
static_assert(std::is_trivially_copyable_v<nexigen::chrono::Timestamp>);

using namespace nexigen::core;   // test TU: exercise the error model unqualified
using namespace nexigen::chrono; // test TU: exercise the API unqualified
namespace core = nexigen::core;  // also allow explicit core:: qualification in fixtures

// Loader-produced Settings have no from_json; pin that contract.
template <class T, class = void> struct has_from_json : std::false_type {};
template <class T>
struct has_from_json<
    T, std::void_t<decltype(from_json(std::declval<const nlohmann::json&>(), std::declval<T&>()))>>
    : std::true_type {};

static_assert(!has_from_json<Settings>::value);

namespace {

int g_checks = 0;
int g_failures = 0;

// Allocation detector (перенесён из core_test.cpp): the C++ free store is
// re-backed by a fixed static bump arena. Any dynamic allocation anywhere in
// the process bumps g_alloc_used — hot-path code must leave it untouched.
// This is the harness's measurement hook, not a use of dynamic allocation
// itself.
alignas(16) unsigned char g_arena[4 << 20]{};
std::size_t g_alloc_used = 0;

} // namespace

[[noreturn]] void arena_exhausted() {
    std::abort();
}

void* operator new(std::size_t size) {
    const std::size_t rounded = (size + 15U) & ~static_cast<std::size_t>(15U);
    if (g_alloc_used + rounded > sizeof(g_arena)) {
        arena_exhausted();
    }
    void* p = g_arena + g_alloc_used;
    g_alloc_used += rounded;
    return p;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* ptr) noexcept {
    static_cast<void>(ptr);
}
void operator delete(void* ptr, std::size_t) noexcept {
    static_cast<void>(ptr);
}
void operator delete[](void* ptr) noexcept {
    static_cast<void>(ptr);
}
void operator delete[](void* ptr, std::size_t) noexcept {
    static_cast<void>(ptr);
}

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

// A component that declares a non-default clock domain (spec chrono/clock-id
// "Compile-time domain declaration"); the specialization lives at global scope
// because the primary template is a member of nexigen::chrono.
struct BoottimeComponent {};
template <>
inline constexpr nexigen::chrono::ClockId nexigen::chrono::clock_domain<BoottimeComponent> =
    nexigen::chrono::ClockId::boottime;

namespace {

void run_suite(const char* suite_name, void (*body)()) {
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
    for (int i = 0; i < 10'000; ++i) {
        work += static_cast<std::uint64_t>(i);
    }
    static_cast<void>(work);

    const Result<Timestamp> second = Timestamp::now();
    NX_CHECK(second.has_value());
    if (first.has_value() && second.has_value()) {
        NX_CHECK(*second >= *first); // monotonic: never lower than the previous read
        NX_CHECK((*second - *first).nanoseconds() == second->nanoseconds() - first->nanoseconds());
    }

    // Repeated reads allocate nothing: the arena bump-counter (operator new
    // hook) must not move across 1000 now() calls. Clock failure is non-fatal:
    // every iteration returns (no abort) with the Result engaged.
    const std::size_t arena_before = g_alloc_used;
    int engaged_reads = 0;
    for (int i = 0; i < 1000; ++i) {
        const Result<Timestamp> r = Timestamp::now();
        if (r.has_value()) {
            ++engaged_reads;
        }
    }
    NX_CHECK(engaged_reads == 1000);
    NX_CHECK(g_alloc_used == arena_before);
}

// --- stamp suite (moved from tests/logging/logging_test.cpp; task 3.2 /
// logging design D6 / spec chrono/stamp-source).
void test_stamp() {
    StampSource src{};
    NX_CHECK(src.calibrate().has_value());

    // Non-decreasing per source object (spec scenario "Stamps are
    // non-decreasing per source object").
    Timestamp prev = src.sample();
    int regressions = 0;
    for (int i = 0; i < 1000; ++i) {
        const Timestamp t = src.sample();
        if (t < prev) {
            ++regressions;
        }
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
    StampSource other{};
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
    for (int i = 0; i < 1000; ++i) {
        spin += static_cast<std::uint64_t>(i);
    }
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

    const CivilTime ct = civil_from_utc_ns(1'784'850'120'321'841'133LL);
    NX_CHECK(ct.year == 2026 && ct.month == 7 && ct.day == 23);
    NX_CHECK(ct.hour == 23 && ct.minute == 42 && ct.second == 0);
    NX_CHECK(ct.nanosecond == 321'841'133U);

    // Every rendered timestamp has the exact field layout (30 visible chars).
    for (const std::int64_t probe : {-1LL, 0LL, 999'999'999LL, 1'784'851'199'999'999'999LL}) {
        format_utc_iso8601_ns(probe, buf);
        NX_CHECK(std::strlen(buf) == 30);
    }

    // A negative civil year keeps its sign instead of zero-padding.
    CivilTime pre{};
    pre.year = -1;
    pre.month = 1;
    pre.day = 1;
    NX_CHECK(pre.to_string().find("-1-01-01") != std::string::npos);
}

// --- diagnostics suite (change add-serialization-and-wait-free-logging, task
// 3.2): universal to_string() const + free helper for Timestamp, StampSource,
// CivilTime. Cold-path strings land in the bump arena; member==free always.

void test_diag_strings() {
    // Timestamp: ns count rendered (sec.ns9) and exact.
    const Timestamp ts{1'000'000'001ULL};
    const std::string ts_dump = ts.to_string();
    NX_CHECK(ts_dump.find("1.000000001") != std::string::npos);
    NX_CHECK(ts_dump.find("monotonic") != std::string::npos);
    NX_CHECK(to_string(ts) == ts_dump); // free == member
    NX_CHECK(Timestamp{0}.to_string().find("0.000000000") != std::string::npos);
    // A full nine-digit fraction skips the left-pad branch.
    NX_CHECK(Timestamp{123'456'789ULL}.to_string().find(".123456789") != std::string::npos);

    // StampSource: uncalibrated state is identifiable before calibrate().
    StampSource src{};
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

    // CivilTime: full field decomposition renders.
    const CivilTime ct = civil_from_utc_ns(1'784'850'120'321'841'133LL);
    const std::string ct_dump = ct.to_string();
    NX_CHECK(ct_dump.find("2026-07-23") != std::string::npos);
    NX_CHECK(ct_dump.find("23:42:00") != std::string::npos);
    NX_CHECK(ct_dump.find("321841133") != std::string::npos);
    NX_CHECK(ct_dump.find("UTC") != std::string::npos);
    NX_CHECK(to_string(ct) == ct_dump);

    std::printf("diag_strings: Timestamp/StampSource/CivilTime dumps verified\n");
}

// --- clock service suites (change add-chrono-clock-service): ClockId,
// Snapshot, sinks, ChronoProvider, Settings.

[[nodiscard]] bool write_file(const char* path, std::string_view content) {
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }
    const ssize_t written = ::write(fd, content.data(), content.size());
    ::close(fd);
    return written == static_cast<ssize_t>(content.size());
}

[[nodiscard]] std::string read_all(const char* path) {
    std::string out;
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return out;
    }
    char buf[256];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
        out.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);
    return out;
}

/// Records every snapshot and the worker's observed affinity at delivery.
struct RecordingSink {
    std::vector<Snapshot> snapshots{};
    int expected_cpu{-1};
    std::vector<bool> affinity_ok{};
    Status snapshot_status{};
    Status flush_status{};
    int flushes{0};

    Status on_snapshot(const Snapshot& snapshot) noexcept {
        snapshots.push_back(snapshot);
        cpu_set_t set;
        CPU_ZERO(&set);
        affinity_ok.push_back(
            ::pthread_getaffinity_np(::pthread_self(), sizeof(set), &set) == 0 &&
            (expected_cpu < 0 ||
             (CPU_COUNT(&set) == 1 && CPU_ISSET(static_cast<std::size_t>(expected_cpu), &set))));
        return snapshot_status;
    }
    Status flush() noexcept {
        ++flushes;
        return flush_status;
    }
};

struct CountSink {
    int count{0};
    Status on_snapshot(const Snapshot&) noexcept {
        ++count;
        return {};
    }
    Status flush() noexcept { return {}; }
    std::string to_string() const { return "CountSink(count=" + std::to_string(count) + ")"; }
};

struct FailSink {
    Status on_snapshot(const Snapshot&) noexcept { return std::unexpected(ErrorCode::SystemError); }
    Status flush() noexcept { return std::unexpected(ErrorCode::SystemError); }
    std::string to_string() const { return "FailSink()"; }
};

/// Records the ClockLogLevel diagnostics a worker emits through a logger.
struct RecordingClockLogger {
    std::vector<std::pair<ClockLogLevel, std::string>> messages{};
    void log(ClockLogLevel level, std::string_view message) noexcept {
        messages.push_back({level, std::string(message)});
    }
};

void test_clock_id() {
    // Three base domains only.
    NX_CHECK(to_string(ClockId::monotonic_raw) == "monotonic_raw");
    NX_CHECK(to_string(ClockId::boottime) == "boottime");
    NX_CHECK(to_string(ClockId::realtime) == "realtime");

    // to_string / from_string round-trip for every value.
    for (const ClockId id : {ClockId::monotonic_raw, ClockId::boottime, ClockId::realtime}) {
        ClockId back{};
        NX_CHECK(from_string(to_string(id), back));
        NX_CHECK(back == id);
    }

    // Unknown spelling leaves the out-parameter unchanged.
    ClockId out = ClockId::realtime;
    NX_CHECK(!from_string("bogus", out));
    NX_CHECK(out == ClockId::realtime);
    NX_CHECK(!from_string("Unknown", out)); // "Unknown" is a rendering, not an enumerator

    // JSON round-trip and non-string rejection under JSON_NOEXCEPTION=1.
    for (const ClockId id : {ClockId::monotonic_raw, ClockId::boottime, ClockId::realtime}) {
        nlohmann::json j;
        to_json(j, id);
        NX_CHECK(j.is_string());
        ClockId back{};
        from_json(j, back);
        NX_CHECK(back == id);
    }
    ClockId untouched = ClockId::realtime;
    from_json(nlohmann::json{42}, untouched);
    NX_CHECK(untouched == ClockId::realtime);

    // Formatter matches to_string.
    NX_CHECK(std::format("{}", ClockId::monotonic_raw) ==
             std::string(to_string(ClockId::monotonic_raw)));

    // Compile-time domain declaration: default is monotonic_raw; a component
    // may specialize (BoottimeComponent declares boottime at global scope).
    NX_CHECK(clock_domain<int> == ClockId::monotonic_raw);
    NX_CHECK(clock_domain<BoottimeComponent> == ClockId::boottime);
}

void test_enum_contracts() {
    // Rendering an out-of-range enumerator is well-defined ("Unknown").
    NX_CHECK(to_string(static_cast<ClockId>(0x7f)) == "Unknown");

    // core::SettingsSource: all values round-trip; unknown spellings are
    // rejected without touching the out-parameter.
    for (const core::SettingsSource src :
         {core::SettingsSource::Global, core::SettingsSource::Local,
          core::SettingsSource::Environment, core::SettingsSource::Computed}) {
        core::SettingsSource back{};
        NX_CHECK(from_string(to_string(src), back));
        NX_CHECK(back == src);
    }
    NX_CHECK(to_string(core::SettingsSource::Environment) == "environment");
    NX_CHECK(to_string(core::SettingsSource::Computed) == "computed");
    NX_CHECK(to_string(static_cast<core::SettingsSource>(0x7f)) == "Unknown");
    core::SettingsSource source = core::SettingsSource::Local;
    NX_CHECK(!from_string("bogus", source));
    NX_CHECK(source == core::SettingsSource::Local);

    // JSON source: strings round-trip; non-strings are ignored.
    {
        nlohmann::json j = "environment";
        core::SettingsSource parsed = core::SettingsSource::Global;
        from_json(j, parsed);
        NX_CHECK(parsed == core::SettingsSource::Environment);
        to_json(j, parsed);
        NX_CHECK(j == "environment");
        from_json(nlohmann::json{42}, parsed);
        NX_CHECK(parsed == core::SettingsSource::Environment);
        NX_CHECK(std::format("{}", parsed) == "environment");
    }

    // ClockLogLevel: every name, unknown handling, JSON trio and formatter.
    for (const ClockLogLevel level :
         {ClockLogLevel::info, ClockLogLevel::warning, ClockLogLevel::error}) {
        ClockLogLevel back{};
        NX_CHECK(from_string(to_string(level), back));
        NX_CHECK(back == level);
        nlohmann::json j;
        to_json(j, level);
        NX_CHECK(j == std::string(to_string(level)));
        ClockLogLevel parsed = ClockLogLevel::info;
        from_json(j, parsed);
        NX_CHECK(parsed == level);
    }
    ClockLogLevel level = ClockLogLevel::info;
    NX_CHECK(!from_string("bogus", level));
    NX_CHECK(level == ClockLogLevel::info);
    from_json(nlohmann::json{42}, level);
    NX_CHECK(level == ClockLogLevel::info);
    NX_CHECK(to_string(static_cast<ClockLogLevel>(0x7f)) == "Unknown");
    NX_CHECK(std::format("{}", ClockLogLevel::error) == "error");
}

void test_snapshot() {
    // Register-sized, trivially copyable.
    static_assert(sizeof(Snapshot) == 16);
    static_assert(std::is_trivially_copyable_v<Snapshot>);

    // Construct / read-back.
    const Snapshot original{Timestamp{1'000'000'000ULL}, 42};
    NX_CHECK(original.stamp.nanoseconds() == 1'000'000'000ULL);
    NX_CHECK(original.realtime_offset_ns == 42);

    // Copy preserves the value.
    const Snapshot copy = original;
    NX_CHECK(copy.stamp == original.stamp);
    NX_CHECK(copy.realtime_offset_ns == original.realtime_offset_ns);

    // Re-binding: stamp + offset reproduces wall-clock time.
    NX_CHECK(static_cast<std::int64_t>(original.stamp.nanoseconds()) +
                 original.realtime_offset_ns ==
             1'000'000'042LL);

    // Serialization round-trips, including a negative offset.
    const Snapshot negative{Timestamp{5ULL}, -7};
    nlohmann::json j;
    to_json(j, negative);
    Snapshot back{};
    from_json(j, back);
    NX_CHECK(back.stamp == negative.stamp);
    NX_CHECK(back.realtime_offset_ns == -7);

    // to_string member == free; formatter == to_string.
    const std::string text = original.to_string();
    NX_CHECK(text.find("stamp_ns") != std::string::npos);
    NX_CHECK(text.find("realtime_offset_ns") != std::string::npos);
    NX_CHECK(to_string(original) == text);
    NX_CHECK(std::format("{}", original) == text);
}

void test_snapshot_edges() {
    // from_json: non-object and mistyped members leave the snapshot unchanged;
    // absent members preserve the existing field.
    Snapshot out{Timestamp{7ULL}, -9};
    from_json(nlohmann::json::array(), out);
    NX_CHECK(out.stamp.nanoseconds() == 7ULL);
    NX_CHECK(out.realtime_offset_ns == -9);

    from_json(nlohmann::json{{"stamp_ns", "nope"}, {"realtime_offset_ns", "nope"}}, out);
    NX_CHECK(out.stamp.nanoseconds() == 7ULL);
    NX_CHECK(out.realtime_offset_ns == -9);

    from_json(nlohmann::json{{"stamp_ns", 11ULL}}, out);
    NX_CHECK(out.stamp.nanoseconds() == 11ULL);
    NX_CHECK(out.realtime_offset_ns == -9); // absent offset is preserved

    from_json(nlohmann::json{{"realtime_offset_ns", 12}}, out);
    NX_CHECK(out.stamp.nanoseconds() == 11ULL);
    NX_CHECK(out.realtime_offset_ns == 12);
}

void test_sinks() {
    // Concept membership (compile-time).
    static_assert(concepts::ChronoSink<NullChronoSink>);
    static_assert(concepts::ChronoSink<ConsoleChronoSink>);
    static_assert(concepts::ChronoSink<JsonFileChronoSink>);
    static_assert(concepts::ChronoSink<CompositeChronoSink<>>);

    // Null sink: no-op, no allocation, diagnostics contract.
    {
        NullChronoSink sink;
        NX_CHECK(sink.on_snapshot(Snapshot{}).has_value());
        NX_CHECK(sink.flush().has_value());
        NX_CHECK(sink.to_string() == "NullChronoSink()");
        NX_CHECK(to_string(sink) == sink.to_string());
        nlohmann::json j;
        to_json(j, sink);
        NullChronoSink back{};
        from_json(j, back);
        NX_CHECK(std::format("{}", sink) == sink.to_string());
    }

    // Console sink: render + write to a pipe; write-error reporting.
    {
        int fds[2];
        NX_CHECK(::pipe(fds) == 0);
        {
            ConsoleChronoSink sink{fds[1]};
            NX_CHECK(sink.on_snapshot(Snapshot{Timestamp{1000ULL}, 500}).has_value());
            NX_CHECK(sink.flush().has_value());
            NX_CHECK(sink.descriptor() == fds[1]);
            NX_CHECK(sink.to_string().find("fd=") != std::string::npos);
        }
        ::close(fds[1]);
        char buf[128]{};
        const ssize_t got = ::read(fds[0], buf, sizeof(buf) - 1);
        ::close(fds[0]);
        NX_CHECK(got > 0);
        const std::string_view text(buf, static_cast<std::size_t>(got));
        NX_CHECK(text.find("stamp_ns=1000") != std::string_view::npos);
        NX_CHECK(text.find("realtime_offset_ns=500") != std::string_view::npos);
    }
    {
        ConsoleChronoSink bad{-1};
        NX_CHECK(!bad.on_snapshot(Snapshot{}).has_value());
    }

    // JSON-file sink: JSON-lines output, append, flush, config round-trip,
    // open/write failure surfacing.
    char tmpl[] = "/tmp/nexigen_chrono_sink_XXXXXX";
    const int mfd = ::mkstemp(tmpl);
    NX_CHECK(mfd >= 0);
    ::close(mfd);
    ::unlink(tmpl); // the sink re-creates it
    {
        JsonFileChronoSinkConfig config;
        config.path = tmpl;
        config.append = true;
        JsonFileChronoSink sink{config};
        NX_CHECK(sink.is_open());
        NX_CHECK(sink.on_snapshot(Snapshot{Timestamp{111ULL}, 222}).has_value());
        NX_CHECK(sink.on_snapshot(Snapshot{Timestamp{333ULL}, -444}).has_value());
        NX_CHECK(sink.flush().has_value());

        // Config serialization round-trip.
        nlohmann::json j;
        to_json(j, config);
        JsonFileChronoSinkConfig back{};
        from_json(j, back);
        NX_CHECK(back.path == config.path);
        NX_CHECK(back.append == config.append);
        NX_CHECK(std::format("{}", config).find("path=") != std::string::npos);
    }
    {
        const std::string content = read_all(tmpl);
        // Two independent JSON lines.
        std::size_t lines = 0;
        std::size_t pos = 0;
        bool stamp_ok = false;
        bool negative_ok = false;
        while (pos < content.size()) {
            const std::size_t nl = content.find('\n', pos);
            const std::string_view line(content.data() + pos,
                                        (nl == std::string::npos ? content.size() : nl) - pos);
            if (!line.empty()) {
                const nlohmann::json parsed =
                    nlohmann::json::parse(std::string(line), nullptr, false);
                NX_CHECK(!parsed.is_discarded());
                if (!parsed.is_discarded() && parsed.is_object()) {
                    if (parsed.value("stamp_ns", 0ULL) == 111ULL) {
                        stamp_ok = parsed.value("realtime_offset_ns", 0LL) == 222LL;
                    }
                    if (parsed.value("stamp_ns", 0ULL) == 333ULL) {
                        negative_ok = parsed.value("realtime_offset_ns", 0LL) == -444LL;
                    }
                }
                ++lines;
            }
            if (nl == std::string::npos) {
                break;
            }
            pos = nl + 1;
        }
        NX_CHECK(lines == 2);
        NX_CHECK(stamp_ok);
        NX_CHECK(negative_ok);
    }
    ::unlink(tmpl);
    {
        JsonFileChronoSinkConfig bad;
        bad.path = "/nonexistent_dir_nexigen/no_file.jsonl";
        JsonFileChronoSink sink{bad};
        NX_CHECK(!sink.is_open());
        NX_CHECK(!sink.on_snapshot(Snapshot{}).has_value());
        NX_CHECK(!sink.flush().has_value());
    }

    // Composite sink: fan-out, first-failure aggregation, typed/indexed access.
    {
        CompositeChronoSink<CountSink, CountSink, CountSink> comp{CountSink{}, CountSink{},
                                                                  CountSink{}};
        NX_CHECK(comp.on_snapshot(Snapshot{}).has_value());
        NX_CHECK(comp.get<0>().count == 1);
        NX_CHECK(comp.get<1>().count == 1);
        NX_CHECK(comp.get<2>().count == 1);
        NX_CHECK(comp.to_string().find("members=3") != std::string::npos);
    }
    {
        // First failure does not starve the rest.
        CompositeChronoSink<FailSink, CountSink, CountSink> comp{FailSink{}, CountSink{},
                                                                 CountSink{}};
        const Status s = comp.on_snapshot(Snapshot{});
        NX_CHECK(!s.has_value());
        NX_CHECK(s.error() == ErrorCode::SystemError);
        NX_CHECK(comp.get<1>().count == 1);
        NX_CHECK(comp.get<2>().count == 1);
        const Status fs = comp.flush();
        NX_CHECK(!fs.has_value());
    }
    {
        CompositeChronoSink<NullChronoSink, CountSink> typed{NullChronoSink{}, CountSink{}};
        NX_CHECK(typed.get<NullChronoSink>().to_string() == "NullChronoSink()");
        NX_CHECK(typed.get<CountSink>().count == 0);
        NX_CHECK(typed.get<1>().count == 0);
    }
}

void test_sink_edges() {
    // Config: append=false rendering, JSON edge cases and formatter.
    {
        JsonFileChronoSinkConfig config;
        config.path = "/tmp/nexigen_chrono_sink_edges.jsonl";
        config.append = false;
        NX_CHECK(config.to_string().find("append=false") != std::string::npos);
        NX_CHECK(std::format("{}", config) == config.to_string());

        JsonFileChronoSinkConfig out;
        out.path = "/keep";
        out.append = true;
        from_json(nlohmann::json::array(), out);
        NX_CHECK(out.path == "/keep" && out.append);
        from_json(nlohmann::json{{"path", 1}, {"append", "yes"}}, out);
        NX_CHECK(out.path == "/keep" && out.append);
        from_json(nlohmann::json{{"path", "/new"}, {"append", false}}, out);
        NX_CHECK(out.path == "/new" && !out.append);
        from_json(nlohmann::json::object(), out);
        NX_CHECK(out.path == "/new" && !out.append);
    }

    // append=false truncates an existing file instead of appending.
    char tmpl[] = "/tmp/nexigen_chrono_trunc_XXXXXX";
    const int mfd = ::mkstemp(tmpl);
    NX_CHECK(mfd >= 0);
    NX_CHECK(write_file(tmpl, "stale-content-that-must-disappear"));
    ::close(mfd);
    {
        JsonFileChronoSinkConfig config;
        config.path = tmpl;
        config.append = false;
        JsonFileChronoSink sink{config};
        NX_CHECK(sink.is_open());
        NX_CHECK(sink.on_snapshot(Snapshot{Timestamp{5ULL}, 6}).has_value());
        NX_CHECK(sink.flush().has_value());
    }
    {
        const std::string content = read_all(tmpl);
        NX_CHECK(content.find("stale") == std::string::npos);
        NX_CHECK(content.find("\"stamp_ns\":5") != std::string::npos);
    }
    ::unlink(tmpl);

    // fsync failure: /dev/null opens but cannot be synced.
    {
        JsonFileChronoSinkConfig config;
        config.path = "/dev/null";
        config.append = false;
        JsonFileChronoSink sink{config};
        NX_CHECK(sink.is_open());
        NX_CHECK(sink.on_snapshot(Snapshot{}).has_value());
        NX_CHECK(!sink.flush().has_value());
    }

    // Write failure: /dev/full reports ENOSPC on every write.
    {
        JsonFileChronoSinkConfig config;
        config.path = "/dev/full";
        config.append = false;
        JsonFileChronoSink sink{config};
        NX_CHECK(sink.is_open());
        NX_CHECK(!sink.on_snapshot(Snapshot{}).has_value());
    }
    {
        const int fd = ::open("/dev/full", O_WRONLY | O_CLOEXEC);
        NX_CHECK(fd >= 0);
        ConsoleChronoSink sink{fd};
        NX_CHECK(!sink.on_snapshot(Snapshot{}).has_value());
        ::close(fd);
    }
}

void test_provider() {
    // Config serialization round-trip + diagnostics.
    {
        ChronoProviderConfig config;
        config.collect_interval = std::chrono::nanoseconds{1'000'000};
        config.worker_thread.set_cpus(core::CpuSet::from_core_mask(0x3));
        nlohmann::json j;
        to_json(j, config);
        NX_CHECK(j["collect_interval_ns"] == 1'000'000);
        NX_CHECK(j["worker_thread"]["cpus"].size() == 2U);
        ChronoProviderConfig back{};
        from_json(j, back);
        NX_CHECK(back.collect_interval == config.collect_interval);
        NX_CHECK(back.worker_thread.cpus() == core::CpuSet::from_core_mask(0x3));
        NX_CHECK(config.to_string().find("worker_thread=") != std::string::npos);
        // Real hex render (P2-17): decimal 10 → `0xa`, never the old
        // "0x<decimal>" artifact.
        {
            ChronoProviderConfig hex_probe{};
            hex_probe.worker_thread.set_cpus(core::CpuSet::from_core_mask(0xA));
            const std::string hex_text = hex_probe.to_string();
            NX_CHECK(hex_text.find("worker_thread=") != std::string::npos);
            NX_CHECK(hex_text.find("core_mask=") == std::string::npos);
        }
        NX_CHECK(std::format("{}", config) == config.to_string());
    }

    // Manual snapshot is deterministic, allocation-free on the steady path,
    // and reports a sane monotonic/offset pair.
    {
        NullChronoSink sink;
        ChronoProviderConfig config{}; // zero interval: no background worker
        ChronoProvider provider{config, sink};
        NX_CHECK(provider.startup_status().has_value());
        const std::size_t before = g_alloc_used;
        for (int i = 0; i < 1000; ++i) {
            NX_CHECK(provider.snapshot_now().has_value());
        }
        NX_CHECK(g_alloc_used == before); // steady-state sampling allocates nothing
    }
    {
        RecordingSink sink;
        ChronoProviderConfig config{};
        ChronoProvider provider{config, sink};
        NX_CHECK(provider.snapshot_now().has_value());
        NX_CHECK(sink.snapshots.size() == 1U);
        NX_CHECK(sink.snapshots[0].stamp.nanoseconds() > 0);
        // Re-binding stamp + offset reproduces the wall clock: positive and
        // within a sane range (after 2017, before 2100).
        const std::int64_t wall = static_cast<std::int64_t>(sink.snapshots[0].stamp.nanoseconds()) +
                                  sink.snapshots[0].realtime_offset_ns;
        NX_CHECK(wall > 1'500'000'000'000'000'000LL);
        NX_CHECK(wall < 4'100'000'000'000'000'000LL);
        NX_CHECK(provider.to_string().find("ChronoProvider") != std::string::npos);
    }

    // Zero interval disables background delivery; destructor drains + flushes.
    {
        RecordingSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{0};
        {
            ChronoProvider provider{config, sink};
            std::this_thread::sleep_for(std::chrono::milliseconds{30});
            NX_CHECK(sink.snapshots.empty());
        } // destructor: one final snapshot + flush
        NX_CHECK(sink.snapshots.size() == 1U);
        NX_CHECK(sink.flushes >= 1);
    }

    // Deferred start: construction does not start the worker; start() is
    // idempotent; a zero interval makes start() a no-op.
    {
        RecordingSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{5'000'000}; // 5 ms
        {
            ChronoProvider provider{config, sink};
            std::this_thread::sleep_for(std::chrono::milliseconds{30});
            NX_CHECK(sink.snapshots.empty()); // not started yet
            NX_CHECK(provider.start().has_value());
            NX_CHECK(provider.start().has_value()); // idempotent
            std::this_thread::sleep_for(std::chrono::milliseconds{30});
        } // destructor: final snapshot + flush
        NX_CHECK(sink.snapshots.size() >= 1U); // worker delivered after start()
    }
    {
        RecordingSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{0}; // zero interval
        {
            ChronoProvider provider{config, sink};
            NX_CHECK(provider.start().has_value()); // no-op
            std::this_thread::sleep_for(std::chrono::milliseconds{30});
            NX_CHECK(sink.snapshots.empty()); // no worker, nothing delivered
        }
        NX_CHECK(sink.snapshots.size() == 1U); // only the destructor's final snapshot
    }

    // Worker logging: start(Logger&) binds a logger; start() runs silent.
    {
        RecordingClockLogger logger;
        NullChronoSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{1'000'000}; // 1 ms
        {
            ChronoProvider provider{config, sink};
            NX_CHECK(provider.start(logger).has_value());
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        } // destructor stops the worker, which logs its shutdown
        NX_CHECK(!logger.messages.empty());
    }
    // Double-start guard (spec chrono/chrono-provider "Optional worker
    // logger", analysis #12): a start(Logger&) on a running provider returns
    // Conflict and does NOT rebind — the first logger keeps its diagnostics.
    {
        RecordingClockLogger first;
        RecordingClockLogger second;
        NullChronoSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{1'000'000};
        {
            ChronoProvider provider{config, sink};
            NX_CHECK(provider.start(first).has_value());
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
            const core::Status rebound = provider.start(second);
            NX_CHECK(rebound.has_value() == false && rebound.error() == core::ErrorCode::Conflict);
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        } // destructor stops the worker
        NX_CHECK(!first.messages.empty());
        NX_CHECK(second.messages.empty()); // never rebound
    }
    {
        RecordingClockLogger logger;
        NullChronoSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{1'000'000};
        {
            ChronoProvider provider{config, sink};
            NX_CHECK(provider.start().has_value()); // no logger
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
        NX_CHECK(logger.messages.empty()); // no logging calls
    }

    // Background worker honors the period.
    {
        RecordingSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{10'000'000}; // 10 ms
        {
            ChronoProvider provider{config, sink};
            NX_CHECK(provider.start().has_value());
            std::this_thread::sleep_for(std::chrono::milliseconds{120});
        } // destructor joins the worker before the sink is read again
        NX_CHECK(sink.snapshots.size() >= 3U);
    }

    // CPU affinity: the worker pins itself; startup_status reports failure for
    // a denied core; zero mask inherits process affinity.
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    NX_CHECK(::sched_getaffinity(0, sizeof(allowed), &allowed) == 0);
    int first_cpu = -1;
    int denied_cpu = -1;
    for (int cpu = 0; cpu < 64; ++cpu) {
        if (CPU_ISSET(static_cast<std::size_t>(cpu), &allowed)) {
            if (first_cpu < 0) {
                first_cpu = cpu;
            }
        } else if (denied_cpu < 0) {
            denied_cpu = cpu;
        }
    }
    NX_CHECK(first_cpu >= 0);
    {
        RecordingSink sink;
        sink.expected_cpu = first_cpu;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{1'000'000}; // 1 ms
        config.worker_thread.set_cpus(core::CpuSet::from_core_mask(std::uint64_t{1} << first_cpu));
        {
            ChronoProvider provider{config, sink};
            NX_CHECK(provider.startup_status().has_value());
            NX_CHECK(provider.start().has_value());
            for (int i = 0; i < 200 && provider.worker_mask() == 0ULL; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            NX_CHECK(provider.worker_mask() == (std::uint64_t{1} << first_cpu));
            NX_CHECK((provider.worker_thread_report().applied() &
                      core::ThreadApplyReport::affinity_mask) != 0U);
            NX_CHECK(provider.worker_thread_report().failed() == 0U);
            std::this_thread::sleep_for(std::chrono::milliseconds{30});
        }
        // Every background snapshot ran on the pinned core (the final drain on
        // the caller thread is excluded).
        bool affinity_ok = sink.affinity_ok.size() > 1U;
        for (std::size_t i = 0; i + 1U < sink.affinity_ok.size(); ++i) {
            affinity_ok = affinity_ok && sink.affinity_ok[i];
        }
        NX_CHECK(affinity_ok);
    }
    if (denied_cpu >= 0) {
        RecordingSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{1'000'000};
        config.worker_thread.set_cpus(core::CpuSet::from_core_mask(std::uint64_t{1} << denied_cpu));
        ChronoProvider provider{config, sink};
        NX_CHECK(provider.start().has_value());
        for (int i = 0; i < 200 && provider.startup_status().has_value(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        const Status startup = provider.startup_status();
        NX_CHECK(!startup.has_value());
        NX_CHECK(startup.error() == ErrorCode::SystemError);
    }
    {
        RecordingSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{0};
        config.worker_thread = core::ThreadSpec{};
        ChronoProvider provider{config, sink};
        NX_CHECK(provider.startup_status().has_value());
    }

    // Control plane: flush-failure propagation and usability after a failed
    // snapshot.
    {
        RecordingSink sink;
        sink.snapshot_status = std::unexpected(ErrorCode::SystemError);
        sink.flush_status = std::unexpected(ErrorCode::SystemError);
        ChronoProviderConfig config{};
        ChronoProvider provider{config, sink};
        const Status snap = provider.snapshot_now();
        NX_CHECK(!snap.has_value());
        NX_CHECK(snap.error() == ErrorCode::SystemError);
        const Status fl = provider.flush();
        NX_CHECK(!fl.has_value());
        // Still usable after failure: reset the sink status and retry.
        sink.snapshot_status = {};
        NX_CHECK(provider.snapshot_now().has_value());
    }

    // Worker delivery failures are logged as warnings.
    {
        RecordingClockLogger logger;
        FailSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{1'000'000};
        {
            ChronoProvider provider{config, sink};
            NX_CHECK(provider.start(logger).has_value());
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
        bool warned = false;
        for (const auto& [level, message] : logger.messages) {
            if (level == ClockLogLevel::warning &&
                message.find("delivery failed") != std::string::npos) {
                warned = true;
            }
        }
        NX_CHECK(warned);
    }

    // A sink slower than the period rebases the schedule instead of chasing
    // the backlog.
    {
        struct SlowSink {
            std::atomic<int> count{0};
            Status on_snapshot(const Snapshot&) noexcept {
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
                count.fetch_add(1, std::memory_order_relaxed);
                return {};
            }
            Status flush() noexcept { return {}; }
            std::string to_string() const { return "SlowSink()"; }
        };
        SlowSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{1};
        {
            ChronoProvider provider{config, sink};
            NX_CHECK(provider.start().has_value());
            std::this_thread::sleep_for(std::chrono::milliseconds{30});
        }
        NX_CHECK(sink.count.load(std::memory_order_relaxed) >= 2);
    }
}

void test_provider_config_edges() {
    ChronoProviderConfig config;
    config.collect_interval = std::chrono::nanoseconds{5};
    config.worker_thread.set_cpus(core::CpuSet::from_core_mask(7));

    // Non-object and empty-object documents leave the config untouched.
    from_json(nlohmann::json::array(), config);
    NX_CHECK(config.collect_interval == std::chrono::nanoseconds{5});
    NX_CHECK(config.worker_thread.cpus() == core::CpuSet::from_core_mask(7));
    from_json(nlohmann::json::object(), config);
    NX_CHECK(config.collect_interval == std::chrono::nanoseconds{5});
    NX_CHECK(config.worker_thread.cpus() == core::CpuSet::from_core_mask(7));

    // Mistyped and negative members are ignored individually.
    from_json(nlohmann::json{{"collect_interval_ns", -1}, {"core_mask", "bad"}}, config);
    NX_CHECK(config.collect_interval == std::chrono::nanoseconds{5});
    NX_CHECK(config.worker_thread.cpus() == core::CpuSet::from_core_mask(7));
    from_json(nlohmann::json{{"collect_interval_ns", "bad"}}, config);
    NX_CHECK(config.collect_interval == std::chrono::nanoseconds{5});
    from_json(nlohmann::json{{"core_mask", -1}}, config);
    NX_CHECK(config.worker_thread.cpus() == core::CpuSet::from_core_mask(7));

    // Valid members replace the previous values.
    from_json(nlohmann::json{{"collect_interval_ns", 9}, {"core_mask", 3ULL}}, config);
    NX_CHECK(config.collect_interval == std::chrono::nanoseconds{9});
    NX_CHECK(config.worker_thread.cpus() == core::CpuSet::from_core_mask(3));
}

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
bool chrono_fail_point(nexigen::core::fault_injection::FaultPoint point) noexcept {
    return point == nexigen::core::fault_injection::FaultPoint::Thread_set_affinity ||
           point == nexigen::core::fault_injection::FaultPoint::Thread_set_scheduler;
}
#endif

void test_chrono_required_thread_failure() {
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    NX_CHECK(::sched_getaffinity(0, sizeof(allowed), &allowed) == 0);
    int first_cpu = -1;
    for (int cpu = 0; cpu < 64 && first_cpu < 0; ++cpu) {
        if (CPU_ISSET(static_cast<std::size_t>(cpu), &allowed)) {
            first_cpu = cpu;
        }
    }
    NX_CHECK(first_cpu >= 0);

    // Required failure joins the worker; the same provider can start again.
    {
        RecordingSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{1'000'000};
        config.worker_thread.set_cpus(core::CpuSet::from_core_mask(std::uint64_t{1} << first_cpu));
        NX_CHECK(config.worker_thread.set_name("chrono-worker").has_value());
        config.worker_thread.set_apply_mode(core::ThreadApplyMode::Required);
        ChronoProvider provider{config, sink};
        nexigen::core::fault_injection::set_fault_hook(chrono_fail_point);
        const Status failed = provider.start();
        nexigen::core::fault_injection::set_fault_hook(nullptr);
        NX_CHECK(!failed.has_value());
        NX_CHECK(failed.error() == ErrorCode::SystemError);
        NX_CHECK(!provider.startup_status().has_value());
        NX_CHECK(provider.worker_thread_report().failed() ==
                 core::ThreadApplyReport::affinity_mask);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        NX_CHECK(sink.snapshots.empty());

        NX_CHECK(provider.start().has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        NX_CHECK(!sink.snapshots.empty());
    }

    // Best effort exposes a scheduler failure but keeps snapshots flowing.
    {
        RecordingSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{1'000'000};
        NX_CHECK(config.worker_thread.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
        config.worker_thread.set_apply_mode(core::ThreadApplyMode::BestEffort);
        ChronoProvider provider{config, sink};
        nexigen::core::fault_injection::set_fault_hook(chrono_fail_point);
        NX_CHECK(provider.start().has_value());
        for (int i = 0; i < 100 && provider.startup_status().has_value(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        nexigen::core::fault_injection::set_fault_hook(nullptr);
        NX_CHECK(!provider.startup_status().has_value());
        NX_CHECK(provider.worker_thread_report().failed() ==
                 core::ThreadApplyReport::scheduler_mask);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        NX_CHECK(!sink.snapshots.empty());
    }

    // Zero interval skips worker creation and attribute application.
    {
        RecordingSink sink;
        ChronoProviderConfig config{};
        config.collect_interval = std::chrono::nanoseconds{0};
        NX_CHECK(config.worker_thread.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
        config.worker_thread.set_apply_mode(core::ThreadApplyMode::Required);
        ChronoProvider provider{config, sink};
        nexigen::core::fault_injection::set_fault_hook(chrono_fail_point);
        NX_CHECK(provider.start().has_value());
        nexigen::core::fault_injection::set_fault_hook(nullptr);
        NX_CHECK(provider.worker_thread_report().requested() == 0U);
        NX_CHECK(sink.snapshots.empty());
    }
#else
    std::printf("chrono_required_thread_failure: fault injection disabled; skipped\n");
#endif
}

void test_affinity_edges() {
    // Zero mask is rejected before touching the thread affinity.
    const Status zero = detail::pin_self(0);
    NX_CHECK(!zero.has_value());
    NX_CHECK(zero.error() == ErrorCode::InvalidArgument);

    // self_affinity reports the current mask.
    std::uint64_t mask = 0;
    NX_CHECK(detail::self_affinity(mask).has_value());
    NX_CHECK(mask != 0ULL);

    // Pinning to a core outside the process's allowed set fails. Some
    // containers expose every CPU 0..63, in which case the failure path is
    // simply not observable; skip the assertion rather than fake it.
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    NX_CHECK(::sched_getaffinity(0, sizeof(allowed), &allowed) == 0);
    int denied = -1;
    for (int cpu = 0; cpu < 64; ++cpu) {
        if (!CPU_ISSET(static_cast<std::size_t>(cpu), &allowed)) {
            denied = cpu;
            break;
        }
    }
    if (denied >= 0) {
        std::atomic<int> result{-1};
        std::thread worker([denied, &result] {
            const Status pinned = detail::pin_self(std::uint64_t{1} << denied);
            result.store(pinned.has_value() ? 0 : static_cast<int>(pinned.error()),
                         std::memory_order_relaxed);
        });
        worker.join();
        NX_CHECK(result.load() == static_cast<int>(ErrorCode::SystemError));
    }
}

void test_chrono_worker_thread_settings() {
    char global_path[256];
    char local_path[256];
    std::snprintf(global_path, sizeof global_path, "/tmp/opencode/chrono-worker-global-%d.json",
                  ::getpid());
    std::snprintf(local_path, sizeof local_path, "/tmp/opencode/chrono-worker-local-%d.json",
                  ::getpid());
    NX_CHECK(write_file(global_path, R"({"collect_interval_ns":1000000,"core_mask":5,)"
                                     R"("worker_thread":{"name":"chrono-worker","policy":"rr",)"
                                     R"("priority":40,"mode":"required"}})"));
    NX_CHECK(write_file(local_path, R"({"worker_thread":{"cpus":[64,65]}})"));

    Settings merged{};
    NX_CHECK(load_chrono_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(merged.options.worker_thread.name() == "chrono-worker");
    NX_CHECK(merged.options.worker_thread.policy() == core::SchedulingPolicy::RR);
    NX_CHECK(merged.options.worker_thread.priority() == 40);
    NX_CHECK(merged.options.worker_thread.apply_mode() == core::ThreadApplyMode::Required);
    NX_CHECK(merged.options.worker_thread.cpus().contains(64U));
    NX_CHECK(merged.options.worker_thread.cpus().contains(65U));
    NX_CHECK(merged.options.worker_thread.cpus().count() == 2U);
    ChronoProviderConfig config{};
    NX_CHECK(apply_to(merged, config).has_value());
    NX_CHECK(config.collect_interval == std::chrono::nanoseconds{1'000'000});
    NX_CHECK(config.worker_thread == merged.options.worker_thread);

    NX_CHECK(write_file(local_path, R"({"worker_thread":{"cpus":[]}})"));
    NX_CHECK(load_chrono_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(merged.options.worker_thread.cpus().empty());
    NX_CHECK(merged.options.worker_thread.name() == "chrono-worker");

    NX_CHECK(::setenv("NEXIGEN_CHRONO_CORE_MASK", "3", 1) == 0);
    NX_CHECK(load_chrono_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(merged.options.worker_thread.cpus() == core::CpuSet::from_core_mask(3));
    NX_CHECK(merged.options.worker_thread.name() == "chrono-worker");
    NX_CHECK(::unsetenv("NEXIGEN_CHRONO_CORE_MASK") == 0);

    nlohmann::json json;
    to_json(json, merged);
    NX_CHECK(json.contains("worker_thread"));
    NX_CHECK(!json.contains("core_mask"));
    NX_CHECK(json["worker_thread"]["name"] == "chrono-worker");

    NX_CHECK(write_file(local_path, R"({"worker_thread":{"cpus":[64,64]}})"));
    NX_CHECK(!load_chrono_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(write_file(local_path, R"({"worker_thread":{"policy":"fifo","priority":0}})"));
    NX_CHECK(!load_chrono_settings({global_path, local_path}, merged).has_value());

    static_cast<void>(std::remove(global_path));
    static_cast<void>(std::remove(local_path));
}

void test_settings() {
    const char* global_path = "/tmp/nexigen_chrono_global.json";
    const char* local_path = "/tmp/nexigen_chrono_local.json";

    // Both sources load; local overrides global; absent local inherits global.
    NX_CHECK(write_file(global_path, "{\"collect_interval_ns\": 1000000, \"core_mask\": 3}"));
    NX_CHECK(write_file(local_path, "{\"collect_interval_ns\": 2000000}"));
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        paths.local_file = local_path;
        NX_CHECK(load_chrono_settings(paths, settings).has_value());
        NX_CHECK(settings.global_loaded && settings.local_loaded);
        NX_CHECK(settings.options.collect_interval == std::chrono::nanoseconds{2000000});
        NX_CHECK(settings.options.worker_thread.cpus() == core::CpuSet::from_core_mask(0x3));
        NX_CHECK(settings.diagnostics.empty());
    }

    // Environment has highest priority.
    ::setenv("NEXIGEN_CHRONO_CORE_MASK", "5", 1);
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        paths.local_file = local_path;
        NX_CHECK(load_chrono_settings(paths, settings).has_value());
        NX_CHECK(settings.options.worker_thread.cpus() == core::CpuSet::from_core_mask(5));
    }
    ::unsetenv("NEXIGEN_CHRONO_CORE_MASK");

    // Malformed local JSON: InvalidArgument, global still applies, diagnostic.
    NX_CHECK(write_file(local_path, "{ not json"));
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        paths.local_file = local_path;
        const Status s = load_chrono_settings(paths, settings);
        NX_CHECK(!s.has_value());
        NX_CHECK(s.error() == ErrorCode::InvalidArgument);
        NX_CHECK(!settings.diagnostics.empty());
        NX_CHECK(settings.options.collect_interval == std::chrono::nanoseconds{1000000});
    }

    // Invalid value keeps the previous valid value.
    NX_CHECK(write_file(local_path, "{\"collect_interval_ns\": \"abc\"}"));
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        paths.local_file = local_path;
        const Status s = load_chrono_settings(paths, settings);
        NX_CHECK(!s.has_value());
        NX_CHECK(settings.options.collect_interval == std::chrono::nanoseconds{1000000});
        // Pin the exact shared-vocabulary text (dedup-sink-plumbing): the
        // diagnostic must stay byte-identical after the consolidation.
        NX_CHECK(settings.diagnostics.size() == 1U);
        NX_CHECK(settings.diagnostics[0].field == "collect_interval_ns");
        NX_CHECK(settings.diagnostics[0].message == "expected an integer in range");
        NX_CHECK(settings.diagnostics[0].source == core::SettingsSource::Local);
    }

    // Empty paths yield defaults with no diagnostics.
    {
        Settings settings;
        core::SettingsPaths paths;
        NX_CHECK(load_chrono_settings(paths, settings).has_value());
        NX_CHECK(!settings.global_loaded && !settings.local_loaded);
        NX_CHECK(settings.options.collect_interval == std::chrono::nanoseconds{0});
        NX_CHECK(settings.options.worker_thread.cpus().empty());
        NX_CHECK(settings.diagnostics.empty());
    }

    // Missing files are not errors and produce no diagnostics.
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = "/tmp/nexigen_chrono_nonexistent.json";
        paths.local_file = "/tmp/nexigen_chrono_also_nonexistent.json";
        NX_CHECK(load_chrono_settings(paths, settings).has_value());
        NX_CHECK(!settings.global_loaded && !settings.local_loaded);
        NX_CHECK(settings.diagnostics.empty());
    }

    // apply_to maps every field.
    {
        Settings settings;
        settings.options.collect_interval = std::chrono::nanoseconds{3'000'000};
        settings.options.worker_thread.set_cpus(core::CpuSet::from_core_mask(0x7));
        ChronoProviderConfig config;
        NX_CHECK(apply_to(settings, config).has_value());
        NX_CHECK(config.collect_interval == settings.options.collect_interval);
        NX_CHECK(config.worker_thread.cpus() == core::CpuSet::from_core_mask(0x7));
    }

    // Settings and diagnostic serialization.
    {
        Settings settings;
        settings.options.collect_interval = std::chrono::nanoseconds{1};
        settings.options.worker_thread.set_cpus(core::CpuSet::from_core_mask(0x3));
        nlohmann::json j;
        to_json(j, settings);
        NX_CHECK(j["worker_thread"]["cpus"].size() == 2U);
        NX_CHECK(j["collect_interval_ns"] == 1);
        NX_CHECK(std::format("{}", settings) == settings.to_string());

        core::SettingsDiagnostic diag{core::SettingsSource::Local, "core_mask", "bad"};
        nlohmann::json dj;
        to_json(dj, diag);
        core::SettingsDiagnostic back{};
        from_json(dj, back);
        NX_CHECK(back.source == core::SettingsSource::Local);
        NX_CHECK(back.field == "core_mask");
        NX_CHECK(back.message == "bad");

        core::SettingsPaths paths;
        paths.global_file = "/a";
        paths.local_file = "/b";
        nlohmann::json pj;
        to_json(pj, paths);
        core::SettingsPaths pback{};
        from_json(pj, pback);
        NX_CHECK(pback.global_file == "/a" && pback.local_file == "/b");
    }

    ::unlink(global_path);
    ::unlink(local_path);
}

void test_settings_sink() {
    ::unsetenv("NEXIGEN_CHRONO_COLLECT_INTERVAL_NS");
    ::unsetenv("NEXIGEN_CHRONO_CORE_MASK");

    const std::string base =
        "/tmp/opencode/chrono_sink_" + std::to_string(static_cast<long long>(::getpid()));
    const std::string global_path = base + "_global.json";
    const std::string local_path = base + "_local.json";

    // Local path overrides global; absent local append inherits global.
    NX_CHECK(write_file(global_path.c_str(),
                        R"({"collect_interval_ns":1,)"
                        R"("sink":{"path":"/tmp/global-chrono.jsonl","append":false}})"));
    NX_CHECK(write_file(local_path.c_str(), R"({"sink":{"path":"/tmp/local-chrono.jsonl"}})"));
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        paths.local_file = local_path;
        NX_CHECK(load_chrono_settings(paths, settings).has_value());
        NX_CHECK(settings.options.collect_interval == std::chrono::nanoseconds{1});
        NX_CHECK(settings.sink.path == "/tmp/local-chrono.jsonl");
        NX_CHECK(!settings.sink.append);
    }

    // Without a local sink block the global path and append flag are retained.
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        paths.local_file = "";
        NX_CHECK(load_chrono_settings(paths, settings).has_value());
        NX_CHECK(settings.sink.path == "/tmp/global-chrono.jsonl");
        NX_CHECK(!settings.sink.append);
    }

    // Provider environment overrides do not touch the sink.
    ::setenv("NEXIGEN_CHRONO_COLLECT_INTERVAL_NS", "42", 1);
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        paths.local_file = local_path;
        NX_CHECK(load_chrono_settings(paths, settings).has_value());
        NX_CHECK(settings.options.collect_interval == std::chrono::nanoseconds{42});
        NX_CHECK(settings.sink.path == "/tmp/local-chrono.jsonl");
        NX_CHECK(!settings.sink.append);
    }
    ::unsetenv("NEXIGEN_CHRONO_COLLECT_INTERVAL_NS");

    // Serialization carries the sink; apply_to ignores it.
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        paths.local_file = local_path;
        NX_CHECK(load_chrono_settings(paths, settings).has_value());
        NX_CHECK(settings.to_string().find("sink=JsonFileChronoSinkConfig") != std::string::npos);
        NX_CHECK(std::format("{}", settings) == settings.to_string());
        nlohmann::json j;
        to_json(j, settings);
        NX_CHECK(j["sink"]["path"] == "/tmp/local-chrono.jsonl");
        NX_CHECK(j["sink"]["append"] == false);

        ChronoProviderConfig config;
        NX_CHECK(apply_to(settings, config).has_value());
        NX_CHECK(config.collect_interval == settings.options.collect_interval);
        NX_CHECK(config.worker_thread == settings.options.worker_thread);
    }

    // Absent sink blocks leave the sink unconfigured.
    NX_CHECK(write_file(global_path.c_str(), "{}"));
    NX_CHECK(write_file(local_path.c_str(), "{}"));
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        paths.local_file = local_path;
        NX_CHECK(load_chrono_settings(paths, settings).has_value());
        NX_CHECK(settings.sink.path.empty());
        NX_CHECK(settings.sink.append);
        NX_CHECK(settings.diagnostics.empty());
    }

    // An empty path is accepted as unconfigured, not a diagnostic.
    NX_CHECK(write_file(global_path.c_str(), R"({"sink":{"path":""}})"));
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        NX_CHECK(load_chrono_settings(paths, settings).has_value());
        NX_CHECK(settings.sink.path.empty());
        NX_CHECK(settings.diagnostics.empty());
    }

    // Wrong field types keep the previous value and name sink.<field>; the
    // shared-vocabulary texts stay pinned byte-identical (dedup-sink-plumbing).
    NX_CHECK(write_file(global_path.c_str(), R"({"sink":{"path":"/tmp/keep-chrono.jsonl"}})"));
    NX_CHECK(write_file(local_path.c_str(), R"({"sink":{"path":3,"append":3}})"));
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        paths.local_file = local_path;
        NX_CHECK(!load_chrono_settings(paths, settings).has_value());
        NX_CHECK(settings.sink.path == "/tmp/keep-chrono.jsonl");
        NX_CHECK(settings.sink.append);
        NX_CHECK(settings.diagnostics.size() == 2U);
        NX_CHECK(settings.diagnostics[0].field == "sink.path");
        NX_CHECK(settings.diagnostics[0].message == "expected a string");
        NX_CHECK(settings.diagnostics[1].field == "sink.append");
        NX_CHECK(settings.diagnostics[1].message == "expected a boolean");
    }

    // A non-object sink keeps the whole previous sink and reports "sink".
    NX_CHECK(write_file(local_path.c_str(), R"({"sink":[]})"));
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        paths.local_file = local_path;
        NX_CHECK(!load_chrono_settings(paths, settings).has_value());
        NX_CHECK(settings.sink.path == "/tmp/keep-chrono.jsonl");
        NX_CHECK(settings.diagnostics[0].field == "sink");
        NX_CHECK(settings.diagnostics[0].message == "expected a JSON object");
    }

    ::unlink(global_path.c_str());
    ::unlink(local_path.c_str());
}

void test_settings_edges() {
    ::unsetenv("NEXIGEN_CHRONO_COLLECT_INTERVAL_NS");
    ::unsetenv("NEXIGEN_CHRONO_CORE_MASK");

    // Diagnostics and paths render through member/free/formatter.
    {
        core::SettingsDiagnostic diag{core::SettingsSource::Computed, "field", "message"};
        const std::string text = diag.to_string();
        NX_CHECK(text.find("source=computed") != std::string::npos);
        NX_CHECK(text.find("field=field") != std::string::npos);
        NX_CHECK(to_string(diag) == text);
        NX_CHECK(std::format("{}", diag) == text);

        core::SettingsPaths paths;
        paths.global_file = "/global";
        paths.local_file = "/local";
        NX_CHECK(paths.to_string().find("/global") != std::string::npos);
        NX_CHECK(std::format("{}", paths) == paths.to_string());
    }

    // Settings render loaded flags and a multi-entry diagnostic list.
    {
        Settings settings;
        settings.global_loaded = true;
        settings.local_loaded = true;
        settings.options.collect_interval = std::chrono::nanoseconds{4};
        settings.options.worker_thread.set_cpus(core::CpuSet::from_core_mask(2));
        settings.diagnostics.push_back({core::SettingsSource::Global, "a", "b"});
        settings.diagnostics.push_back({core::SettingsSource::Local, "c", "d"});
        const std::string text = settings.to_string();
        NX_CHECK(text.find("global_loaded=true") != std::string::npos);
        NX_CHECK(text.find("local_loaded=true") != std::string::npos);
        NX_CHECK(text.find("; ") != std::string::npos);
        NX_CHECK(std::format("{}", settings) == text);

        nlohmann::json j;
        to_json(j, settings);
        NX_CHECK(j["diagnostics"].size() == 2U);
    }

    // from_json edge cases: non-objects and mistyped members are ignored.
    {
        core::SettingsPaths paths;
        paths.global_file = "/keep";
        from_json(nlohmann::json::array(), paths);
        NX_CHECK(paths.global_file == "/keep");
        from_json(nlohmann::json{{"global_file", 1}, {"local_file", false}}, paths);
        NX_CHECK(paths.global_file == "/keep");
        NX_CHECK(paths.local_file.empty());
        from_json(nlohmann::json::object(), paths);
        NX_CHECK(paths.global_file == "/keep");
        NX_CHECK(paths.local_file.empty());

        core::SettingsDiagnostic diag{core::SettingsSource::Local, "keep", "keep"};
        from_json(nlohmann::json::array(), diag);
        NX_CHECK(diag.source == core::SettingsSource::Local);
        from_json(nlohmann::json{{"source", 1}, {"field", 2}, {"message", 3}}, diag);
        NX_CHECK(diag.source == core::SettingsSource::Local);
        NX_CHECK(diag.field == "keep");
        NX_CHECK(diag.message == "keep");
        from_json(nlohmann::json::object(), diag);
        NX_CHECK(diag.source == core::SettingsSource::Local);
        NX_CHECK(diag.field == "keep");
        NX_CHECK(diag.message == "keep");
        from_json(nlohmann::json{{"source", "bogus"}}, diag);
        NX_CHECK(diag.source == core::SettingsSource::Local);
    }

    const char* global_path = "/tmp/nexigen_chrono_edge_global.json";
    const char* local_path = "/tmp/nexigen_chrono_edge_local.json";

    // Non-object documents: one InvalidArgument per file, both loaded.
    NX_CHECK(write_file(global_path, "[]"));
    NX_CHECK(write_file(local_path, "\"not-an-object\""));
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        paths.local_file = local_path;
        const Status s = load_chrono_settings(paths, settings);
        NX_CHECK(!s.has_value());
        NX_CHECK(s.error() == ErrorCode::InvalidArgument);
        NX_CHECK(settings.global_loaded && settings.local_loaded);
        NX_CHECK(settings.diagnostics.size() == 2U);
        const std::string text = settings.to_string();
        NX_CHECK(text.find("diagnostics=[") != std::string::npos);
        NX_CHECK(text.find("; ") != std::string::npos);
    }

    // A directory opens but cannot be read: SystemError, not "absent".
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = "/tmp";
        const Status s = load_chrono_settings(paths, settings);
        NX_CHECK(!s.has_value());
        NX_CHECK(s.error() == ErrorCode::SystemError);
        NX_CHECK(settings.diagnostics.size() == 1U);
        NX_CHECK(settings.diagnostics[0].message.find("could not be read") != std::string::npos);
    }

    // The same failure on the local source (the global path succeeded).
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.local_file = "/tmp";
        const Status s = load_chrono_settings(paths, settings);
        NX_CHECK(!s.has_value());
        NX_CHECK(s.error() == ErrorCode::SystemError);
        NX_CHECK(settings.diagnostics.size() == 1U);
        NX_CHECK(settings.diagnostics[0].source == core::SettingsSource::Local);
    }

    // Malformed JSON in the global file is a diagnostic and InvalidArgument.
    NX_CHECK(write_file(global_path, "{ not json"));
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        const Status s = load_chrono_settings(paths, settings);
        NX_CHECK(!s.has_value());
        NX_CHECK(s.error() == ErrorCode::InvalidArgument);
        NX_CHECK(settings.global_loaded);
        NX_CHECK(settings.diagnostics.size() == 1U);
        NX_CHECK(settings.diagnostics[0].message.find("malformed JSON") != std::string::npos);
    }

    // An unreadable file (EACCES, not ENOENT) is a SystemError. Skip when the
    // test runs as root, where mode 000 is not restrictive.
    if (::geteuid() != 0) {
        const char* unreadable_path = "/tmp/nexigen_chrono_unreadable.json";
        NX_CHECK(write_file(unreadable_path, "{}"));
        NX_CHECK(::chmod(unreadable_path, 0) == 0);
        {
            Settings settings;
            core::SettingsPaths paths;
            paths.global_file = unreadable_path;
            const Status s = load_chrono_settings(paths, settings);
            NX_CHECK(!s.has_value());
            NX_CHECK(s.error() == ErrorCode::SystemError);
        }
        NX_CHECK(::chmod(unreadable_path, 0644) == 0);
        ::unlink(unreadable_path);
    }

    // Environment interval: valid, invalid and > INT64_MAX.
    ::setenv("NEXIGEN_CHRONO_COLLECT_INTERVAL_NS", "4242", 1);
    {
        Settings settings;
        core::SettingsPaths paths;
        NX_CHECK(load_chrono_settings(paths, settings).has_value());
        NX_CHECK(settings.options.collect_interval == std::chrono::nanoseconds{4242});
    }
    ::setenv("NEXIGEN_CHRONO_COLLECT_INTERVAL_NS", "-1", 1);
    {
        Settings settings;
        core::SettingsPaths paths;
        const Status s = load_chrono_settings(paths, settings);
        NX_CHECK(!s.has_value());
        NX_CHECK(s.error() == ErrorCode::InvalidArgument);
        NX_CHECK(settings.diagnostics.size() == 1U);
        NX_CHECK(settings.diagnostics[0].source == core::SettingsSource::Environment);
    }
    ::setenv("NEXIGEN_CHRONO_COLLECT_INTERVAL_NS", "9223372036854775808", 1);
    {
        Settings settings;
        core::SettingsPaths paths;
        const Status s = load_chrono_settings(paths, settings);
        NX_CHECK(!s.has_value());
        NX_CHECK(s.error() == ErrorCode::InvalidArgument);
    }
    ::setenv("NEXIGEN_CHRONO_COLLECT_INTERVAL_NS", "12abc", 1);
    {
        Settings settings;
        core::SettingsPaths paths;
        const Status s = load_chrono_settings(paths, settings);
        NX_CHECK(!s.has_value());
        NX_CHECK(s.error() == ErrorCode::InvalidArgument);
    }
    ::unsetenv("NEXIGEN_CHRONO_COLLECT_INTERVAL_NS");

    // Environment core mask: invalid spelling is a diagnostic.
    ::setenv("NEXIGEN_CHRONO_CORE_MASK", "not-a-number", 1);
    {
        Settings settings;
        core::SettingsPaths paths;
        const Status s = load_chrono_settings(paths, settings);
        NX_CHECK(!s.has_value());
        NX_CHECK(s.error() == ErrorCode::InvalidArgument);
        NX_CHECK(settings.diagnostics.size() == 1U);
    }
    ::unsetenv("NEXIGEN_CHRONO_CORE_MASK");

    // JSON range checks: interval above INT64_MAX and a max-width core mask.
    NX_CHECK(write_file(global_path, "{\"collect_interval_ns\": 9223372036854775808}"));
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        const Status s = load_chrono_settings(paths, settings);
        NX_CHECK(!s.has_value());
        NX_CHECK(s.error() == ErrorCode::InvalidArgument);
    }
    NX_CHECK(write_file(global_path, "{\"core_mask\": 18446744073709551615}"));
    {
        Settings settings;
        core::SettingsPaths paths;
        paths.global_file = global_path;
        NX_CHECK(load_chrono_settings(paths, settings).has_value());
        NX_CHECK(settings.options.worker_thread.cpus() ==
                 core::CpuSet::from_core_mask(std::numeric_limits<std::uint64_t>::max()));
    }

    ::unlink(global_path);
    ::unlink(local_path);
}

} // namespace

int main() {
    run_suite("timestamp", test_timestamp);
    run_suite("stamp", test_stamp);
    run_suite("utc_offset", test_utc_offset);
    run_suite("time_format", test_time_format);
    run_suite("diag_strings", test_diag_strings);
    run_suite("enum_contracts", test_enum_contracts);
    run_suite("clock_id", test_clock_id);
    run_suite("snapshot", test_snapshot);
    run_suite("snapshot_edges", test_snapshot_edges);
    run_suite("sinks", test_sinks);
    run_suite("sink_edges", test_sink_edges);
    run_suite("provider", test_provider);
    run_suite("provider_config_edges", test_provider_config_edges);
    run_suite("affinity_edges", test_affinity_edges);
    run_suite("chrono_required_thread_failure", test_chrono_required_thread_failure);
    run_suite("settings", test_settings);
    run_suite("chrono_worker_thread_settings", test_chrono_worker_thread_settings);
    run_suite("settings_sink", test_settings_sink);
    run_suite("settings_edges", test_settings_edges);

    const int failures = g_failures;
    std::printf("total: %d checks, %d failed\n", g_checks, failures);
    return failures;
}
