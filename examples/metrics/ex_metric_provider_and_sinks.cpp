// ex_metric_provider_and_sinks — runnable nexigen::metrics example
// (design D1/D3/D4/D5/D7; spec metrics/examples "Provider and sink pipeline
// example").
//
// End-to-end HIL telemetry pipeline:
//   1. MetricProvider with a periodic 50 ms collect_interval and a core_mask
//      pinning the background harvester (derived from the process's allowed
//      CPU set so pinning succeeds in containers and CI).
//   2. CompositeMetricSink<ConsoleMetricSink, JsonFileMetricSink,
//      NullMetricSink> constructed with configured members (the JSON member
//      writes a per-run file under /tmp/nexigen-examples-metrics/).
//   3. Concurrent producers updating counters, gauges, descriptive statistics
//      and histograms — every instrument MultiThreaded, since the background
//      harvester observes concurrently (design D4).
//   4. flush(), clean shutdown, and cold-path read-back verification of the
//      JSONL telemetry: one parseable JSON object per line with ISO-8601 UTC
//      timestamps. Any unparsable line makes the example exit non-zero.
//
// Recording stays on the wait-free instrument paths; directory creation,
// parsing and printing are cold path.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <sched.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/metrics/composite_metric_sink.hpp>
#include <nexigen/metrics/console_metric_sink.hpp>
#include <nexigen/metrics/counter.hpp>
#include <nexigen/metrics/descriptive_statistics.hpp>
#include <nexigen/metrics/exponential_histogram.hpp>
#include <nexigen/metrics/explicit_histogram.hpp>
#include <nexigen/metrics/gauge.hpp>
#include <nexigen/metrics/json_file_metric_sink.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>

namespace metrics = nexigen::metrics;
namespace core = nexigen::core;

