// Тесты nexigen::core — C++23, без сторонних фреймворков (см. design.md D4).
//
// Харнес: NX_CHECK ведёт счёт проверок и падений с файлом/строкой,
// run_suite печатает строку-резюме на каждый набор, main возвращает число
// падений — ненулевой код выхода роняет тест в CTest.

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <format>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/serialization_mandate.hpp>
#include <nexigen/core/settings_vocabulary.hpp>
#include <nexigen/core/shard_pool.hpp>
#include <nexigen/core/affinity.hpp>
#include <nexigen/core/sink_io.hpp>
#include <nexigen/core/sink_pack.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/detail/fault_injection.hpp>

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
static_assert(nexigen::core::to_string(nexigen::core::ErrorCode::InvalidArgument) ==
              "InvalidArgument");
static_assert(nexigen::core::to_string(nexigen::core::ErrorCode::SystemError) == "SystemError");
static_assert(nexigen::core::to_string(nexigen::core::ErrorCode::NotInitialized) ==
              "NotInitialized");
static_assert(nexigen::core::to_string(nexigen::core::ErrorCode::RateLimited) == "RateLimited");
static_assert(nexigen::core::to_string(nexigen::core::ErrorCode::Conflict) == "Conflict");

// Append-only contract (logging change): existing enumerator values are pinned,
// so RateLimited joins the catalogue as enumerator 6 without renumbering.
static_assert(static_cast<std::uint8_t>(nexigen::core::ErrorCode::Ok) == 0);
static_assert(static_cast<std::uint8_t>(nexigen::core::ErrorCode::NotInitialized) == 5);
static_assert(static_cast<std::uint8_t>(nexigen::core::ErrorCode::RateLimited) == 6);
static_assert(static_cast<std::uint8_t>(nexigen::core::ErrorCode::Conflict) == 7);

// Alias identity: Result's default error type IS ErrorCode; Status is the void form.
static_assert(
    std::is_same_v<nexigen::core::Result<int>, std::expected<int, nexigen::core::ErrorCode>>);
static_assert(std::is_same_v<nexigen::core::Status, std::expected<void, nexigen::core::ErrorCode>>);

// --- serialization mandate machinery (change refactor-serialization-
// mandate, P3-28): a NEW enum + struct reach std::format / to_json /
// from_json / free to_string with ZERO per-type boilerplate beyond the
// canonical vocabulary (to_string / from_string / member to_string).
// The demo types live under nexigen so the generic ADL machinery in
// nexigen:: is reachable exactly as it will be for real types.
namespace nexigen {
namespace mandate_demo {

enum class DemoMode { Idle, Active };
[[nodiscard]] constexpr std::string_view to_string(DemoMode v) noexcept {
    return v == DemoMode::Idle ? "idle" : "active";
}
[[nodiscard]] constexpr bool from_string(std::string_view name, DemoMode& out) noexcept {
    if (name == "idle") {
        out = DemoMode::Idle;
        return true;
    }
    if (name == "active") {
        out = DemoMode::Active;
        return true;
    }
    return false;
}

struct DemoTag {
    std::string label{};
    [[nodiscard]] std::string to_string() const { return "tag(" + label + ")"; }
};

/// Per-enum JSON pair for the demo scope (change refactor-serialization-
/// mandate D2 note: enum ADL reaches only the immediate namespace, so each
/// namespace that owns enums keeps a pair — here one for the demo enum).
inline void to_json(nlohmann::json& j, const DemoMode value) {
    j = std::string(to_string(value));
}
inline void from_json(const nlohmann::json& j, DemoMode& value) noexcept {
    if (const auto* const name = j.get_ptr<const nlohmann::json::string_t*>()) {
        static_cast<void>(from_string(std::string_view(*name), value));
    }
}

} // namespace mandate_demo
} // namespace nexigen

static_assert(nexigen::core::mandate::MandateToString<nexigen::mandate_demo::DemoMode>);
static_assert(nexigen::core::mandate::MandateToString<nexigen::mandate_demo::DemoTag>);

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

#define NX_CHECK(condition)                                                                        \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(condition)) {                                                                        \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);              \
        }                                                                                          \
    } while (false)

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

// --- result suite (tasks 2.2): ErrorCode / to_string / Result / Status.

// A fallible operation with no payload: success is Status{}.
[[nodiscard]] Status status_succeeds() noexcept {
    return {};
}

[[nodiscard]] Status status_fails_not_initialized() noexcept {
    return std::unexpected(ErrorCode::NotInitialized);
}

// A fallible operation returning a value; default error type is ErrorCode.
[[nodiscard]] Result<int> parse_succeeds() noexcept {
    return 42;
}

[[nodiscard]] Result<int> parse_fails() noexcept {
    return std::unexpected(ErrorCode::BufferEmpty);
}

