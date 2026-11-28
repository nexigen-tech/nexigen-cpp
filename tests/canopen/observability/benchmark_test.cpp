// Hot-path observability benchmark (change add-canopen-observability; design
// D3/D6): one Tier 1 counter increment and one error-gate decision, with the
// allocation trap armed around both.
#include "observability_test_support.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <vector>

#include <nexigen/canopen/observability/can_open_error_domain.hpp>
#include <nexigen/canopen/observability/can_open_log_policy.hpp>
#include <nexigen/canopen/observability/detail/error_gate.hpp>
#include <nexigen/canopen/observability/detail/runtime_metrics.hpp>
#include <nexigen/logging/null_logger.hpp>

namespace {

namespace obs = nexigen::canopen::observability;
namespace detail = nexigen::canopen::observability::detail;

using obs::CanOpenErrorDomain;
using obs::CanOpenLogPolicy;

/// @brief Monotonic timestamp in nanoseconds for benchmark samples.
/// @return Clock reading as a double.
[[nodiscard]] inline double now_ns() noexcept {
    timespec stamp{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &stamp);
    return static_cast<double>(stamp.tv_sec) * 1e9 + static_cast<double>(stamp.tv_nsec);
}

/// @brief Median nanoseconds per operation over several batches.
/// @tparam Body Callable taking an operation count.
/// @param body Operation loop.
/// @param batches Number of timed batches.
/// @param ops Operations per batch.
/// @return Median nanoseconds per operation.
template <class Body> double median_ns_per_op(Body&& body, int batches, int ops) {
    body(ops / 10 + 1); // warm-up
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(batches));
    for (int batch = 0; batch < batches; ++batch) {
        const double start = now_ns();
        body(ops);
        const double stop = now_ns();
        samples.push_back((stop - start) / static_cast<double>(ops));
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

/// @brief Arm the trap, run every hot operation, and return the allocation count.
/// @param metric Registered Tier 1 instruments.
/// @param gate Error gate under test.
/// @return Allocations observed while armed.
std::uint64_t allocations_on_hot_path(detail::RuntimeMetrics& metric,
                                      detail::ErrorGate<nexigen::logging::NullLogger>& gate) {
    allocation_counter_arm();
    for (int index = 0; index < 100'000; ++index) {
        metric.cycles_total->inc();
        static_cast<void>(
            gate.on_error(CanOpenErrorDomain::Bus, 0U, 1U, static_cast<std::uint64_t>(index)));
    }
    return allocation_counter_disarm();
}

} // namespace

#ifdef NEXIGEN_CANOPEN_OBSERVABILITY_STRICT_LATENCY

void run_observability_benchmark() {
    MeterFixture fixture;
    NX_CHECK(fixture.meter != nullptr);
    if (fixture.meter == nullptr) {
        return;
    }
    auto registered = detail::RuntimeMetrics::register_metrics(*fixture.meter);
    NX_CHECK(registered.has_value());
    if (!registered.has_value()) {
        return;
    }

    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 60'000U;
    policy.max_suppressed_per_key = 100'000'000U;
    nexigen::logging::NullLogger logger;
    detail::ErrorGate<nexigen::logging::NullLogger> gate{policy, logger};
    static_cast<void>(gate.on_error(CanOpenErrorDomain::Bus, 0U, 1U, 0U));

    const double counter_ns = median_ns_per_op(
        [&registered](int count) {
            for (int index = 0; index < count; ++index) {
                registered->cycles_total->inc();
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
        },
        200, 100'000);

    // One steady-state suppression decision: same key, same code, inside the
    // repeat interval. This is the common case for a persistent error.
    std::uint64_t clock_ns = 1U;
    const double gate_ns = median_ns_per_op(
        [&gate, &clock_ns](int count) {
            for (int index = 0; index < count; ++index) {
                static_cast<void>(gate.on_error(CanOpenErrorDomain::Bus, 0U, 1U, clock_ns));
                ++clock_ns;
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
        },
        200, 100'000);

    const std::uint64_t allocations = allocations_on_hot_path(*registered, gate);

    std::printf("  counter inc: %.2f ns/op (budget 10 ns)\n", counter_ns);
    std::printf("  error gate:  %.2f ns/op (budget 50 ns)\n", gate_ns);
    std::printf("  hot-path allocations: %llu\n", static_cast<unsigned long long>(allocations));
    NX_CHECK(counter_ns <= 10.0);
    NX_CHECK(gate_ns <= 50.0);
    NX_CHECK_EQ(allocations, std::uint64_t{0});
}

#else

void run_observability_benchmark() {
    MeterFixture fixture;
    NX_CHECK(fixture.meter != nullptr);
    if (fixture.meter == nullptr) {
        return;
    }
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 60'000U;
    policy.max_suppressed_per_key = 100'000'000U;
    nexigen::logging::NullLogger logger;
    detail::ErrorGate<nexigen::logging::NullLogger> gate{policy, logger};
    auto registered = detail::RuntimeMetrics::register_metrics(*fixture.meter);
    NX_CHECK(registered.has_value());
    if (!registered.has_value()) {
        return;
    }
    const std::uint64_t allocations = allocations_on_hot_path(*registered, gate);
    std::printf("  benchmark gates compiled out (correctness-only mode)\n");
    NX_CHECK_EQ(allocations, std::uint64_t{0});
}

#endif

void suite_benchmark() {
    run_observability_benchmark();
}