namespace {

using metrics::AccessPolicy;
using metrics::ConcurrencyPolicy;
using metrics::ResetPolicy;
constexpr ConcurrencyPolicy MT = ConcurrencyPolicy::MultiThreaded;
constexpr AccessPolicy Readable = AccessPolicy::Readable;
constexpr ResetPolicy Cumulative = ResetPolicy::Cumulative;

constexpr int worker_count = 4;
constexpr double latency_bounds[] = {10.0, 25.0, 50.0, 100.0, 250.0, 500.0, 1000.0};

int g_failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

/// A core_mask pinning the harvester to one permitted CPU (design D7): derived
/// from the process's allowed set so pthread_setaffinity_np succeeds anywhere.
[[nodiscard]] std::uint64_t derive_core_mask() noexcept {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (::sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        return 0U; // inherit
    }
    for (std::size_t cpu = 0; cpu < static_cast<std::size_t>(CPU_SETSIZE); ++cpu) {
        if (CPU_ISSET(cpu, &allowed)) {
            return 1ULL << (cpu > 63U ? 63U : cpu);
        }
    }
    return 0U;
}

/// YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ (nexigen::chrono rendering).
[[nodiscard]] bool iso8601_shape(const std::string& stamp) {
    if (stamp.size() != 30U || stamp[10] != 'T' || stamp[19] != '.' || stamp[29] != 'Z') {
        return false;
    }
    for (std::size_t i : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U, 11U, 12U, 14U, 15U, 17U, 18U}) {
        if (stamp[i] < '0' || stamp[i] > '9') {
            return false;
        }
    }
    for (std::size_t i = 20U; i <= 28U; ++i) {
        if (stamp[i] < '0' || stamp[i] > '9') {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    // --- Output location and per-run file (append-safe across ctest runs).
    std::error_code dir_error;
    std::filesystem::create_directories("/tmp/nexigen-examples-metrics", dir_error);
    expect(!dir_error, "output directory created");
    const std::string path = "/tmp/nexigen-examples-metrics/telemetry_" +
                             std::to_string(static_cast<long long>(::getpid())) + ".jsonl";

    // --- The composite owns configured members (design D3): console to
    // stdout, JSONL telemetry to the per-run file, and a null member.
    metrics::CompositeMetricSink<metrics::ConsoleMetricSink, metrics::JsonFileMetricSink,
                                 metrics::NullMetricSink>
        composite{metrics::ConsoleMetricSink{STDOUT_FILENO},
                  metrics::JsonFileMetricSink{metrics::JsonFileMetricSink_config{path, false}},
                  metrics::NullMetricSink{}};
    expect(composite.get<metrics::JsonFileMetricSink>().is_open(), "JSONL sink opened its file");

    const std::uint64_t core_mask = derive_core_mask();
    metrics::MetricProvider_config config{};
    config.collect_interval = std::chrono::nanoseconds{50'000'000}; // 50 ms
    config.core_mask = core_mask;
    std::printf("== HIL telemetry pipeline (core_mask=0x%llx) ==\n",
                static_cast<unsigned long long>(core_mask));

    {
        metrics::MetricProvider provider{config, composite};
        const core::Status startup = provider.startup_status();
        expect(startup.has_value(), "harvester pinned to the configured core mask");
        if (!startup) {
            std::fprintf(stderr, "  startup_status error: %d\n",
                         static_cast<int>(startup.error()));
        }

        const core::Result<metrics::Meter*> meter = provider.get_meter("hil-rig", "3.1.0");
        expect(meter.has_value(), "get_meter succeeds");
        if (!meter) {
            return 1;
        }

        const auto events = (*meter)->create_counter<MT, Readable, Cumulative>("hil_events");
        const auto temperature =
            (*meter)->create_gauge<std::int64_t, MT, Readable, Cumulative>("hil_temperature");
        const auto pressure =
            (*meter)->create_gauge<double, MT, Readable, Cumulative>("hil_pressure");
        const auto latency_stats =
            (*meter)->create_statistics<MT, Readable, Cumulative>("hil_latency_stats");
        const auto latency_ns = (*meter)->create_explicit_histogram<MT, Readable, Cumulative>(
            "hil_latency_ns", std::span<const double>{latency_bounds});
        const auto latency_wide = (*meter)->create_exponential_histogram<MT, Readable, Cumulative>(
            "hil_latency_wide", 10);
        expect(events.has_value() && temperature.has_value() && pressure.has_value() &&
                   latency_stats.has_value() && latency_ns.has_value() && latency_wide.has_value(),
               "all instruments created");
        if (!events || !temperature || !pressure || !latency_stats || !latency_ns || !latency_wide) {
            return 1;
        }

        // --- Concurrent producers across every instrument kind.
        {
            std::vector<std::jthread> producers;
            producers.reserve(worker_count);
            for (int w = 0; w < worker_count; ++w) {
                producers.emplace_back([&, w] {
                    for (int i = 0; i < 100'000; ++i) {
                        (*events)->inc();
                        (*temperature)->add(1);
                        (*pressure)->add(0.5);
                        (*latency_stats)->record(25.0 + static_cast<double>(w));
                        (*latency_ns)->record(15.0 + static_cast<double>(i % 64));
                        (*latency_wide)->record(1.0 + static_cast<double>(i % 4096));
                    }
                });
            }
        } // producers joined
        // Let the background harvester deliver several periodic batches.
        std::this_thread::sleep_for(std::chrono::milliseconds{300});

        const core::Status flushed = provider.flush();
        expect(flushed.has_value(), "flush reaches the sink");
    } // provider destroyed: stop, join, final harvest + flush

    // --- Cold-path read-back: one parseable JSON object per line, ISO-8601
    // stamps, non-decreasing in delivery order, records intact.
    std::printf("== JSONL read-back ==\n");
    std::ifstream input(path);
    expect(input.is_open(), "telemetry file exists on disk");
    std::string line;
    std::string previous_stamp;
    std::size_t lines = 0;
    bool saw_meter = false;
    bool saw_counter = false;
    bool saw_histogram = false;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        ++lines;
        const nlohmann::json parsed = nlohmann::json::parse(line, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            ++g_failures;
            std::fprintf(stderr, "FAIL: line %zu is not a parseable JSON object\n", lines);
            continue;
        }
        const auto stamp_it = parsed.find("timestamp");
        if (stamp_it == parsed.end() || !stamp_it->is_string()) {
            ++g_failures;
            std::fprintf(stderr, "FAIL: line %zu has no timestamp string\n", lines);
            continue;
        }
        const std::string stamp = stamp_it->get<std::string>();
        if (!iso8601_shape(stamp)) {
            ++g_failures;
            std::fprintf(stderr, "FAIL: line %zu stamp '%s' is not ISO-8601 UTC\n", lines,
                         stamp.c_str());
        }
        if (!previous_stamp.empty() && stamp < previous_stamp) {
            ++g_failures;
            std::fprintf(stderr, "FAIL: line %zu timestamp went backwards\n", lines);
        }
        previous_stamp = stamp;

        const auto records_it = parsed.find("records");
        if (records_it == parsed.end() || !records_it->is_array()) {
            ++g_failures;
            std::fprintf(stderr, "FAIL: line %zu has no records array\n", lines);
            continue;
        }
        for (const nlohmann::json& record : *records_it) {
            const auto meter_it = record.find("meter");
            const auto metric_it = record.find("metric");
            if (meter_it != record.end() && meter_it->is_string() &&
                meter_it->get<std::string>() == "hil-rig") {
                saw_meter = true;
            }
            if (metric_it != record.end() && metric_it->is_string()) {
                const std::string name = metric_it->get<std::string>();
                saw_counter = saw_counter || name == "hil_events";
                saw_histogram = saw_histogram || name == "hil_latency_ns";
            }
        }
    }
    std::printf("  %zu batch lines parsed from %s\n", lines, path.c_str());
    expect(lines >= 3U, "periodic batch delivery observed (multiple batches)");
    expect(saw_meter, "harvested records carry the meter identity");
    expect(saw_counter && saw_histogram, "records cover the instrument kinds");

    std::printf("ex_metric_provider_and_sinks: %s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
