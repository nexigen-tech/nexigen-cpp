// Shared harness and fixtures for the nexigen::metrics test suite.
//
// Split per entity (policy enums, concepts, meter, counters, gauges,
// statistics, histograms, sinks, provider, serialization, latency) so a change
// recompiles only the owning TU and failures point at one file.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <malloc.h> // mallinfo2(): glibc-счётчики кучи (Linux-only проект)

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>
#include <nexigen/core/detail/fault_injection.hpp>
#include <nexigen/metrics/composite_metric_sink.hpp>
#include <nexigen/metrics/concepts/metric_concept.hpp>
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/console_metric_sink.hpp>
#include <nexigen/metrics/counter.hpp>
#include <nexigen/metrics/descriptive_statistics.hpp>
#include <nexigen/metrics/detail/affinity.hpp>
#include <nexigen/metrics/detail/thread_slot.hpp>
#include <nexigen/metrics/detail/welford.hpp>
#include <nexigen/metrics/exponential_histogram.hpp>
#include <nexigen/metrics/explicit_histogram.hpp>
#include <nexigen/metrics/gauge.hpp>
#include <nexigen/metrics/json_file_metric_sink.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/detail/json_writer.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/metrics_settings.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>
#include <nexigen/metrics/policy_enums.hpp>
#include <nexigen/metrics/state_gauge.hpp>

using nexigen::core::ErrorCode;
using nexigen::core::Result;
using nexigen::core::Status;
namespace metrics = nexigen::metrics;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;

// --- Compile-time claims (design D16): the factory hierarchy and the
// compile-time read gating are pinned before the first suite runs.
static_assert(!std::is_constructible_v<nexigen::metrics::MeterPasskey>,
              "the passkey must not be constructible by user code");
static_assert(
    !std::is_constructible_v<nexigen::metrics::Counter<
        nexigen::metrics::ConcurrencyPolicy::SingleThreaded,
        nexigen::metrics::AccessPolicy::Readable, nexigen::metrics::ResetPolicy::Cumulative>>,
    "a counter must not be constructible outside a Meter");
static_assert(
    !std::is_constructible_v<nexigen::metrics::Gauge<
        std::int64_t, nexigen::metrics::ConcurrencyPolicy::SingleThreaded,
        nexigen::metrics::AccessPolicy::Readable, nexigen::metrics::ResetPolicy::Cumulative>>,
    "a gauge must not be constructible outside a Meter");
static_assert(
    !std::is_constructible_v<nexigen::metrics::DescriptiveStatistics<
        nexigen::metrics::ConcurrencyPolicy::SingleThreaded,
        nexigen::metrics::AccessPolicy::Readable, nexigen::metrics::ResetPolicy::Cumulative>>,
    "statistics must not be constructible outside a Meter");
static_assert(
    !std::is_constructible_v<nexigen::metrics::ExplicitBucketHistogram<
        nexigen::metrics::ConcurrencyPolicy::SingleThreaded,
        nexigen::metrics::AccessPolicy::Readable, nexigen::metrics::ResetPolicy::Cumulative>>,
    "an explicit histogram must not be constructible outside a Meter");
static_assert(
    !std::is_constructible_v<nexigen::metrics::ExponentialHistogram<
        nexigen::metrics::ConcurrencyPolicy::SingleThreaded,
        nexigen::metrics::AccessPolicy::Readable, nexigen::metrics::ResetPolicy::Cumulative>>,
    "an exponential histogram must not be constructible outside a Meter");
static_assert(
    !std::is_constructible_v<nexigen::metrics::StateGauge<
        nexigen::metrics::ConcurrencyPolicy::SingleThreaded,
        nexigen::metrics::AccessPolicy::Readable, nexigen::metrics::ResetPolicy::Cumulative>>,
    "a state gauge must not be constructible outside a Meter");

// Sanitizer detection (design D16): under ASan/TSan the numeric latency gate
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

/// Policy aliases usable as template arguments (C++20 constexpr variables).
inline constexpr metrics::ConcurrencyPolicy ST = metrics::ConcurrencyPolicy::SingleThreaded;
inline constexpr metrics::ConcurrencyPolicy MT = metrics::ConcurrencyPolicy::MultiThreaded;
inline constexpr metrics::AccessPolicy Readable = metrics::AccessPolicy::Readable;
inline constexpr metrics::ResetPolicy Cumulative = metrics::ResetPolicy::Cumulative;
inline constexpr metrics::ResetPolicy ResetOnRead = metrics::ResetPolicy::ResetOnRead;

inline int g_checks = 0;
inline int g_failures = 0;

// Allocation detector (design D16): the C++ free store is re-backed by a
// fixed static bump arena; any operator new anywhere in the process bumps
// g_alloc_used. 4 MiB covers the test fixtures; hot-path code must leave
// both counters flat.
extern std::size_t g_alloc_used;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
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

/// Heap in use (bytes handed to malloc and not freed): the raw-malloc twin of
/// the arena counter above.
[[nodiscard]] inline std::size_t heap_in_use() noexcept {
    return static_cast<std::size_t>(mallinfo2().uordblks);
}

/// A sink that records what it receives (used by the provider suites).
class RecordingSink {
public:
    [[nodiscard]] core::Status on_batch(const metrics::MetricBatchSnapshot& batch) noexcept {
        ++batches_;
        records_ += batch.record_count;
        last_count_ = batch.record_count;
        if (batch.record_count > 0U && batch.records != nullptr) {
            first_ = batch.records[0];
            have_first_ = true;
        }
        return status_;
    }

    [[nodiscard]] core::Status flush() noexcept {
        ++flushes_;
        return flush_status_;
    }

    void fail_batches(core::Status status) noexcept { status_ = status; }
    void fail_flush(core::Status status) noexcept { flush_status_ = status; }

    [[nodiscard]] std::uint32_t batches() const noexcept { return batches_.load(); }
    [[nodiscard]] std::uint32_t records() const noexcept { return records_.load(); }
    [[nodiscard]] std::uint32_t flushes() const noexcept { return flushes_.load(); }
    [[nodiscard]] std::uint32_t last_count() const noexcept { return last_count_.load(); }
    [[nodiscard]] bool have_first() const noexcept { return have_first_.load(); }
    [[nodiscard]] metrics::MetricRecord first() const noexcept { return first_; }

private:
    std::atomic<std::uint32_t> batches_{0};
    std::atomic<std::uint32_t> records_{0};
    std::atomic<std::uint32_t> flushes_{0};
    std::atomic<std::uint32_t> last_count_{0};
    std::atomic<bool> have_first_{false};
    metrics::MetricRecord first_{};
    core::Status status_{};
    core::Status flush_status_{};
};

// --- harness suite: proves NX_CHECK, run_suite and both allocation counters
// are wired before the real suites land.
inline void test_harness() {
    NX_CHECK(g_checks > 0);
    const std::size_t arena_before = g_alloc_used;
    const std::size_t heap_before = heap_in_use();
    void* p = ::operator new(64U);
    ::operator delete(p);
    NX_CHECK(g_alloc_used == arena_before + 64U);
    NX_CHECK(heap_in_use() == heap_before);
}
