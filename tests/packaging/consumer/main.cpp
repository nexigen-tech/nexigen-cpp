// Packaging smoke test: consumes the installed nexigen package only through
// find_package(nexigen CONFIG REQUIRED). Link nexigen::logging and
// nexigen::metrics; dependencies (Threads, nlohmann_json) must resolve from the
// package config.
#include <chrono>
#include <cstdint>
#include <cstdio>

#include <nexigen/logging/null_logger.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>

int main() {
    nexigen::logging::NullLogger logger{};
    logger.info("installed consumer {}", 1);

    nexigen::metrics::NullMetricSink sink{};
    nexigen::metrics::MetricProvider_config config{};
    config.collect_interval = std::chrono::nanoseconds{0};
    nexigen::metrics::MetricProvider provider{config, sink};

    const auto meter = provider.get_meter("consumer", "1.0.0");
    if (!meter) {
        std::fputs("get_meter failed\n", stderr);
        return 1;
    }

    const auto ticks = (*meter)->create_counter<
        nexigen::metrics::ConcurrencyPolicy::SingleThreaded,
        nexigen::metrics::AccessPolicy::Readable,
        nexigen::metrics::ResetPolicy::Cumulative>("ticks");
    if (!ticks) {
        std::fputs("create_counter failed\n", stderr);
        return 1;
    }

    (*ticks)->add(3);
    const std::uint64_t value = (*ticks)->read();
    if (value != 3) {
        std::fputs("counter read mismatch\n", stderr);
        return 1;
    }

    std::printf("installed consumer: ticks=%llu\n",
                static_cast<unsigned long long>(value));
    return 0;
}
