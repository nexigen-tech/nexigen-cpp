// ex_state_gauge — runnable nexigen::metrics example (change
// add-state-gauge-metric; spec metrics/state-gauge).
//
// Demonstrates the StateGauge instrument end to end:
//   1. A Meter creates state gauges (SingleThreaded and MultiThreaded).
//   2. A gauge records a current named state — an enum spelling rendered with
//      the project-wide to_string(Enum) (mandate 9). The metric system only
//      ever sees the resulting std::string_view, never the enum type.
//   3. set() rejects empty and over-long (>63 chars) values without side
//      effects (last-writer-wins semantics).
//   4. ResetPolicy::Cumulative vs ResetPolicy::ResetOnRead windows.
//   5. The provider harvests the records (collect_now) and prints them.
//
// Concurrency contract: collect_interval = 0, so no background harvester runs;
// the example quiesces writers before collect_now() observes their gauges.
//
// The example self-verifies its invariants and exits non-zero on any mismatch.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>
#include <nexigen/metrics/state_gauge.hpp>

namespace metrics = nexigen::metrics;
namespace core = nexigen::core;

namespace {

using metrics::AccessPolicy;
using metrics::ConcurrencyPolicy;
using metrics::ResetPolicy;
constexpr ConcurrencyPolicy ST = ConcurrencyPolicy::SingleThreaded;
constexpr ConcurrencyPolicy MT = ConcurrencyPolicy::MultiThreaded;
constexpr AccessPolicy Readable = AccessPolicy::Readable;
constexpr ResetPolicy Cumulative = ResetPolicy::Cumulative;
constexpr ResetPolicy ResetOnRead = ResetPolicy::ResetOnRead;

// A caller-side enum and its canonical spelling (mandate 9): the state gauge
// never depends on this type — callers render it with to_string(Enum).
enum class EngineMode : std::uint8_t {
    Idle,
    Running,
    Degraded,
};

[[nodiscard]] constexpr std::string_view to_string(EngineMode mode) noexcept {
    switch (mode) {
        case EngineMode::Idle:     return "Idle";
        case EngineMode::Running:  return "Running";
        case EngineMode::Degraded: return "Degraded";
        default:                   return "Unknown";
    }
}

int g_failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

// Sink that prints each harvested record and remembers the state records it
// saw, so the example can verify what the provider harvested.
class CaptureSink {
public:
    [[nodiscard]] core::Status on_batch(const metrics::MetricBatchSnapshot& batch) noexcept {
        for (std::uint32_t i = 0; i < batch.record_count; ++i) {
            if (batch.records == nullptr) {
                break;
            }
            std::printf("  [harvest] %s\n", batch.records[i].to_string().c_str());
            seen_.push_back(batch.records[i]);
        }
        return {};
    }

    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// Harvested value of the named state gauge (empty when never harvested).
    [[nodiscard]] std::string_view state_value(const char* name) const {
        for (const metrics::MetricRecord& record : seen_) {
            if (record.kind == metrics::MetricKind::State &&
                metrics::detail::view_name(record.identity.metric_name) == name) {
                return metrics::detail::view_name(record.state.value);
            }
        }
        return {};
    }

private:
    std::vector<metrics::MetricRecord> seen_{};
};

} // namespace

int main() {
    CaptureSink capture;
    metrics::MetricProviderConfig config{};
    config.collect_interval = std::chrono::nanoseconds{0}; // no background thread
    metrics::MetricProvider provider{config, capture};
    expect(provider.startup_status().has_value(), "provider startup");

    const core::Result<metrics::Meter*> meter = provider.get_meter("engine", "1.2.0");
    expect(meter.has_value(), "get_meter succeeds");
    if (!meter) {
        return 1;
    }

    // --- 1. Single-threaded state: an enum spelling as the current state.
    std::printf("== SingleThreaded state ==\n");
    const auto mode = (*meter)->create_state<ST, Readable, Cumulative>("engine_mode");
    expect(mode.has_value(), "state gauge created");
    if (!mode) {
        return 1;
    }

    expect((*mode)->set(to_string(EngineMode::Idle)), "set Idle accepted");
    expect((*mode)->read() == "Idle", "read returns Idle");
    expect((*mode)->set(to_string(EngineMode::Running)), "set Running accepted");
    expect((*mode)->read() == "Running", "read returns Running");
    std::printf("  mode = %.*s\n", static_cast<int>((*mode)->read().size()),
                (*mode)->read().data());

    // Last-writer-wins: the most recent set determines the current value.
    (*mode)->set(to_string(EngineMode::Degraded));
    expect((*mode)->read() == "Degraded", "last writer wins");

    // --- 2. Rejection surface: empty and over-long values are refused.
    std::printf("== Rejection surface ==\n");
    expect(!(*mode)->set(""), "empty value rejected");
    expect((*mode)->read() == "Degraded", "previous value unchanged after empty set");
    expect(!(*mode)->set(std::string_view(std::string(64, 'x'))), "over-long value rejected");
    expect((*mode)->read() == "Degraded", "previous value unchanged after over-long set");
    expect((*mode)->set(std::string_view(std::string(63, 'y'))), "exactly 63 chars accepted");
    expect((*mode)->read().size() == 63, "63-char value stored");

    // --- 3. Reset windows.
    std::printf("== Reset windows ==\n");
    (*mode)->set(to_string(EngineMode::Running));
    expect((*mode)->read() == "Running", "cumulative read reports the value");
    expect((*mode)->read() == "Running", "cumulative read persists");

    const auto phase = (*meter)->create_state<ST, Readable, ResetOnRead>("engine_phase");
    expect(phase.has_value(), "reset-on-read state gauge created");
    if (!phase) {
        return 1;
    }
    (*phase)->set("armed");
    expect((*phase)->read() == "armed", "reset-on-read reports the value");
    expect((*phase)->read().empty(), "reset-on-read clears after observation");

    // --- 4. Multi-threaded last-writer-wins with concurrent writers.
    std::printf("== MultiThreaded state ==\n");
    const auto mt_mode = (*meter)->create_state<MT, Readable, Cumulative>("engine_mode_mt");
    expect(mt_mode.has_value(), "multi-threaded state gauge created");
    if (!mt_mode) {
        return 1;
    }
    {
        std::vector<std::jthread> writers;
        writers.reserve(4);
        for (int w = 0; w < 4; ++w) {
            writers.emplace_back([&, w] {
                for (int i = 0; i < 100000; ++i) {
                    (*mt_mode)->set((w % 2 == 0) ? "even" : "odd");
                }
            });
        }
    } // all writers joined here
    (*mt_mode)->set(to_string(EngineMode::Degraded));
    expect((*mt_mode)->read() == "Degraded", "final multi-threaded set is observable");

    // --- 5. Harvest and print the records.
    std::printf("== Harvest ==\n");
    const core::Status collected = provider.collect_now();
    expect(collected.has_value(), "collect_now succeeds");
    expect(capture.state_value("engine_mode") == "Running",
           "harvested cumulative record carries the value");
    expect(capture.state_value("engine_mode_mt") == "Degraded",
           "harvested multi-threaded record carries the value");

    // --- 6. Cold-path diagnostics.
    std::printf("== Diagnostics ==\n");
    const std::string text = (*mode)->to_string();
    std::printf("  %s\n", text.c_str());
    expect(!text.empty(), "to_string() renders");
    expect(text.find("State") != std::string::npos, "diagnostics name the kind");

    std::printf("%s: %s\n", "ex_state_gauge", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
