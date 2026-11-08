// Shared harness for the nexigen::tracing test suite.
//
// Split per entity (span enums, span context, attributes, span record,
// concepts, sinks, scoped span, latency, settings, provider, record format) so
// a change recompiles one TU and a failure names its owner.

#pragma once

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
#include <dirent.h>
#include <fstream>
#include <format>
#include <limits>
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

#include "../support/test_temp.hpp"

#include <nexigen/core/result.hpp>
#include <nexigen/core/detail/fault_injection.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/chrono/utc_offset.hpp>
#include <nexigen/tracing/span_enums.hpp>
#include <nexigen/tracing/span_context.hpp>
#include <nexigen/tracing/detail/json_writer.hpp>
#include <nexigen/tracing/span_record.hpp>
#include <nexigen/tracing/detail/active_span_stack.hpp>
#include <nexigen/tracing/detail/affinity.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/null_trace_sink.hpp>
#include <nexigen/tracing/tracer_provider.hpp>
#include <nexigen/tracing/tracing_settings.hpp>
#include <nexigen/tracing/console_trace_sink.hpp>
#include <nexigen/tracing/json_file_trace_sink.hpp>
#include <nexigen/tracing/chrome_trace_sink.hpp>
#include <nexigen/tracing/composite_trace_sink.hpp>

#include <fcntl.h>
#include <sys/stat.h>

using namespace nexigen::core;  // test TU: exercise core unqualified
namespace core = nexigen::core; // and explicit core:: qualification
namespace chrono = nexigen::chrono;
namespace tracing = nexigen::tracing;

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

inline int g_checks = 0;
inline int g_failures = 0;

// Arena usage is ATOMIC: unlike the logging/metrics suites, tracing suites run
// real concurrent workers whose cold paths (to_string diagnostics, sink
// fixtures) may allocate on other threads while the main thread builds
// jthreads — a plain bump counter would hand two allocations the same bytes.
extern std::atomic<std::size_t> g_alloc_used;

/// Current arena usage (snapshot; safe from any thread).
[[nodiscard]] inline std::size_t arena_used() noexcept {
    return g_alloc_used.load(std::memory_order_relaxed);
}

#define NX_CHECK(condition)                                                                        \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(condition)) {                                                                        \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);              \
        }                                                                                          \
    } while (false)

inline void run_suite(const char* suite_name, void (*body)()) {
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
[[nodiscard]] inline std::size_t heap_in_use() noexcept {
    return static_cast<std::size_t>(mallinfo2().uordblks);
}

// --- test sinks (design D14): pre-allocated static storage, no allocation
// on the consumer path. One global record buffer serves every RecordingSink
// (suites run sequentially; instances carve disjoint base offsets so two
// sinks can coexist within a suite). Shared by the sinks, scoped-span and
// provider suites.
inline constexpr std::size_t sink_record_capacity = 48'000;
alignas(64) inline tracing::SpanRecord g_sink_records[sink_record_capacity]{};
// Bump cursor; only consumer threads write within their own slice.
inline std::size_t g_sink_record_next = 0;

/// Captures every delivered record (bounded; over-capacity counted) plus the
/// delivering worker's identity and affinity — the worker-pinning proof
/// surface for the provider suite.
struct RecordingSink {
    std::size_t base{0};
    std::size_t count{0};
    std::atomic<std::uint64_t> over_capacity{0};
    std::atomic<std::uint32_t> deliverer_tid{0};
    std::atomic<std::uint64_t> deliverer_mask{0};
    std::atomic<bool> started{false};
    std::atomic<bool> stopped{false};
    std::atomic<int> flush_calls{0};

    RecordingSink() noexcept { base = g_sink_record_next; }

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
        return "RecordingSink(count=" + std::to_string(count) + ")";
    }

    /// Read-back AFTER a flush barrier or provider destruction (the release
    /// edge of the handshake/join orders these plain writes).
    [[nodiscard]] const tracing::SpanRecord* view() const noexcept { return g_sink_records + base; }
    void reset() noexcept {
        g_sink_record_next = base; // release the slice for the next user
        count = 0;
    }
};

/// A sink whose flush always fails (error propagation through the barrier).
struct FailingFlushSink {
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) noexcept { return {}; }
    core::Status flush() noexcept { return std::unexpected(core::ErrorCode::SystemError); }
    void stop() noexcept {}
    std::string to_string() const { return "FailingFlushSink"; }
};

inline constexpr const char* sink_tmp_dir = "/tmp/opencode/nexigen-tracing-tests";

/// Read a whole file into a string (cold test utility).
[[nodiscard]] inline std::string read_file(const char* path) {
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
[[nodiscard]] inline tracing::SpanRecord crafted_record(std::uint64_t mono_start_ns,
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
    const std::size_t take = name.size() <= tracing::SpanRecord::name_capacity
                                 ? name.size()
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

// Harness self-check (задача 1.2): proves NX_CHECK, run_suite and both
// allocation counters are wired before real suites land.
inline void test_harness() {
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
