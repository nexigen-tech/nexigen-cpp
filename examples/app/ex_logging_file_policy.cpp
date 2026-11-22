// nexigen::app — runnable file-logging example.
//
// One main() creates a heap-only Infrastructure with a FileLogger policy,
// writes a flat JSONL file, exercises a second producer thread, flushes and
// verifies the produced file. The time base is the in-process fixture used by
// the other app examples; production applications attach to nexigen-chronod.
//
// Run from a build tree:
//   cmake --build build-com -j 64 --target ex_logging_file_policy
//   ./build-com/examples/app/ex_logging_file_policy
// Output:
//   ./nexigen-logging-example/app.jsonl
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <nexigen/app/infrastructure.hpp>
#include <nexigen/chrono/null_sink.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/logging/settings.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>
#include <nexigen/tracing/null_trace_sink.hpp>

#include "time_base_fixture.hpp"

namespace {

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace logging = nexigen::logging;
namespace metrics = nexigen::metrics;
namespace tracing = nexigen::tracing;

constexpr std::string_view kOutputDir = "./nexigen-logging-example";

/// Stateless policy: one FileLogger, flat directory, no rotation.
///
/// The metrics and tracing sinks are null on purpose: this example is about
/// logging, and the Infrastructure still requires all three components.
struct FilePolicy {
    using Logger = logging::FileLogger<>;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Settings& settings,
                                       const nexigen::app::TelemetryIdentity& identity) noexcept {
        const core::Status capacity =
            logging::check_capacity<Logger::capacity(), Logger::max_threads(), 128>(settings);
        if (!capacity) {
            return capacity;
        }
        logging::FileLoggerConfig cfg{};
        const core::Status applied = logging::apply_to(settings, cfg);
        if (!applied) {
            return applied;
        }
        cfg.app_name = std::string(identity.app_name);
        cfg.rotation.kind = logging::RotationSchedule::None;
        out.emplace();
        return out->initialize(cfg);
    }

    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&, const nexigen::app::TelemetryIdentity&) noexcept {
        out.emplace();
        return {};
    }

    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           const nexigen::app::TelemetryIdentity&) noexcept {
        out.emplace();
        return {};
    }
};

int fail(std::string_view message) {
    std::fprintf(stderr, "ex_logging_file_policy: %.*s\n", static_cast<int>(message.size()),
                 message.data());
    return 1;
}

std::size_t count_lines(const std::string& path) {
    std::ifstream in{path};
    std::size_t lines = 0;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) {
            ++lines;
        }
    }
    return lines;
}

} // namespace

int main() {
    nexigen::examples::LocalTimeBase time_base{"logging-file"};
    if (!time_base.is_open()) {
        return fail("local time base could not be created");
    }

    auto created = app::make_infrastructure<FilePolicy, 256, 8>();
    if (!created.has_value()) {
        return fail("make_infrastructure failed");
    }
    auto infra = std::move(*created);

    app::Settings settings = app::Settings::create("logging-example");
    settings.logging.options.log_root_directory = std::string(kOutputDir);
    settings.logging.options.initial_log_level = core::LogLevel::Debug;
    settings.logging.options.max_file_size_bytes = 0;
    settings.chrono.segment_name = time_base.name();

    const core::Status initialized = infra->initialize(settings);
    if (!initialized) {
        return fail("Infrastructure::initialize failed");
    }

    (*infra->logger())->info("infrastructure ready: app={}", std::string_view{"logging-example"});
    (*infra->logger())->debug("debug detail: shard={}", 0);
    (*infra->logger())->warning("warning goes to the file too");

    std::thread producer(
        [&infra] { (*infra->logger())->info("record from a second producer thread"); });
    producer.join();

    std::uint32_t ticks = 0;
    const app::InfrastructureStopReason reason = infra->run([&] {
        (*infra->logger())->info("tick {}", ++ticks);
        if (ticks == 3) {
            infra->request_stop(app::InfrastructureStopReason::Requested);
        }
    });
    if (reason != app::InfrastructureStopReason::Requested) {
        return fail("run() did not return Requested");
    }

    const core::Status flushed = (*infra->logger())->flush();
    if (!flushed) {
        return fail("logger flush failed");
    }
    const logging::DroppedStats dropped = (*infra->logger())->dropped();

    const core::Status shutdown = infra->shutdown();
    if (!shutdown) {
        return fail("Infrastructure::shutdown failed");
    }

    const std::string path = std::string(kOutputDir) + "/app.jsonl";
    const std::size_t lines = count_lines(path);
    std::printf("ex_logging_file_policy: OK (ticks=%u, lines=%zu, dropped=%llu, file=%s)\n", ticks,
                lines, static_cast<unsigned long long>(dropped.total()), path.c_str());
    return lines >= 6 ? 0 : 1;
}