void test_result() {
    // Distinctness: all eight codes pairwise different (28 pairs).
    const ErrorCode all_codes[] = {
        ErrorCode::Ok,          ErrorCode::BufferFull,
        ErrorCode::BufferEmpty, ErrorCode::InvalidArgument,
        ErrorCode::SystemError, ErrorCode::NotInitialized,
        ErrorCode::RateLimited, ErrorCode::Conflict,
    };
    for (int i = 0; i < 8; ++i) {
        for (int j = i + 1; j < 8; ++j) {
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
    NX_CHECK(to_string(ErrorCode::Conflict) == "Conflict");
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
    for (std::uint64_t v = 0; v < 8; ++v) {
        NX_CHECK(fd.try_push(v + 1000).has_value());
    }
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
                if (got != expected) {
                    ++r.order_violations;
                } // increasing, no gaps
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
    static_assert(sizeof(queue) >= 64 + 64 + 64 * 1024,
                  "fixture: 1024 x 64 B slots + 2 index lines");

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
                    if (got.field[f] != want.field[f]) {
                        ++r.field_mismatches;
                    }
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
    ErrorCode::Ok,          ErrorCode::BufferFull,
    ErrorCode::BufferEmpty, ErrorCode::InvalidArgument,
    ErrorCode::SystemError, ErrorCode::NotInitialized,
    ErrorCode::RateLimited, ErrorCode::Conflict,
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

    // Serialization mandate machinery (change refactor-serialization-
    // mandate, P3-28): the generic formatter + generic JSON pair serve a
    // brand-new enum and struct without per-type boilerplate. Calls are
    // qualified from this GLOBAL test scope (the enclosing-namespace walk
    // that reaches nexigen:: is a property of nexigen::<module> scopes;
    // module-level tests inside nexigen::<module> call these unqualified).
    NX_CHECK(nexigen::to_string(nexigen::mandate_demo::DemoMode::Active) == "active");
    NX_CHECK(nexigen::to_string(nexigen::mandate_demo::DemoTag{"x"}) == "tag(x)");
    NX_CHECK(std::format("{}", nexigen::mandate_demo::DemoMode::Idle) == "idle");
    NX_CHECK(std::format("{}", nexigen::mandate_demo::DemoTag{"x"}) == "tag(x)");
    // JSON pairs carry per-enum (ADL for enums reaches only the immediate
    // namespace — the analysis #28 discovery recorded in design D2).
    nlohmann::json mode;
    nexigen::mandate_demo::to_json(mode, nexigen::mandate_demo::DemoMode::Active);
    NX_CHECK(mode.is_string() && mode.get<std::string>() == "active");
    nexigen::mandate_demo::DemoMode back{nexigen::mandate_demo::DemoMode::Idle};
    nexigen::mandate_demo::from_json(mode, back);
    NX_CHECK(back == nexigen::mandate_demo::DemoMode::Active);
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
    for (const nlohmann::json& hostile :
         {nlohmann::json(7), nlohmann::json::array(), nlohmann::json::object(),
          nlohmann::json(), // null
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

// --- settings vocabulary suite (change refactor-settings-into-subsystem-modules;
// spec core/settings-vocabulary): SettingsSource text/JSON round-trips, hostile
// JSON left-unchanged, and SettingsPaths / SettingsDiagnostic round-trips.

void test_settings_vocabulary() {
    const SettingsSource all_sources[] = {
        SettingsSource::Global,
        SettingsSource::Local,
        SettingsSource::Environment,
        SettingsSource::Computed,
    };

    // to_string names each source; from_string recovers it; garbage is "Unknown".
    NX_CHECK(to_string(SettingsSource::Global) == "global");
    NX_CHECK(to_string(SettingsSource::Local) == "local");
    NX_CHECK(to_string(SettingsSource::Environment) == "environment");
    NX_CHECK(to_string(SettingsSource::Computed) == "computed");
    NX_CHECK(to_string(static_cast<SettingsSource>(0x7f)) == "Unknown");
    for (const SettingsSource src : all_sources) {
        SettingsSource back = SettingsSource::Global;
        NX_CHECK(from_string(to_string(src), back));
        NX_CHECK(back == src);
    }
    for (std::string_view bad : {"Nope", "", "GLOBAL", "unknown"}) {
        SettingsSource keep = SettingsSource::Local;
        NX_CHECK(!from_string(bad, keep));
        NX_CHECK(keep == SettingsSource::Local);
    }

    // Enum JSON round-trips; the intermediate value is the canonical name.
    for (const SettingsSource src : all_sources) {
        nlohmann::json j;
        to_json(j, src);
        NX_CHECK(j.is_string());
        NX_CHECK(j == std::string(to_string(src)));
        SettingsSource back = SettingsSource::Computed;
        from_json(j, back);
        NX_CHECK(back == src);
    }

    // Hostile enum JSON leaves the fallback unchanged, never aborts.
    for (const nlohmann::json& hostile :
         {nlohmann::json(7), nlohmann::json::array(), nlohmann::json::object(), nlohmann::json(),
          nlohmann::json("Bogus")}) {
        SettingsSource keep = SettingsSource::Environment;
        from_json(hostile, keep);
        NX_CHECK(keep == SettingsSource::Environment);
    }

    // std::formatter<SettingsSource> delegates to to_string with fill/align.
    NX_CHECK(std::format("{}", SettingsSource::Computed) == "computed");
    NX_CHECK(std::format("[{:*>8}]", SettingsSource::Local) == "[***local]");
    static_assert(std::formattable<const SettingsSource&, char>);

    // SettingsPaths renders and round-trips through JSON.
    const SettingsPaths paths{"/tmp/global.json", "/tmp/local.json"};
    const std::string paths_text = paths.to_string();
    NX_CHECK(paths_text.find("global_file") != std::string::npos);
    NX_CHECK(paths_text.find("/tmp/global.json") != std::string::npos);
    NX_CHECK(paths_text.find("/tmp/local.json") != std::string::npos);
    NX_CHECK(to_string(paths) == paths_text);
    {
        nlohmann::json j;
        to_json(j, paths);
        SettingsPaths back{};
        from_json(j, back);
        NX_CHECK(back.global_file == "/tmp/global.json");
        NX_CHECK(back.local_file == "/tmp/local.json");
    }
    {
        SettingsPaths keep{"/a", "/b"};
        from_json(nlohmann::json(42), keep); // non-object leaves unchanged
        NX_CHECK(keep.global_file == "/a" && keep.local_file == "/b");
    }

    // SettingsDiagnostic renders and round-trips through JSON.
    const SettingsDiagnostic diag{SettingsSource::Local, "core_mask", "bad"};
    const std::string diag_text = diag.to_string();
    NX_CHECK(diag_text.find("local") != std::string::npos);
    NX_CHECK(diag_text.find("core_mask") != std::string::npos);
    NX_CHECK(diag_text.find("bad") != std::string::npos);
    NX_CHECK(to_string(diag) == diag_text);
    {
        nlohmann::json j;
        to_json(j, diag);
        SettingsDiagnostic back{};
        from_json(j, back);
        NX_CHECK(back.source == SettingsSource::Local);
        NX_CHECK(back.field == "core_mask");
        NX_CHECK(back.message == "bad");
    }
    {
        SettingsDiagnostic keep{SettingsSource::Global, "a", "b"};
        from_json(nlohmann::json::array(), keep);
        NX_CHECK(keep.source == SettingsSource::Global);
        NX_CHECK(keep.field == "a" && keep.message == "b");
    }
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
            if (ring.try_pop(out)) {
                sink += out;
            }
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

// --- ShardPool TLS-cache bounds (change fix-p1-races-process-correctness,
// spec logging/multi-producer "Thread-local slot registration", scenario
// "TLS cache exhaustion drops without burning registration slots"):
// 8 distinct pools per thread are cached wait-free; a ninth pool takes the
// counted slot_exhausted drop WITHOUT advancing the registration counter per
// record (analysis #10: the old size-4 map burned one global slot per emit).
void test_shard_pool_tls_cache() {
    using Pool = nexigen::core::ShardPool<std::uint64_t, 8, 16>;
    std::vector<std::unique_ptr<Pool>> pools;
    pools.reserve(9);
    std::vector<std::int32_t> first_slots{};
    for (std::size_t i = 0; i < 8; ++i) {
        pools.push_back(std::make_unique<Pool>());
        const std::int32_t slot = pools.back()->claim_slot();
        NX_CHECK(slot >= 0);
        first_slots.push_back(slot);
    }
    // Cached steady path: every later call on a cached pool returns the
    // same slot (thread-local map hit; no shared atomic touched). Slots are
    // per-pool numbers, so cross-pool equality is expected and irrelevant.
    bool steady_ok = true;
    bool whole_values = true;
    for (std::size_t i = 0; i < 8; ++i) {
        if (pools[i]->claim_slot() != first_slots[i]) {
            steady_ok = false;
        }
        if (first_slots[i] < 0) {
            whole_values = false;
        }
    }
    NX_CHECK(steady_ok);
    NX_CHECK(whole_values);

    // Ninth pool: TLS map full → counted drop (its own counter, fresh 0→1).
    pools.push_back(std::make_unique<Pool>());
    NX_CHECK(pools.back()->claim_slot() == -1);
    NX_CHECK(pools.back()->slot_exhausted() == 1);

    // The critical regression probe: repeated overflow edges (the old
    // per-record burn path) must NOT advance this pool's registration
    // high-water mark — it stays 0 while every edge is counted.
    std::size_t counted_edges = 0;
    for (std::size_t i = 0; i < 100; ++i) {
        if (pools.back()->claim_slot() == -1) {
            ++counted_edges;
        }
    }
    NX_CHECK(counted_edges == 100);
    NX_CHECK(pools.back()->slot_exhausted() == 101);
    NX_CHECK(pools.back()->active() == 0);

    // A second thread claiming the same pool takes the NEXT registration
    // slot of that pool (fetch_add after this thread's first claim) — TSan
    // must not see any race on the pool machinery. The claim result is read
    // only after an explicit join (happens-before edge).
    const std::int32_t observed = [&] {
        std::int32_t thread_slot = -2;
        std::jthread claimer([&] { thread_slot = pools.front()->claim_slot(); });
        claimer.join();
        return thread_slot;
    }();
    NX_CHECK(observed == first_slots[0] + 1);
}

// --- sink_pack suite (dedup-sink-plumbing): the variadic fan-out member
// pack that composite sinks inherit. Runtime probes via NX_CHECK plus
// compile-time contract probes via static_assert.

struct FanoutStubAlpha {
    std::size_t visit{};
};
struct FanoutStubBeta {
    std::size_t visit{};
};
struct FanoutStubGamma {
    std::size_t visit{};
};

// Non-default-constructible, move-only member (the injection-ctor probe).
struct FanoutConfiguredSink {
    int configured{0};
    int moved_from_marker{0};
    explicit FanoutConfiguredSink(int value) noexcept : configured(value) {}
    FanoutConfiguredSink(FanoutConfiguredSink&& other) noexcept
        : configured(other.configured), moved_from_marker(1) {
        other.configured = -1;
    }
    FanoutConfiguredSink(const FanoutConfiguredSink&) = delete;
    FanoutConfiguredSink& operator=(const FanoutConfiguredSink&) = delete;
    FanoutConfiguredSink& operator=(FanoutConfiguredSink&&) = delete;
};

// Unit probe: a test-only consumer mirroring how real composite sinks
// consume the pack — public inheritance plus public re-exposure of the
// protected iteration surface (the same widening idiom diagnostics use).
template <class... Sinks> struct FanoutProbe final : nexigen::core::sink_pack::Fanout<Sinks...> {
    using nexigen::core::sink_pack::Fanout<Sinks...>::Fanout;
    using nexigen::core::sink_pack::Fanout<Sinks...>::for_each;
};

using FanoutABC = FanoutProbe<FanoutStubAlpha, FanoutStubBeta, FanoutStubGamma>;

void test_sink_pack() {
    // Reference identity: index access and type access return the same member.
    FanoutABC pack{};
    NX_CHECK(&pack.template get<0>() == &pack.get<FanoutStubAlpha>());
    NX_CHECK(&pack.template get<1>() == &pack.get<FanoutStubBeta>());
    NX_CHECK(&pack.template get<2>() == &pack.get<FanoutStubGamma>());
    static_assert(FanoutABC::size == 3, "fan-out size must be the pack length");

    // Const surface: same identity, const references returned.
    const FanoutABC& const_pack = pack;
    NX_CHECK(&const_pack.template get<0>() == &const_pack.get<FanoutStubAlpha>());

    // Pack-order iteration: the comma-fold visits index 0 first, each member
    // exactly once ([expr.fold] guarantees left-to-right sequencing).
    std::size_t order = 0;
    pack.for_each([&order](auto& member) { member.visit = order++; });
    NX_CHECK(pack.get<FanoutStubAlpha>().visit == 0);
    NX_CHECK(pack.get<FanoutStubBeta>().visit == 1);
    NX_CHECK(pack.get<FanoutStubGamma>().visit == 2);

    // Empty pack: iterable zero times, size 0, default-constructible.
    FanoutProbe<> empty{};
    static_assert(nexigen::core::sink_pack::Fanout<>::size == 0);
    bool visited_empty = false;
    empty.for_each([&visited_empty](auto&) { visited_empty = true; });
    NX_CHECK(visited_empty == false);

    // combine(): first failure wins; a later success never masks it;
    // ok+ok stays ok.
    const nexigen::core::Status ok{};
    const nexigen::core::Status failing_x{std::unexpected(nexigen::core::ErrorCode::BufferFull)};
    const nexigen::core::Status failing_y{std::unexpected(nexigen::core::ErrorCode::SystemError)};
    NX_CHECK(nexigen::core::sink_pack::combine(nexigen::core::sink_pack::combine(ok, failing_x),
                                               failing_y) == failing_x);
    NX_CHECK(nexigen::core::sink_pack::combine(ok, ok).has_value());
    NX_CHECK(!nexigen::core::sink_pack::combine(failing_x, ok).has_value());
    NX_CHECK((nexigen::core::sink_pack::combine(failing_x, ok).error() ==
              nexigen::core::ErrorCode::BufferFull));

    // Member injection: a non-default-constructible, move-only configured
    // sink is moved into the tuple and keeps its state.
    FanoutProbe<FanoutConfiguredSink> configured_pack{FanoutConfiguredSink{77}};
    NX_CHECK(configured_pack.get<FanoutConfiguredSink>().configured == 77);
    NX_CHECK(configured_pack.get<0>().configured == 77);
    NX_CHECK(configured_pack.get<FanoutConfiguredSink>().moved_from_marker == 1);

    // Compile-time contract probes (declaration-level facts that CAN be
    // probed statically). The negative access case (absent/duplicate type,
    // out-of-range index) is additionally enforced in CI by an automated
    // negative-compile probe — body static_asserts are not reachable through
    // requires-expressions (the declaration substitutes cleanly), so the
    // recipe style of tests/logging/logging_test.cpp:97 and
    // tests/tracing/tracing_test.cpp:1220 graduated into a real CTest entry:
    //
    //   ctest -R nexigen_sink_pack_negative_compile
    //
    // driven by tests/core/negative_compile.sh over
    // tests/core/neg/sink_pack_negative.cpp (expected: "static assertion
    // failed" hits — duplicated type, absent type, index beyond the pack).
    // The script fails the suite if the probe TU ever compiles.

    // Default-constructibility gate (declaration-level requires-clauses are
    // probeable statically): the pack forbids default construction when any
    // member is non-default-constructible ...
    static_assert(
        !std::is_default_constructible_v<nexigen::core::sink_pack::Fanout<FanoutConfiguredSink>>);
    // ... and keeps it for all-default-constructible packs (the empty pack
    // included, so the "empty composite satisfies the sink contract" gate
    // keeps holding).
    static_assert(
        std::is_default_constructible_v<
            nexigen::core::sink_pack::Fanout<FanoutStubAlpha, FanoutStubBeta, FanoutStubGamma>>);
    static_assert(std::is_default_constructible_v<nexigen::core::sink_pack::Fanout<>>);
}

// --- sink_io suite (dedup-sink-plumbing): shared fd plumbing — write loop
// (partial writes + EINTR retry), open flag parity, fsync mapping.

// Probe storage: static ONLY — the harness replaces global operator new
// with a finite, monotonic 64 KiB arena, so probe buffers must not touch
// the heap at all (2 × 192 KiB live in .bss).
constexpr std::size_t g_sink_io_blob_size = 192U * 1024U;
alignas(16) char g_sink_io_blob[g_sink_io_blob_size]{};
alignas(16) char g_sink_io_received[g_sink_io_blob_size]{};
std::size_t g_sink_io_received_total = 0;
std::atomic<bool> g_sink_io_atomic_writer_failed{false};

void test_sink_io() {
    // Invalid descriptor: write_all maps the failure to SystemError and
    // never reports success.
    NX_CHECK(!nexigen::core::sink_io::write_all(-1, "x", 1).has_value());
    NX_CHECK((nexigen::core::sink_io::write_all(-1, "x", 1).error() ==
              nexigen::core::ErrorCode::SystemError));
    NX_CHECK(!nexigen::core::sink_io::fsync_fd(-1).has_value());

    // Open flag parity. The mode assertion runs under a pinned umask 022
    // (restored after this subsection): mode bits apply only at first
    // creation, so the pre-existence fixture can't come from fopen/other
    // creators with their own default mode.
    const mode_t previous_umask = ::umask(0022);
    const mode_t expected_mode = 0644 & ~0022;

    const char* const toggled_path = "/tmp/opencode/nexigen-sink-io-parity.txt";
    ::unlink(toggled_path); // fresh creation: the 0644 mode is applied here

    const int created = nexigen::core::sink_io::open_output(toggled_path, /*append=*/true);
    NX_CHECK(created >= 0);
    NX_CHECK(nexigen::core::sink_io::write_all(created, "hello\n", 6).has_value());
    {
        struct stat st{};
        NX_CHECK(::fstat(created, &st) == 0);
        NX_CHECK((st.st_mode & 07777U) == expected_mode);
        NX_CHECK((::fcntl(created, F_GETFD) & FD_CLOEXEC) != 0);
    }
    static_cast<void>(::close(created));

    // Truncate policy: pre-existing text replaced entirely.
    const int truncated = nexigen::core::sink_io::open_output(toggled_path, /*append=*/false);
    NX_CHECK(truncated >= 0);
    NX_CHECK(nexigen::core::sink_io::write_all(truncated, "world\n", 6).has_value());
    static_cast<void>(::close(truncated));
    {
        std::FILE* back = std::fopen(toggled_path, "r");
        char readback[16]{};
        const std::size_t got = std::fread(readback, 1, sizeof(readback), back);
        std::fclose(back);
        NX_CHECK(got == 6 && std::memcmp(readback, "world\n", 6) == 0);
    }

    // Append policy: pre-existing content preserved, new bytes appended.
    const int appended = nexigen::core::sink_io::open_output(toggled_path, /*append=*/true);
    NX_CHECK(appended >= 0);
    NX_CHECK(nexigen::core::sink_io::write_all(appended, "plus\n", 5).has_value());
    static_cast<void>(::close(appended));
    {
        std::FILE* back = std::fopen(toggled_path, "r");
        char readback[16]{};
        const std::size_t got = std::fread(readback, 1, sizeof(readback), back);
        std::fclose(back);
        NX_CHECK(got == 11 && std::memcmp(readback, "world\nplus\n", 11) == 0);
    }
    std::remove(toggled_path);
    static_cast<void>(::umask(previous_umask));

    // fsync_fd success on a real file descriptor.
    {
        const char* const fsync_path = "/tmp/opencode/nexigen-sink-io-fsync.txt";
        const int fd = ::open(fsync_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        NX_CHECK(fd >= 0);
        NX_CHECK(nexigen::core::sink_io::fsync_fd(fd).has_value());
        static_cast<void>(::close(fd));
        std::remove(fsync_path);
    }

    // Full-write loop under partial writes + EINTR: a 192 KiB blob through a
    // 64 KiB pipe cap on a throttled reader, with an interval timer firing
    // during blocked writes (SIGALRM handler without SA_RESTART ⇒ real
    // EINTR returns). The loop must finish exactly, in order, with no loss
    // and no duplication.
    //
    // Storage: file-scope static buffers ONLY — the harness replaces global
    // operator new with a finite, monotonic 64 KiB arena, so probe buffers
    // must not touch the heap at all.
    constexpr std::size_t blob_size = 192U * 1024U;
    for (std::size_t i = 0; i < blob_size; ++i) {
        g_sink_io_blob[i] = static_cast<char>((i * 31U + 7U) & 0xFFU);
    }

    int pipe_fds[2] = {-1, -1};
    NX_CHECK(::pipe2(pipe_fds, O_CLOEXEC) == 0);

    // SIGPIPE shield: the sink_io contract targets regular files (where
    // SIGPIPE never fires); the pipe probes below block it around the
    // writes only, restoring the previous mask afterwards.
    sigset_t sigpipe_mask{};
    sigemptyset(&sigpipe_mask);
    sigaddset(&sigpipe_mask, SIGPIPE);
    sigset_t old_mask{};
    ::sigprocmask(SIG_BLOCK, &sigpipe_mask, &old_mask);

    // Interval-timer wiring: a no-op handler, period 1 ms, no SA_RESTART —
    // so blocked writes genuinely return EINTR and the loop's retry branch
    // runs (repeatedly; timer disarmed before restore).
    struct sigaction def_action{};
    struct sigaction alarm_action{};
    alarm_action.sa_handler = [](int) {};
    NX_CHECK(::sigaction(SIGALRM, &alarm_action, &def_action) == 0);
    struct itimerval stop_timer{};
    struct itimerval alarm_timer{};
    alarm_timer.it_value.tv_usec = 1000;    // first tick after 1 ms
    alarm_timer.it_interval.tv_usec = 1000; // then every 1 ms
    NX_CHECK(::setitimer(ITIMER_REAL, &alarm_timer, nullptr) == 0);

    // Throttled reader: it polls so it can bail out even if the writer
    // thread failed, and it tolerates EINTR from the same timer. Works on
    // the static receive buffer (no heap).
    g_sink_io_atomic_writer_failed = false;
    g_sink_io_received_total = 0;
    std::jthread reader([&]() {
        char chunk[8192];
        int polls = 0;
        while (g_sink_io_received_total < blob_size && polls < 1000) {
            if (g_sink_io_atomic_writer_failed.load(std::memory_order_relaxed)) {
                break;
            }
            struct pollfd pfd{};
            pfd.fd = pipe_fds[0];
            pfd.events = POLLIN;
            const int ready = ::poll(&pfd, 1, 100);
            ++polls;
            if (ready <= 0) {
                continue;
            }
            const ssize_t got = ::read(pipe_fds[0], chunk, sizeof(chunk));
            if (got > 0) {
                const std::size_t room = blob_size - g_sink_io_received_total;
                const std::size_t take =
                    static_cast<std::size_t>(got) < room ? static_cast<std::size_t>(got) : room;
                std::memcpy(g_sink_io_received + g_sink_io_received_total, chunk, take);
                g_sink_io_received_total += take;
            } else if (got < 0 && errno == EINTR) {
                continue;
            } else if (got == 0) {
                break;
            }
        }
    });

    // The shared loop does the whole 192 KiB in one call.
    const nexigen::core::Status full_write =
        nexigen::core::sink_io::write_all(pipe_fds[1], g_sink_io_blob, blob_size);
    if (!full_write.has_value()) {
        g_sink_io_atomic_writer_failed.store(true, std::memory_order_relaxed);
    }
    NX_CHECK(full_write.has_value());
    NX_CHECK(!g_sink_io_atomic_writer_failed.load());

    // Disarm the timer and restore the default handler before joining.
    NX_CHECK(::setitimer(ITIMER_REAL, &stop_timer, nullptr) == 0);
    NX_CHECK(::sigaction(SIGALRM, &def_action, nullptr) == 0);
    reader.join();

    // Byte integrity: exactly the original bytes, in order, no loss, no dup.
    NX_CHECK(g_sink_io_received_total == blob_size);
    std::size_t mismatches = 0;
    if (g_sink_io_received_total == blob_size) {
        for (std::size_t i = 0; i < blob_size; ++i) {
            if (g_sink_io_received[i] != g_sink_io_blob[i]) {
                ++mismatches;
            }
        }
    }
    NX_CHECK(mismatches == 0);

    // Closed-read-end failure probe: with SIGPIPE blocked, write reports
    // EPIPE — the loop maps any unexpected errno to SystemError.
    ::close(pipe_fds[0]);
    const nexigen::core::Status epipe_probe =
        nexigen::core::sink_io::write_all(pipe_fds[1], "tail", 4);
    NX_CHECK(!epipe_probe.has_value());
    NX_CHECK((epipe_probe.error() == nexigen::core::ErrorCode::SystemError));
    ::close(pipe_fds[1]);

    // Consume the pending SIGPIPE generated by the closed-read-end probe:
    // it was blocked at delivery time, so it is still queued; restoring the
    // old mask now would deliver it with the default (lethal) disposition.
    sigset_t pipe_only{};
    sigemptyset(&pipe_only);
    sigaddset(&pipe_only, SIGPIPE);
    struct timespec reap_timeout{0, 100 * 1000 * 1000}; // 100 ms
    ::sigtimedwait(&pipe_only, nullptr, &reap_timeout);

    // Restore the previous SIGPIPE mask.
    ::sigprocmask(SIG_SETMASK, &old_mask, nullptr);
}

// --- affinity_core suite (dedup-affinity-core): the single thread-affinity
// implementation in nexigen::core::affinity. Probes are allocation-free
// (finite arena) and restore the main thread's affinity afterwards.

[[nodiscard]] std::uint64_t first_core_of(std::uint64_t mask) noexcept {
    for (std::uint64_t bit = 0; bit < 64ULL; ++bit) {
        if ((mask & (1ULL << bit)) != 0ULL) {
            return bit;
        }
    }
    return 64ULL; // empty-mask sentinel (should not happen per query contract)
}

void test_affinity_core() {
    // Fresh main-thread query: success, non-empty representable mask.
    std::uint64_t original = 0;
    NX_CHECK(nexigen::core::affinity::self_affinity(original).has_value());
    NX_CHECK(original != 0ULL);

    // Zero mask: InvalidArgument, thread state untouched.
    NX_CHECK(!nexigen::core::affinity::pin_self(0ULL).has_value());
    NX_CHECK((nexigen::core::affinity::pin_self(0ULL).error() ==
              nexigen::core::ErrorCode::InvalidArgument));
    std::uint64_t after_zero = 0;
    NX_CHECK(nexigen::core::affinity::self_affinity(after_zero).has_value());
    NX_CHECK(after_zero == original);

    const std::uint64_t single = 1ULL << first_core_of(original);
    NX_CHECK(nexigen::core::affinity::pin_self(single).has_value());
    std::uint64_t pinned = 0;
    NX_CHECK(nexigen::core::affinity::self_affinity(pinned).has_value());
    NX_CHECK(pinned == single); // round-trip pin/query agreement

    // Ungrantable CPU (host with <64 online cores — the usual CI/host case):
    // the far bit must surface SystemError, not success or an exception.
    std::uint64_t online_count = 0;
    for (std::uint64_t bit = 0; bit < 64ULL; ++bit) {
        if ((original & (1ULL << bit)) != 0ULL) {
            ++online_count;
        }
    }
    if (online_count < 64ULL) {
        NX_CHECK(!nexigen::core::affinity::pin_self(1ULL << 63).has_value());
        NX_CHECK((nexigen::core::affinity::pin_self(1ULL << 63).error() ==
                  nexigen::core::ErrorCode::SystemError));
        // The failed pin must not have changed the pinned state.
        std::uint64_t still_pinned = 0;
        NX_CHECK(nexigen::core::affinity::self_affinity(still_pinned).has_value());
        NX_CHECK(still_pinned == single);
    } else {
        std::printf("affinity_core: host has 64+ online cores; absent-CPU probe skipped\n");
    }

    // Calling-thread-only: two threads pin DIFFERENT available cores and each
    // query sees exactly its own (spawn-side pinning must not leak across).
    std::uint64_t second_bit = 64ULL;
    for (std::uint64_t bit = first_core_of(original) + 1ULL; bit < 64ULL; ++bit) {
        if ((original & (1ULL << bit)) != 0ULL) {
            second_bit = bit;
            break;
        }
    }
    if (second_bit != 64ULL) {
        const std::uint64_t core_a = first_core_of(original);
        const std::uint64_t core_b = second_bit;
        std::uint64_t seen_a = 0;
        std::uint64_t seen_b = 0;
        // Harness counters (g_checks/g_failures) are single-threaded by
        // design — worker threads only record plain flags/locals; the main
        // thread asserts after join (happens-before edge).
        std::atomic<bool> ok_a{false};
        std::atomic<bool> ok_b{false};
        std::jthread thread_a([&seen_a, &ok_a, core_a] {
            ok_a.store(nexigen::core::affinity::pin_self(1ULL << core_a).has_value() &&
                           nexigen::core::affinity::self_affinity(seen_a).has_value(),
                       std::memory_order_relaxed);
        });
        std::jthread thread_b([&seen_b, &ok_b, core_b] {
            ok_b.store(nexigen::core::affinity::pin_self(1ULL << core_b).has_value() &&
                           nexigen::core::affinity::self_affinity(seen_b).has_value(),
                       std::memory_order_relaxed);
        });
        thread_a.join();
        thread_b.join();
        NX_CHECK(ok_a.load());
        NX_CHECK(ok_b.load());
        NX_CHECK(seen_a == (1ULL << core_a));
        NX_CHECK(seen_b == (1ULL << core_b));
    } else {
        std::printf("affinity_core: single-core host; independence probe skipped\n");
    }

    // Restore the main thread's scheduler view.
    NX_CHECK(nexigen::core::affinity::pin_self(original).has_value());
}

// --- CpuSet value semantics (add-rt-thread-management).

static_assert(sizeof(nexigen::core::CpuSet) == 128);
static_assert(alignof(nexigen::core::CpuSet) == 64);
static_assert(nexigen::core::CpuSet::from_core_mask(1ULL << 63U).contains(63U));
static_assert(nexigen::core::CpuSet{}.empty());
static_assert(sizeof(nexigen::core::ThreadSpec) == 192);
static_assert(alignof(nexigen::core::ThreadSpec) == 64);
static_assert(sizeof(nexigen::core::ThreadApplyReport) == 64);
static_assert(alignof(nexigen::core::ThreadApplyReport) == 64);
static_assert(nexigen::core::to_string(nexigen::core::SchedulingPolicy::FIFO) == "fifo");
static_assert(nexigen::core::to_string(nexigen::core::ThreadApplyMode::Required) == "required");

void test_cpu_set() {
    CpuSet empty{};
    NX_CHECK(empty.empty());
    NX_CHECK(empty.count() == 0U);
    NX_CHECK(empty.to_string() == "[]");
    NX_CHECK(to_string(empty) == empty.to_string());

    // Legacy masks convert exactly, including the top representable bit.
    const CpuSet legacy = CpuSet::from_core_mask((1ULL << 0U) | (1ULL << 3U) | (1ULL << 63U));
    NX_CHECK(legacy.count() == 3U);
    NX_CHECK(legacy.contains(0U) && legacy.contains(3U) && legacy.contains(63U));
    NX_CHECK(!legacy.contains(1U) && !legacy.contains(64U));

    // IDs above 63 are first-class; the highest structural ID is valid even on
    // a host that has no such online CPU (availability is an apply-time issue).
    const std::uint32_t ids[] = {2U, 64U, 130U, 1023U};
    const Result<CpuSet> converted = CpuSet::from_cpus(ids);
    NX_CHECK(converted.has_value());
    if (converted) {
        NX_CHECK(converted->count() == 4U);
        NX_CHECK(converted->contains(2U) && converted->contains(64U));
        NX_CHECK(converted->contains(130U) && converted->contains(1023U));
    }
    NX_CHECK(!CpuSet{}.contains(1024U));

    CpuSet ranges{};
    for (const std::int64_t id : {0, 2, 3, 4, 70}) {
        NX_CHECK(ranges.add(id).has_value());
    }
    NX_CHECK(ranges.count() == 5U);
    NX_CHECK(ranges.to_string() == "0,2-4,70");

    // Invalid input is rejected instead of truncated or silently deduplicated.
    NX_CHECK(!ranges.add(-1).has_value());
    NX_CHECK(ranges.add(-1).error() == ErrorCode::InvalidArgument);
    NX_CHECK(!ranges.add(1024).has_value());
    NX_CHECK(ranges.add(1024).error() == ErrorCode::InvalidArgument);
    NX_CHECK(!ranges.add(2).has_value());
    NX_CHECK(ranges.add(2).error() == ErrorCode::InvalidArgument);
    NX_CHECK(ranges.count() == 5U); // failed insertion leaves the set intact

    const std::uint32_t duplicate[] = {8U, 8U};
    const Result<CpuSet> rejected_duplicate = CpuSet::from_cpus(duplicate);
    NX_CHECK(!rejected_duplicate.has_value());
    NX_CHECK(rejected_duplicate.error() == ErrorCode::InvalidArgument);
    const std::uint32_t too_large[] = {1024U};
    const Result<CpuSet> rejected_large = CpuSet::from_cpus(too_large);
    NX_CHECK(!rejected_large.has_value());
    NX_CHECK(rejected_large.error() == ErrorCode::InvalidArgument);
}

void test_thread_spec() {
    ThreadSpec spec{};
    NX_CHECK(spec.cpus().empty());
    NX_CHECK(spec.name().empty());
    NX_CHECK(spec.policy() == SchedulingPolicy::Other);
    NX_CHECK(spec.priority() == 0);
    NX_CHECK(spec.apply_mode() == ThreadApplyMode::BestEffort);
    NX_CHECK(!spec.scheduler_configured());
    NX_CHECK(spec.validate().has_value()); // default preserves inherited attributes

    // Explicit Other,0 is distinct from an unconfigured default scheduler.
    NX_CHECK(spec.set_scheduler(SchedulingPolicy::Other, 0).has_value());
    NX_CHECK(spec.scheduler_configured());
    NX_CHECK(spec.policy() == SchedulingPolicy::Other && spec.priority() == 0);
    NX_CHECK(spec.validate().has_value());
    spec.clear_scheduler();
    NX_CHECK(!spec.scheduler_configured());

    NX_CHECK(spec.set_scheduler(SchedulingPolicy::FIFO, 1).has_value());
    NX_CHECK(spec.validate().has_value());
    NX_CHECK(spec.set_scheduler(SchedulingPolicy::RR, 99).has_value());
    NX_CHECK(spec.validate().has_value());
    NX_CHECK(!spec.set_scheduler(SchedulingPolicy::FIFO, 0).has_value());
    NX_CHECK(!spec.set_scheduler(SchedulingPolicy::RR, 100).has_value());
    NX_CHECK(!spec.set_scheduler(SchedulingPolicy::Other, 1).has_value());
    NX_CHECK(!spec.set_scheduler(static_cast<SchedulingPolicy>(77), 1).has_value());
    NX_CHECK(spec.policy() == SchedulingPolicy::RR && spec.priority() == 99);
    NX_CHECK(spec.scheduler_configured()); // rejected setters leave state intact

    spec.set_apply_mode(ThreadApplyMode::Required);
    NX_CHECK(spec.apply_mode() == ThreadApplyMode::Required);
    NX_CHECK(spec.validate().has_value());
    spec.set_apply_mode(static_cast<ThreadApplyMode>(88));
    NX_CHECK(!spec.validate().has_value());
    NX_CHECK(spec.validate().error() == ErrorCode::InvalidArgument);
    spec.set_apply_mode(ThreadApplyMode::BestEffort);

    NX_CHECK(spec.set_name("").has_value());
    NX_CHECK(spec.name().empty());
    NX_CHECK(spec.set_name("123456789012345").has_value());
    NX_CHECK(spec.name() == "123456789012345");
    NX_CHECK(spec.name().size() == 15U);
    NX_CHECK(!spec.set_name("1234567890123456").has_value());
    NX_CHECK(spec.set_name("1234567890123456").error() == ErrorCode::InvalidArgument);
    NX_CHECK(spec.name() == "123456789012345"); // no truncation on failure
    const std::string_view embedded_nul("ab\0cd", 5);
    NX_CHECK(!spec.set_name(embedded_nul).has_value());
    NX_CHECK(spec.name() == "123456789012345");

    const CpuSet cpus = CpuSet::from_core_mask(1ULL << 2U);
    spec.set_cpus(cpus);
    NX_CHECK(spec.cpus() == cpus);

    for (const auto policy :
         {SchedulingPolicy::Other, SchedulingPolicy::FIFO, SchedulingPolicy::RR}) {
        SchedulingPolicy recovered = SchedulingPolicy::Other;
        NX_CHECK(from_string(to_string(policy), recovered));
        NX_CHECK(recovered == policy);
    }
    SchedulingPolicy unknown_policy = SchedulingPolicy::RR;
    NX_CHECK(!from_string("deadline", unknown_policy));
    NX_CHECK(unknown_policy == SchedulingPolicy::RR);
    for (const auto mode : {ThreadApplyMode::BestEffort, ThreadApplyMode::Required}) {
        ThreadApplyMode recovered = ThreadApplyMode::BestEffort;
        NX_CHECK(from_string(to_string(mode), recovered));
        NX_CHECK(recovered == mode);
    }
    ThreadApplyMode unknown_mode = ThreadApplyMode::Required;
    NX_CHECK(!from_string("optional", unknown_mode));
    NX_CHECK(unknown_mode == ThreadApplyMode::Required);
}

void test_thread_serialization() {
    const std::uint32_t ids[] = {0U, 2U, 3U, 4U, 70U};
    const Result<CpuSet> made = CpuSet::from_cpus(ids);
    NX_CHECK(made.has_value());
    const CpuSet cpus = *made;
    NX_CHECK(std::format("{}", cpus) == cpus.to_string());
    const nlohmann::json cpu_json = cpus;
    NX_CHECK(cpu_json.is_array() && cpu_json.size() == 5U);
    NX_CHECK(cpu_json[4].get<std::uint32_t>() == 70U);
    CpuSet cpu_back{};
    from_json(cpu_json, cpu_back);
    NX_CHECK(cpu_back == cpus);
    for (const nlohmann::json& bad :
         {nlohmann::json{-1}, nlohmann::json{2, 2}, nlohmann::json{1024}, nlohmann::json{"70"},
          nlohmann::json{1, nullptr}, nlohmann::json::object(), nlohmann::json(4)}) {
        CpuSet unchanged = cpus;
        from_json(bad, unchanged);
        NX_CHECK(unchanged == cpus);
    }

    for (const auto policy :
         {SchedulingPolicy::Other, SchedulingPolicy::FIFO, SchedulingPolicy::RR}) {
        NX_CHECK(std::format("{}", policy) == to_string(policy));
        const nlohmann::json j = policy;
        SchedulingPolicy back = SchedulingPolicy::Other;
        from_json(j, back);
        NX_CHECK(back == policy);
    }
    SchedulingPolicy bad_policy = SchedulingPolicy::RR;
    from_json(nlohmann::json("deadline"), bad_policy);
    NX_CHECK(bad_policy == SchedulingPolicy::RR);
    from_json(nlohmann::json(3), bad_policy);
    NX_CHECK(bad_policy == SchedulingPolicy::RR);
    for (const auto mode : {ThreadApplyMode::BestEffort, ThreadApplyMode::Required}) {
        NX_CHECK(std::format("{}", mode) == to_string(mode));
        const nlohmann::json j = mode;
        ThreadApplyMode back = ThreadApplyMode::BestEffort;
        from_json(j, back);
        NX_CHECK(back == mode);
    }
    ThreadApplyMode bad_mode = ThreadApplyMode::Required;
    from_json(nlohmann::json("optional"), bad_mode);
    NX_CHECK(bad_mode == ThreadApplyMode::Required);

    ThreadSpec spec{};
    NX_CHECK(spec.set_name("worker").has_value());
    spec.set_cpus(cpus);
    NX_CHECK(spec.set_scheduler(SchedulingPolicy::RR, 40).has_value());
    spec.set_apply_mode(ThreadApplyMode::Required);
    const std::string rendered = spec.to_string();
    NX_CHECK(rendered.find("name=worker") != std::string::npos);
    NX_CHECK(rendered.find("0,2-4,70") != std::string::npos);
    NX_CHECK(rendered.find("policy=rr") != std::string::npos);
    NX_CHECK(rendered.find("mode=required") != std::string::npos);
    NX_CHECK(to_string(spec) == rendered);
    NX_CHECK(std::format("{}", spec) == rendered);

    const nlohmann::json spec_json = spec;
    NX_CHECK(spec_json["policy"] == "rr");
    NX_CHECK(spec_json["priority"] == 40);
    NX_CHECK(spec_json["mode"] == "required");
    ThreadSpec spec_back{};
    from_json(spec_json, spec_back);
    NX_CHECK(spec_back == spec);
    NX_CHECK(spec_back.scheduler_configured());

    ThreadSpec default_spec{};
    const nlohmann::json default_json = default_spec;
    NX_CHECK(default_json.find("policy") == default_json.end());
    NX_CHECK(default_json.find("priority") == default_json.end());
    ThreadSpec default_back{};
    from_json(default_json, default_back);
    NX_CHECK(!default_back.scheduler_configured());
    NX_CHECK(default_back == default_spec);

    ThreadSpec explicit_other{};
    NX_CHECK(explicit_other.set_scheduler(SchedulingPolicy::Other, 0).has_value());
    ThreadSpec other_back{};
    from_json(nlohmann::json(explicit_other), other_back);
    NX_CHECK(other_back.scheduler_configured());
    NX_CHECK(other_back == explicit_other);

    for (const nlohmann::json& bad :
         {nlohmann::json{{"policy", "deadline"}}, nlohmann::json{{"policy", 7}},
          nlohmann::json{{"policy", "fifo"}, {"priority", 0}},
          nlohmann::json{{"policy", "rr"}, {"priority", 100}}, nlohmann::json{{"priority", -1}},
          nlohmann::json{{"name", "1234567890123456"}}, nlohmann::json{{"mode", "optional"}},
          nlohmann::json{{"cpus", nlohmann::json{64, 64}}}, nlohmann::json(7)}) {
        ThreadSpec unchanged = spec;
        from_json(bad, unchanged);
        NX_CHECK(unchanged == spec);
    }
}

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
nexigen::core::fault_injection::FaultPoint g_thread_calls[32]{};
std::size_t g_thread_call_count = 0;
std::uint32_t g_thread_fault_mask = 0;

bool record_thread_fault(nexigen::core::fault_injection::FaultPoint point) noexcept {
    if (g_thread_call_count < 32U) {
        g_thread_calls[g_thread_call_count++] = point;
    }
    const auto bit = 1U << static_cast<std::uint32_t>(point);
    return (g_thread_fault_mask & bit) != 0U;
}

void reset_thread_faults() {
    g_thread_call_count = 0;
    g_thread_fault_mask = 0;
    nexigen::core::fault_injection::set_fault_hook(record_thread_fault);
}

[[nodiscard]] bool saw_thread_call(nexigen::core::fault_injection::FaultPoint point) {
    for (std::size_t i = 0; i < g_thread_call_count; ++i) {
        if (g_thread_calls[i] == point) {
            return true;
        }
    }
    return false;
}
#endif

void test_thread_apply_failure_modes() {
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    using nexigen::core::fault_injection::FaultPoint;
    std::uint64_t original = 0;
    NX_CHECK(nexigen::core::affinity::self_affinity(original).has_value());
    const std::uint64_t core = first_core_of(original);
    NX_CHECK(core < 64U);

    auto make_spec = [&] {
        ThreadSpec spec{};
        spec.set_cpus(CpuSet::from_core_mask(1ULL << core));
        NX_CHECK(spec.set_name("rt-fault-test").has_value());
        NX_CHECK(spec.set_scheduler(SchedulingPolicy::FIFO, 1).has_value());
        return spec;
    };

    // Required: affinity succeeds, FIFO returns EPERM, and the earlier changes
    // are restored in reverse order before the error is returned.
    {
        ThreadSpec spec = make_spec();
        spec.set_apply_mode(ThreadApplyMode::Required);
        reset_thread_faults();
        g_thread_fault_mask = 1U << static_cast<std::uint32_t>(FaultPoint::Thread_set_scheduler);
        ThreadApplyReport report{};
        bool ok = true;
        std::uint64_t before_mask = 0;
        std::uint64_t after_mask = 0;
        char before_name[16]{};
        char after_name[16]{};
        std::jthread worker([&] {
            static_cast<void>(nexigen::core::affinity::self_affinity(before_mask));
            static_cast<void>(
                ::pthread_getname_np(::pthread_self(), before_name, sizeof(before_name)));
            ok = apply_thread_spec_self(spec, report).has_value();
            static_cast<void>(nexigen::core::affinity::self_affinity(after_mask));
            static_cast<void>(
                ::pthread_getname_np(::pthread_self(), after_name, sizeof(after_name)));
        });
        worker.join();
        NX_CHECK(!ok);
        NX_CHECK(report.first_error() == ErrorCode::SystemError);
        NX_CHECK(report.applied() ==
                 (ThreadApplyReport::name_mask | ThreadApplyReport::affinity_mask));
        NX_CHECK(report.failed() == ThreadApplyReport::scheduler_mask);
        NX_CHECK(report.rollback_failed() == 0U);
        NX_CHECK(before_mask == after_mask);
        NX_CHECK(std::string_view(before_name) == std::string_view(after_name));
        NX_CHECK(g_thread_call_count == 6U);
        if (g_thread_call_count == 6U) {
            NX_CHECK(g_thread_calls[0] == FaultPoint::Thread_snapshot);
            NX_CHECK(g_thread_calls[1] == FaultPoint::Thread_set_name);
            NX_CHECK(g_thread_calls[2] == FaultPoint::Thread_set_affinity);
            NX_CHECK(g_thread_calls[3] == FaultPoint::Thread_set_scheduler);
            NX_CHECK(g_thread_calls[4] == FaultPoint::Thread_restore_affinity);
            NX_CHECK(g_thread_calls[5] == FaultPoint::Thread_restore_name);
        }
    }

    // Best effort: the same EPERM is visible but leaves the successful
    // name/affinity changes in place and does not invoke rollback.
    {
        ThreadSpec spec = make_spec();
        reset_thread_faults();
        g_thread_fault_mask = 1U << static_cast<std::uint32_t>(FaultPoint::Thread_set_scheduler);
        ThreadApplyReport report{};
        bool ok = false;
        std::uint64_t after_mask = 0;
        char after_name[16]{};
        std::jthread worker([&] {
            ok = apply_thread_spec_self(spec, report).has_value();
            static_cast<void>(nexigen::core::affinity::self_affinity(after_mask));
            static_cast<void>(
                ::pthread_getname_np(::pthread_self(), after_name, sizeof(after_name)));
        });
        worker.join();
        NX_CHECK(ok);
        NX_CHECK(after_mask == (1ULL << core));
        NX_CHECK(std::string_view(after_name) == "rt-fault-test");
        NX_CHECK(report.failed() == ThreadApplyReport::scheduler_mask);
        NX_CHECK(report.applied() ==
                 (ThreadApplyReport::name_mask | ThreadApplyReport::affinity_mask));
        NX_CHECK(report.rollback_failed() == 0U);
        NX_CHECK(!saw_thread_call(FaultPoint::Thread_restore_affinity));
        NX_CHECK(!saw_thread_call(FaultPoint::Thread_restore_name));
    }

    // Independent best-effort failures do not prevent later operations.
    {
        ThreadSpec spec = make_spec();
        reset_thread_faults();
        g_thread_fault_mask = (1U << static_cast<std::uint32_t>(FaultPoint::Thread_set_name)) |
                              (1U << static_cast<std::uint32_t>(FaultPoint::Thread_set_affinity)) |
                              (1U << static_cast<std::uint32_t>(FaultPoint::Thread_set_scheduler));
        ThreadApplyReport report{};
        NX_CHECK(apply_thread_spec_self(spec, report).has_value());
        NX_CHECK(report.failed() == report.requested());
        NX_CHECK(report.applied() == 0U);
        NX_CHECK(saw_thread_call(FaultPoint::Thread_set_name));
        NX_CHECK(saw_thread_call(FaultPoint::Thread_set_affinity));
        NX_CHECK(saw_thread_call(FaultPoint::Thread_set_scheduler));
    }

    // A rollback failure is explicit rather than being reported as a clean
    // restoration. The worker exits immediately so no affinity cleanup is needed.
    {
        ThreadSpec spec = make_spec();
        spec.set_apply_mode(ThreadApplyMode::Required);
        reset_thread_faults();
        g_thread_fault_mask =
            (1U << static_cast<std::uint32_t>(FaultPoint::Thread_set_scheduler)) |
            (1U << static_cast<std::uint32_t>(FaultPoint::Thread_restore_affinity));
        ThreadApplyReport report{};
        bool ok = true;
        std::jthread worker([&] { ok = apply_thread_spec_self(spec, report).has_value(); });
        worker.join();
        NX_CHECK(!ok);
        NX_CHECK(report.rollback_failed() == ThreadApplyReport::affinity_mask);
        NX_CHECK(report.first_error() == ErrorCode::SystemError);
    }

    // Snapshot and default-spec paths: a snapshot error changes nothing, while
    // a default spec performs no attribute operation at all.
    {
        ThreadSpec spec = make_spec();
        spec.set_apply_mode(ThreadApplyMode::Required);
        reset_thread_faults();
        g_thread_fault_mask = 1U << static_cast<std::uint32_t>(FaultPoint::Thread_snapshot);
        ThreadApplyReport report{};
        NX_CHECK(!apply_thread_spec_self(spec, report).has_value());
        NX_CHECK(report.first_error() == ErrorCode::SystemError);
        NX_CHECK(!saw_thread_call(FaultPoint::Thread_set_name));

        reset_thread_faults();
        ThreadSpec defaults{};
        NX_CHECK(apply_thread_spec_self(defaults, report).has_value());
        NX_CHECK(g_thread_call_count == 0U);
    }
    nexigen::core::fault_injection::set_fault_hook(nullptr);
#else
    std::printf("thread_apply_failure_modes: fault injection disabled; skipped\n");
#endif
}

void test_thread_apply() {
    ThreadApplyReport report{};
    ThreadSpec defaults{};
    NX_CHECK(apply_thread_spec_self(defaults, report).has_value());
    NX_CHECK(report.requested() == 0U && report.applied() == 0U);
    NX_CHECK(report.complete() && report.first_error() == ErrorCode::Ok);

    ThreadSpec invalid{};
    invalid.set_apply_mode(static_cast<ThreadApplyMode>(99));
    NX_CHECK(!apply_thread_spec_self(invalid, report).has_value());
    NX_CHECK(report.first_error() == ErrorCode::InvalidArgument);
    NX_CHECK(report.applied() == 0U && report.failed() == 0U);

    std::uint64_t original = 0;
    NX_CHECK(nexigen::core::affinity::self_affinity(original).has_value());
    const std::uint64_t core = first_core_of(original);
    NX_CHECK(core < 64U);

    ThreadSpec spec{};
    spec.set_cpus(CpuSet::from_core_mask(1ULL << core));
    NX_CHECK(spec.set_name("rt-apply-test").has_value());
    NX_CHECK(spec.set_scheduler(SchedulingPolicy::Other, 0).has_value());
    spec.set_apply_mode(ThreadApplyMode::Required);

    bool applied = false;
    bool first_work_saw_attributes = false;
    std::uint64_t observed_mask = 0;
    int observed_policy = -1;
    char observed_name[16]{};
    std::jthread worker([&] {
        applied = apply_thread_spec_self(spec, report).has_value();
        // First "work" after application: every requested attribute is already
        // visible on this same thread, with no parent-side pinning window.
        first_work_saw_attributes =
            ::pthread_getname_np(::pthread_self(), observed_name, sizeof(observed_name)) == 0 &&
            nexigen::core::affinity::self_affinity(observed_mask).has_value();
        observed_policy = ::sched_getscheduler(0);
    });
    worker.join();
    NX_CHECK(applied);
    NX_CHECK(first_work_saw_attributes);
    NX_CHECK(observed_mask == (1ULL << core));
    NX_CHECK(std::string_view(observed_name) == "rt-apply-test");
    NX_CHECK(observed_policy == SCHED_OTHER);
    NX_CHECK(report.requested() ==
             (ThreadApplyReport::name_mask | ThreadApplyReport::affinity_mask |
              ThreadApplyReport::scheduler_mask));
    NX_CHECK(report.applied() == report.requested());
    NX_CHECK(report.failed() == 0U && report.rollback_failed() == 0U);
    NX_CHECK(report.complete());
}

void test_thread_report_and_publication() {
    ThreadApplyReport partial{};
    const nlohmann::json input{{"requested", 7U},
                               {"applied", 3U},
                               {"failed", 4U},
                               {"rollback_failed", 0U},
                               {"first_error", "SystemError"}};
    from_json(input, partial);
    NX_CHECK(partial.requested() == 7U && partial.applied() == 3U);
    NX_CHECK(partial.failed() == 4U && partial.rollback_failed() == 0U);
    NX_CHECK(partial.first_error() == ErrorCode::SystemError);
    NX_CHECK(!partial.complete());
    const std::string rendered = partial.to_string();
    NX_CHECK(rendered.find("applied=3") != std::string::npos);
    NX_CHECK(rendered.find("failed=4") != std::string::npos);
    NX_CHECK(rendered.find("first_error=SystemError") != std::string::npos);
    NX_CHECK(to_string(partial) == rendered);
    NX_CHECK(std::format("{}", partial) == rendered);
    const nlohmann::json output = partial;
    ThreadApplyReport recovered{};
    from_json(output, recovered);
    NX_CHECK(recovered == partial);
    for (const nlohmann::json& bad :
         {nlohmann::json{{"applied", -1}}, nlohmann::json{{"failed", "4"}},
          nlohmann::json{{"first_error", "Missing"}}, nlohmann::json{{"first_error", 4}},
          nlohmann::json(9)}) {
        ThreadApplyReport unchanged = partial;
        from_json(bad, unchanged);
        NX_CHECK(unchanged == partial);
    }

    // Startup publication pattern used by every worker integration: the report
    // is written once, then readiness is released. An acquiring reader sees the
    // complete immutable snapshot; there is no per-record atomic traffic.
    struct StartCell {
        ThreadApplyReport report{};
        std::atomic<bool> ready{false};
    } start{};
    std::uint64_t original = 0;
    NX_CHECK(nexigen::core::affinity::self_affinity(original).has_value());
    ThreadSpec spec{};
    spec.set_cpus(CpuSet::from_core_mask(1ULL << first_core_of(original)));
    std::jthread producer([&] {
        static_cast<void>(apply_thread_spec_self(spec, start.report));
        start.ready.store(true, std::memory_order_release);
    });
    while (!start.ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    producer.join();
    NX_CHECK(start.report.complete());
    NX_CHECK(start.report.applied() == ThreadApplyReport::affinity_mask);
    NX_CHECK(start.report.first_error() == ErrorCode::Ok);
}

void test_rt_integration_opt_in() {
    if (std::getenv("NEXIGEN_RT_INTEGRATION_TESTS") == nullptr) {
        std::printf("rt_integration: skipped (set NEXIGEN_RT_INTEGRATION_TESTS=1 "
                    "on a CAP_SYS_NICE RT host to enable)\n");
        return;
    }
    std::uint64_t original = 0;
    NX_CHECK(nexigen::core::affinity::self_affinity(original).has_value());
    const std::uint64_t core = first_core_of(original);
    NX_CHECK(core < 64U);
    for (const auto policy : {SchedulingPolicy::FIFO, SchedulingPolicy::RR}) {
        ThreadSpec spec{};
        spec.set_cpus(CpuSet::from_core_mask(1ULL << core));
        NX_CHECK(spec.set_name("rt-live").has_value());
        NX_CHECK(spec.set_scheduler(policy, 1).has_value());
        spec.set_apply_mode(ThreadApplyMode::Required);

        ThreadApplyReport report{};
        bool applied = false;
        std::uint64_t observed_mask = 0;
        int observed_policy = -1;
        sched_param observed_param{};
        char observed_name[16]{};
        std::jthread worker([&] {
            applied = apply_thread_spec_self(spec, report).has_value();
            static_cast<void>(nexigen::core::affinity::self_affinity(observed_mask));
            observed_policy = ::sched_getscheduler(0);
            static_cast<void>(::sched_getparam(0, &observed_param));
            static_cast<void>(
                ::pthread_getname_np(::pthread_self(), observed_name, sizeof(observed_name)));
        });
        worker.join();
        NX_CHECK(applied); // requires CAP_SYS_NICE only when explicitly enabled
        NX_CHECK(report.complete());
        NX_CHECK(observed_mask == (1ULL << core));
        NX_CHECK(std::string_view(observed_name) == "rt-live");
        NX_CHECK(observed_policy == (policy == SchedulingPolicy::FIFO ? SCHED_FIFO : SCHED_RR));
        NX_CHECK(observed_param.sched_priority == 1);
    }
}

} // namespace

int main() {
    run_suite("result", test_result);
    run_suite("json_smoke", test_json_smoke);
    run_suite("ec_vocab", test_error_code_vocabulary);
    run_suite("settings_vocab", test_settings_vocabulary);
    run_suite("ring_to_string", test_ring_to_string);
    run_suite("spsc_single_threaded", test_spsc_single_threaded);
    run_suite("spsc_stress_1m", test_spsc_stress);
    run_suite("spsc_visibility", test_spsc_visibility);
    run_suite("shard_pool_tls_cache", test_shard_pool_tls_cache);
    run_suite("sink_pack", test_sink_pack);
    run_suite("sink_io", test_sink_io);
    run_suite("affinity_core", test_affinity_core);
    run_suite("cpu_set", test_cpu_set);
    run_suite("thread_spec", test_thread_spec);
    run_suite("thread_serialization", test_thread_serialization);
    run_suite("thread_apply", test_thread_apply);
    run_suite("thread_apply_failure_modes", test_thread_apply_failure_modes);
    run_suite("thread_report_and_publication", test_thread_report_and_publication);
    run_suite("rt_integration_opt_in", test_rt_integration_opt_in);

    const int failures = g_failures;
    std::printf("total: %d checks, %d failed\n", g_checks, failures);
    return failures;
}
