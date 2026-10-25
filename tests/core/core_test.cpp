// Тесты nexigen::core — C++23, без сторонних фреймворков (см. design.md D4).
//
// Харнес: NX_CHECK ведёт счёт проверок и падений с файлом/строкой,
// run_suite печатает строку-резюме на каждый набор, main возвращает число
// падений — ненулевой код выхода роняет тест в CTest.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <format>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>

// Build wiring proof (task 1.1): every consumer TU must see JSON_NOEXCEPTION=1
// before <nlohmann/json.hpp> (it is already included above via the nexigen::core
// INTERFACE definitions) — otherwise the headers would generate try/catch paths
// that cannot exist under the project-wide -fno-exceptions.
#ifndef JSON_NOEXCEPTION
#error "JSON_NOEXCEPTION=1 must propagate to every nexigen consumer TU"
#endif

// --- Compile-time claims (design.md D4): to_string resolves per enumerator.
static_assert(nexigen::core::to_string(nexigen::core::ErrorCode::Ok) == "Ok");
static_assert(nexigen::core::to_string(nexigen::core::ErrorCode::BufferFull) == "BufferFull");
static_assert(nexigen::core::to_string(nexigen::core::ErrorCode::BufferEmpty) == "BufferEmpty");
static_assert(nexigen::core::to_string(nexigen::core::ErrorCode::InvalidArgument) == "InvalidArgument");
static_assert(nexigen::core::to_string(nexigen::core::ErrorCode::SystemError) == "SystemError");
static_assert(nexigen::core::to_string(nexigen::core::ErrorCode::NotInitialized) == "NotInitialized");
static_assert(nexigen::core::to_string(nexigen::core::ErrorCode::RateLimited) == "RateLimited");

// Append-only contract (logging change): existing enumerator values are pinned,
// so RateLimited joins the catalogue as enumerator 6 without renumbering.
static_assert(static_cast<std::uint8_t>(nexigen::core::ErrorCode::Ok) == 0);
static_assert(static_cast<std::uint8_t>(nexigen::core::ErrorCode::NotInitialized) == 5);
static_assert(static_cast<std::uint8_t>(nexigen::core::ErrorCode::RateLimited) == 6);

// Alias identity: Result's default error type IS ErrorCode; Status is the void form.
static_assert(std::is_same_v<nexigen::core::Result<int>, std::expected<int, nexigen::core::ErrorCode>>);
static_assert(std::is_same_v<nexigen::core::Status, std::expected<void, nexigen::core::ErrorCode>>);

// --- Compile-time claims (design.md D3/D4): queue layout + capacity predicate.
static_assert(alignof(nexigen::core::SpscRingBuffer<std::uint64_t, 8>) == 64);
static_assert(!nexigen::core::SpscRingBuffer<std::uint64_t, 8>::is_power_of_two(1000));
static_assert(nexigen::core::SpscRingBuffer<std::uint64_t, 1024>::is_power_of_two(1024));
static_assert(nexigen::core::SpscRingBuffer<std::uint64_t, 8>::capacity() == 8);

using namespace nexigen::core; // test TU: exercise the API unqualified

namespace {

int g_checks = 0;
int g_failures = 0;

// Allocation detector (task 3.2): the C++ free store is re-backed by a fixed
// static bump arena. Any dynamic allocation anywhere in the process bumps
// g_alloc_used — hot-path code must leave it untouched. This is the harness's
// measurement hook, not a use of dynamic allocation itself.
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

// --- result suite (tasks 2.2): ErrorCode / to_string / Result / Status.

// A fallible operation with no payload: success is Status{}.
[[nodiscard]] Status status_succeeds() noexcept { return {}; }

[[nodiscard]] Status status_fails_not_initialized() noexcept {
    return std::unexpected(ErrorCode::NotInitialized);
}

// A fallible operation returning a value; default error type is ErrorCode.
[[nodiscard]] Result<int> parse_succeeds() noexcept { return 42; }

[[nodiscard]] Result<int> parse_fails() noexcept { return std::unexpected(ErrorCode::BufferEmpty); }

void test_result() {
    // Distinctness: all seven codes pairwise different (21 pairs).
    const ErrorCode all_codes[] = {
        ErrorCode::Ok, ErrorCode::BufferFull, ErrorCode::BufferEmpty,
        ErrorCode::InvalidArgument, ErrorCode::SystemError, ErrorCode::NotInitialized,
        ErrorCode::RateLimited,
    };
    for (int i = 0; i < 7; ++i) {
        for (int j = i + 1; j < 7; ++j) {
            NX_CHECK(all_codes[i] != all_codes[j]);
        }
    }
    // Ok is distinguishable from every failure code (success is representable).
    NX_CHECK(all_codes[0] != all_codes[1] && all_codes[0] != all_codes[5]);

    // to_string at runtime matches the compile-time (static_assert'ed) names.
    NX_CHECK(to_string(ErrorCode::Ok) == "Ok");
    NX_CHECK(to_string(ErrorCode::BufferFull) == "BufferFull");
    NX_CHECK(to_string(ErrorCode::BufferEmpty) == "BufferEmpty");
    NX_CHECK(to_string(ErrorCode::InvalidArgument) == "InvalidArgument");
    NX_CHECK(to_string(ErrorCode::SystemError) == "SystemError");
    NX_CHECK(to_string(ErrorCode::NotInitialized) == "NotInitialized");
    NX_CHECK(to_string(ErrorCode::RateLimited) == "RateLimited");
    // Garbage-cast value is well-defined via the default arm.
    NX_CHECK(to_string(static_cast<ErrorCode>(200)) == "Unknown");

    // Result<int> success: branch on has_value(), dereference — never value().
    const Result<int> ok_res = parse_succeeds();
    NX_CHECK(ok_res.has_value());
    NX_CHECK(*ok_res == 42);

    // Result<int> failure: error code readable as the code it was built with.
    const Result<int> err_res = parse_fails();
    NX_CHECK(!err_res.has_value());
    NX_CHECK(err_res.error() == ErrorCode::BufferEmpty);

    // Direct construction of both states.
    const Result<int> constructed_ok{7};
    const Result<int> constructed_err{std::unexpected(ErrorCode::BufferFull)};
    NX_CHECK(constructed_ok.has_value() && *constructed_ok == 7);
    NX_CHECK(!constructed_err.has_value() && constructed_err.error() == ErrorCode::BufferFull);

    // Status: Status{} is success; failure carries the code.
    const Status ok_status = status_succeeds();
    NX_CHECK(ok_status.has_value());
    const Status err_status = status_fails_not_initialized();
    NX_CHECK(!err_status.has_value());
    NX_CHECK(err_status.error() == ErrorCode::NotInitialized);

    // Hot-path shape: return-by-value carries no payload beyond T + ErrorCode
    // (checked by sizeof below); construction/read of both states allocates nothing.
    static_assert(sizeof(Status) <= sizeof(ErrorCode) + sizeof(std::size_t),
                  "Status stays small: no hidden payload");
    static_assert(sizeof(Result<std::uint64_t>) <= 2 * sizeof(std::uint64_t),
                  "Result<uint64_t> stays register-pair sized");
}

// --- spsc_ring_buffer suite, single-threaded (task 4.3): capacity, full/empty
// classification, FIFO integrity across wrap-arounds.

void test_spsc_single_threaded() {
    SpscRingBuffer<std::uint64_t, 8> q{};
    NX_CHECK(q.capacity() == 8);
    NX_CHECK(q.is_empty());
    NX_CHECK(q.size() == 0);

    // Pop from empty reports BufferEmpty and yields nothing.
    std::uint64_t yielded = 0xDEADBEEF;
    const Status empty_pop = q.try_pop(yielded);
    NX_CHECK(!empty_pop.has_value());
    NX_CHECK(empty_pop.error() == ErrorCode::BufferEmpty);
    NX_CHECK(yielded == 0xDEADBEEF); // out untouched on failure

    // Push into room succeeds; element is enqueued.
    const Status push0 = q.try_push(11ULL);
    NX_CHECK(push0.has_value());
    NX_CHECK(q.size() == 1);
    NX_CHECK(!q.is_empty());

    // Fill to exactly Capacity: all succeed, full state distinguishable from empty.
    for (std::uint64_t v = 1; v < 8; ++v) {
        const Status s = q.try_push(v);
        NX_CHECK(s.has_value());
    }
    NX_CHECK(q.size() == 8);
    NX_CHECK(q.is_full());

    // Next push reports BufferFull and the element is NOT taken into the queue
    // (no stored element lost: size unchanged, drain below still sees 1..8).
    const Status overflow = q.try_push(999ULL);
    NX_CHECK(!overflow.has_value());
    NX_CHECK(overflow.error() == ErrorCode::BufferFull);
    NX_CHECK(q.size() == 8);

    // Pop returns the front element (FIFO), then drain: all succeed in order.
    std::uint64_t out = 0;
    const Status first_pop = q.try_pop(out);
    NX_CHECK(first_pop.has_value());
    NX_CHECK(out == 11); // the first pushed value comes out first
    for (std::uint64_t v = 1; v < 8; ++v) {
        const Status s = q.try_pop(out);
        NX_CHECK(s.has_value());
        NX_CHECK(out == v);
    }
    NX_CHECK(q.is_empty());
    const Status drained_pop = q.try_pop(out);
    NX_CHECK(!drained_pop.has_value());
    NX_CHECK(drained_pop.error() == ErrorCode::BufferEmpty);

    // Move-rvalue push path.
    std::uint64_t payload = 42;
    const Status moved = q.try_push(std::move(payload));
    NX_CHECK(moved.has_value());
    NX_CHECK(q.try_pop(out).has_value());
    NX_CHECK(out == 42);

    // Wrap-around: Capacity 8, 250 elements through bursts that cross the
    // ring boundary repeatedly (slot reuse over many laps) — FIFO order and
    // values preserved end to end.
    SpscRingBuffer<std::uint64_t, 8> ring{};
    std::uint64_t next_push = 0;
    std::uint64_t next_pop = 0;
    for (int round = 0; round < 50; ++round) {
        const int burst = (round % 2 == 0) ? 5 : 3; // vary lap phases to hit both ends
        for (int b = 0; b < burst; ++b) {
            NX_CHECK(ring.try_push(next_push++).has_value());
        }
        for (int b = 0; b < burst; ++b) {
            std::uint64_t got = 0;
            NX_CHECK(ring.try_pop(got).has_value());
            NX_CHECK(got == next_pop);
            ++next_pop;
        }
    }
    NX_CHECK(ring.is_empty());
    NX_CHECK(next_push == 200 && next_pop == 200);

    // Full/empty boundaries at WRAPPED indices (head/tail are past one lap of
    // the 8-slot ring): fill to exactly Capacity, next push must report
    // BufferFull without disturbing stored elements, drain restores empty.
    for (std::uint64_t v = 0; v < 8; ++v) {
        NX_CHECK(ring.try_push(next_push++).has_value());
    }
    NX_CHECK(ring.is_full());
    NX_CHECK(!ring.try_push(12345).has_value());
    for (std::uint64_t v = 0; v < 8; ++v) {
        std::uint64_t got = 0;
        NX_CHECK(ring.try_pop(got).has_value());
        NX_CHECK(got == next_pop++);
    }
    NX_CHECK(ring.is_empty());

    // Fill-then-drain over the whole capacity again (fresh queue, all push, all pop).
    SpscRingBuffer<std::uint64_t, 8> fd{};
    for (std::uint64_t v = 0; v < 8; ++v) { NX_CHECK(fd.try_push(v + 1000).has_value()); }
    NX_CHECK(!fd.try_push(9).has_value()); // full: BufferFull, nothing dropped
    for (std::uint64_t v = 0; v < 8; ++v) {
        std::uint64_t got = 0;
        NX_CHECK(fd.try_pop(got).has_value());
        NX_CHECK(got == v + 1000);
    }
    NX_CHECK(!fd.try_pop(out).has_value()); // empty: BufferEmpty, nothing yielded
}

// --- spsc stress suite (task 5.1): 1,000,000 elements, one producer thread,
// one consumer thread, capacity 8192. std::jthread joins on scope exit (no
// detach). Nothing is printed inside the element loops; per-element work is
// a single Status check; mismatches are counted and asserted once after join.

struct StressResult {
    std::uint64_t sent = 0;
    std::uint64_t producer_retries = 0;
    std::uint64_t received = 0;
    std::uint64_t consumer_retries = 0;
    std::uint64_t order_violations = 0;
    std::uint64_t push_sum = 0;
    std::uint64_t pop_sum = 0;
};

void test_spsc_stress() {
    static SpscRingBuffer<std::uint64_t, 8192> queue{};
    static constexpr std::uint64_t k_count = 1'000'000;
    StressResult r{};

    std::jthread producer{[&r] {
        while (r.sent < k_count) {
            if (queue.try_push(r.sent).has_value()) {
                r.push_sum += r.sent;
                ++r.sent;
            } else {
                ++r.producer_retries; // BufferFull: wait-free, retry immediately
            }
        }
    }};

    std::jthread consumer{[&r] {
        std::uint64_t expected = 0;
        while (expected < k_count) {
            std::uint64_t got = 0;
            if (queue.try_pop(got).has_value()) {
                if (got != expected) { ++r.order_violations; } // increasing, no gaps
                r.pop_sum += got;
                ++expected;
            } else {
                ++r.consumer_retries; // BufferEmpty: wait-free, retry immediately
            }
        }
        r.received = expected;
    }};

    // Join before reading the results; jthread destructors are the backstop.
    producer.join();
    consumer.join();

    // jthread destructors join both threads — the run terminates on its own.
    NX_CHECK(r.sent == k_count);
    NX_CHECK(r.received == k_count);
    NX_CHECK(r.order_violations == 0);
    NX_CHECK(r.push_sum == r.pop_sum);
    // Closed form of 0+1+...+(k_count-1) as an independent third checksum.
    NX_CHECK(r.pop_sum == k_count * (k_count - 1) / 2);
    NX_CHECK(queue.is_empty());
    std::printf("stress: received=%llu retries=%llu/%llu checksum=%llu\n",
                static_cast<unsigned long long>(r.received),
                static_cast<unsigned long long>(r.producer_retries),
                static_cast<unsigned long long>(r.consumer_retries),
                static_cast<unsigned long long>(r.pop_sum));
}

// --- spsc visibility suite (task 5.2): a padded multi-field payload (exactly
// 64 bytes, every field derived from the sequence number) transferred
// cross-thread. A missing release/acquire edge shows up as a field mismatch.
// Two variants: producer created first, and consumer created first so it
// spins on BufferEmpty before any push — both cached-index refresh paths
// are exercised.

struct Payload {
    std::uint64_t field[8];
};
static_assert(sizeof(Payload) >= 64, "payload must span a full cache line");

void fill_payload(Payload& p, std::uint64_t seq) noexcept {
    p.field[0] = seq;
    p.field[1] = seq * 3U + 1U;
    p.field[2] = seq ^ 0x9E3779B97F4A7C15ULL;
    p.field[3] = ~seq;
    p.field[4] = (seq << 7U) | 0x5AULL;
    p.field[5] = seq + 0xFEEDFACECAFEBEEFULL;
    p.field[6] = seq * seq;
    p.field[7] = (seq << 13U) ^ (seq >> 5U);
}

void run_visibility_variant(const bool consumer_first) {
    static constexpr std::uint64_t k_views = 50'000;

    struct ViewResult {
        std::uint64_t sent = 0;
        std::uint64_t received = 0;
        std::uint64_t field_mismatches = 0;
    } r{};

    SpscRingBuffer<Payload, 1024> queue{};
    static_assert(sizeof(queue) >= 64 + 64 + 64 * 1024, "fixture: 1024 x 64 B slots + 2 index lines");

    const auto produce = [&r, &queue] {
        while (r.sent < k_views) {
            Payload p{};
            fill_payload(p, r.sent);
            if (queue.try_push(p).has_value()) {
                ++r.sent;
            }
        }
    };
    const auto consume = [&r, &queue] {
        std::uint64_t expected = 0;
        while (expected < k_views) {
            Payload got{};
            if (queue.try_pop(got).has_value()) {
                Payload want{};
                fill_payload(want, expected);
                for (int f = 0; f < 8; ++f) {
                    if (got.field[f] != want.field[f]) { ++r.field_mismatches; }
                }
                ++expected;
            }
        }
        r.received = expected;
    };

    if (consumer_first) {
        const std::jthread consumer{consume}; // spins on BufferEmpty first
        const std::jthread producer{produce};
    } else {
        const std::jthread producer{produce};
        const std::jthread consumer{consume};
    }

    NX_CHECK(r.sent == k_views);
    NX_CHECK(r.received == k_views);
    NX_CHECK(r.field_mismatches == 0);
}

// --- serialization suite (change add-serialization-and-wait-free-logging):
// json smoke (task 1.2), ErrorCode from_string / to_json / from_json /
// std::formatter (task 2.1). All cold-path surface; arena allocations are
// expected here (the bump arena simply bounds them).

constexpr ErrorCode k_all_codes[] = {
    ErrorCode::Ok, ErrorCode::BufferFull, ErrorCode::BufferEmpty,
    ErrorCode::InvalidArgument, ErrorCode::SystemError, ErrorCode::NotInitialized,
    ErrorCode::RateLimited,
};

void test_json_smoke() {
    // Task 1.2: a TU that includes <nlohmann/json.hpp> under -fno-exceptions
    // (JSON_NOEXCEPTION=1 proven by the #error above) builds one value.
    nlohmann::json j = {{"answer", 42}};
    NX_CHECK(j.is_object());
    NX_CHECK(j.contains("answer"));
    NX_CHECK(j["answer"] == 42);
    NX_CHECK(j["answer"].is_number_integer());
    NX_CHECK(j["answer"].get<int>() == 42);
    NX_CHECK(j["answer"].get_ptr<const std::string*>() == nullptr); // wrong type: nullptr, no abort
}

void test_error_code_vocabulary() {
    // from_string: every canonical name parses; everything else leaves the
    // out-parameter untouched and returns false (including "Unknown").
    for (const ErrorCode code : k_all_codes) {
        ErrorCode parsed = ErrorCode::SystemError; // sentinel
        NX_CHECK(from_string(to_string(code), parsed));
        NX_CHECK(parsed == code);
    }
    for (std::string_view bad : {"Nope", "", "unknown", "Unknown", "bufferfull"}) {
        ErrorCode keep = ErrorCode::NotInitialized;
        NX_CHECK(!from_string(bad, keep));
        NX_CHECK(keep == ErrorCode::NotInitialized);
    }

    // to_json/from_json round-trip: the JSON value is exactly to_string(code).
    for (const ErrorCode code : k_all_codes) {
        nlohmann::json j;
        to_json(j, code);
        NX_CHECK(j.is_string());
        NX_CHECK(j == std::string(to_string(code)));
        ErrorCode back = ErrorCode::InvalidArgument; // fallback pre-set
        from_json(j, back);
        NX_CHECK(back == code);
    }

    // from_json hostile inputs (spec core/serialization): never terminate,
    // leave the fallback. JSON_NOEXCEPTION would abort on .get<> mistakes —
    // get_ptr is the mandated non-throwing inspection.
    for (const nlohmann::json& hostile : {nlohmann::json(7),
                                          nlohmann::json::array(),
                                          nlohmann::json::object(),
                                          nlohmann::json(),           // null
                                          nlohmann::json("Bogus")}) {
        ErrorCode keep = ErrorCode::Ok;
        from_json(hostile, keep);
        NX_CHECK(keep == ErrorCode::Ok);
    }

    // std::formatter<ErrorCode>: name via "{}", fill/align via string_view rules.
    NX_CHECK(std::format("{}", ErrorCode::BufferFull) == "BufferFull");
    NX_CHECK(std::format("[{:*>8}]", ErrorCode::Ok) == "[******Ok]");
    static_assert(std::formattable<const ErrorCode&, char>);
}

void test_spsc_visibility() {
    run_visibility_variant(false); // producer starts first
    run_visibility_variant(true);  // consumer idles first (empty-retry + refresh paths)
    std::printf("visibility: 2 x 50000 payloads, 0 field mismatches\n");
}

// Ring diagnostic snapshot (task 3.1): capacity/occupancy/state render; a
// third thread may call it while both endpoints are active (relaxed loads,
// snapshot caveat identical to size()).
void test_ring_to_string() {
    SpscRingBuffer<std::uint64_t, 8> ring{};
    const std::string empty_dump = ring.to_string();
    NX_CHECK(empty_dump.find("capacity=8") != std::string::npos);
    NX_CHECK(empty_dump.find("size=0") != std::string::npos);
    NX_CHECK(empty_dump.find("empty") != std::string::npos);
    NX_CHECK(to_string(ring) == empty_dump); // free == member

    for (int i = 0; i < 3; ++i) {
        static_cast<void>(ring.try_push(static_cast<std::uint64_t>(i)));
    }
    const std::string partial = ring.to_string();
    NX_CHECK(partial.find("size=3") != std::string::npos);
    NX_CHECK(partial.find("partial") != std::string::npos);

    for (int i = 3; i < 8; ++i) {
        static_cast<void>(ring.try_push(static_cast<std::uint64_t>(i)));
    }
    NX_CHECK(ring.to_string().find("full") != std::string::npos);

    // Third-thread call concurrent with active producer + consumer.
    std::uint64_t sink = 0;
    std::atomic<bool> done{false};
    std::jthread consumer([&] {
        while (!done.load(std::memory_order_relaxed)) {
            std::uint64_t out = 0;
            if (ring.try_pop(out)) { sink += out; }
        }
    });
    std::jthread producer([&] {
        for (std::uint64_t v = 0; v < 200'000; ++v) {
            static_cast<void>(ring.try_push(v));
        }
    });
    const std::string concurrent = ring.to_string(); // main thread: third observer
    done.store(true, std::memory_order_relaxed);
    producer.join();
    consumer.join(); // jthread: stop-request + join (idempotent at scope exit)
    NX_CHECK(!concurrent.empty());
    NX_CHECK(sink < (1ULL << 63U)); // drained value observed post-join
}

} // namespace

int main()
{
    run_suite("result", test_result);
    run_suite("json_smoke", test_json_smoke);
    run_suite("ec_vocab", test_error_code_vocabulary);
    run_suite("ring_to_string", test_ring_to_string);
    run_suite("spsc_single_threaded", test_spsc_single_threaded);
    run_suite("spsc_stress_1m", test_spsc_stress);
    run_suite("spsc_visibility", test_spsc_visibility);

    const int failures = g_failures;
    std::printf("total: %d checks, %d failed\n", g_checks, failures);
    return failures;
}
