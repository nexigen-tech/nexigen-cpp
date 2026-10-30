// nexigen::app — runnable lifecycle example (change add-app-class).
//
// Demonstrates the full base-class surface:
//   1. Three per-subsystem settings pairs loaded through load_app_settings.
//   2. App::initialize() wiring logger + metric/tracing providers.
//   3. app.logger(), app.meter() and app.tracer_for() in application code.
//   4. A graceful SIGINT stop (raised from a helper thread).
//   5. shutdown() flushing everything and logging the stop reason once.
//
// Self-contained (creates its own files under /tmp) and self-verifying.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#include <signal.h>
#include <unistd.h>

#include <nexigen/app/app.hpp>
#include <nexigen/app/app_settings.hpp>
#include <nexigen/logging/composite_logger.hpp>
#include <nexigen/logging/console_logger.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/metrics/composite_metric_sink.hpp>
#include <nexigen/metrics/console_metric_sink.hpp>
#include <nexigen/metrics/json_file_metric_sink.hpp>
#include <nexigen/tracing/composite_trace_sink.hpp>
#include <nexigen/tracing/console_trace_sink.hpp>
#include <nexigen/tracing/json_file_trace_sink.hpp>

namespace {

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace logging = nexigen::logging;
namespace metrics = nexigen::metrics;
namespace tracing = nexigen::tracing;

constexpr std::string_view k_root = "/tmp/nexigen-examples-app";
constexpr std::string_view k_log_dir = "/tmp/nexigen-examples-app/logs";
constexpr std::string_view k_metrics_file = "/tmp/nexigen-examples-app/metrics.jsonl";
constexpr std::string_view k_trace_file = "/tmp/nexigen-examples-app/trace.json";
constexpr std::string_view k_app_name = "ex-app-lifecycle";

using Demo_logger =
    logging::CompositeLogger<logging::ConsoleLogger<>, logging::FileLogger<>>;
using Demo_metrics_sink =
    metrics::CompositeMetricSink<metrics::ConsoleMetricSink, metrics::JsonFileMetricSink>;
using Demo_trace_sink =
    tracing::CompositeTraceSink<tracing::ConsoleTraceSink, tracing::JsonFileTraceSink>;

class Demo_app;

using Demo_base =
    app::App<Demo_app, Demo_logger, Demo_metrics_sink, Demo_trace_sink, 256, 8>;

class Demo_app final : public Demo_base {
public:
    [[nodiscard]] core::Status emplace_logger(
        std::optional<Demo_logger>& out,
        const logging::Logging_settings& settings) noexcept {
        out.emplace();
        core::Status status =
            out->get<logging::ConsoleLogger<>>().initialize(logging::ConsoleLogger_config{});
        if (!status) {
            return status;
        }
        logging::FileLogger_config config{};
        config.app_name = settings.options.app_name;
        config.base_dir = k_log_dir;
        config.file_name = "app.jsonl";
        config.initial_level = settings.options.initial_log_level;
        config.rotation.kind = logging::Rotation_schedule::Hourly;
        config.max_file_size_bytes = 65536;
        core::Status file_status = out->get<logging::FileLogger<>>().initialize(config);
        if (!file_status) {
            return file_status;
        }
        out->set_level(settings.options.initial_log_level);
        return {};
    }

    [[nodiscard]] core::Status emplace_metrics_sink(
        std::optional<Demo_metrics_sink>& out,
        const app::Metrics_settings&) noexcept {
        out.emplace(metrics::ConsoleMetricSink{},
                    metrics::JsonFileMetricSink{metrics::JsonFileMetricSink_config{
                        std::string(k_metrics_file), true}});
        return {};
    }

    [[nodiscard]] core::Status emplace_trace_sink(
        std::optional<Demo_trace_sink>& out,
        const app::Tracing_settings&) noexcept {
        out.emplace(tracing::ConsoleTraceSink{1},
                    tracing::JsonFileTraceSink{tracing::JsonFileTraceSink_config{
                        std::string(k_trace_file), tracing::RotationPolicy::None, 0, 0}});
        return {};
    }
};

std::string find_log_file() {
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(k_log_dir, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file(ec) && entry.path().extension() == ".jsonl") {
            return entry.path().string();
        }
    }
    return {};
}

bool file_contains(const std::string& path, std::string_view needle) {
    std::ifstream in{path};
    if (!in.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void write_text(const std::string& path, std::string_view text) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

int fail(std::string_view message) {
    std::fprintf(stderr, "ex_app_lifecycle: %s\n", std::string(message).c_str());
    return 1;
}

} // namespace

int main() {
    std::error_code ec;
    std::filesystem::remove_all(k_root, ec);
    std::filesystem::create_directories(k_root, ec);
    if (ec) {
        return fail("cannot create the temporary app directory");
    }

    // --- three per-subsystem settings pairs -------------------------------
    const std::string logging_global = std::string(k_root) + "/logging.global.json";
    const std::string logging_local = std::string(k_root) + "/logging.local.json";
    const std::string metrics_global = std::string(k_root) + "/metrics.global.json";
    const std::string metrics_local = std::string(k_root) + "/metrics.local.json";
    const std::string tracing_global = std::string(k_root) + "/tracing.global.json";
    const std::string tracing_local = std::string(k_root) + "/tracing.local.json";

    write_text(logging_global,
               R"({"log_root_directory":")" + std::string(k_log_dir) +
                   R"(","initial_log_level":"info"})");
    write_text(logging_local, std::string(R"({"app_name":")") + std::string(k_app_name) +
                                  R"(","initial_log_level":"debug"})");
    write_text(metrics_global,
               R"({"core_mask":0,"collect_interval_ns":0,"max_meters":4})");
    write_text(metrics_local, R"({"max_counters":16})");
    write_text(tracing_global,
               R"({"core_mask":0,"max_tracers":4,"max_spans_per_window":0})");
    write_text(tracing_local, R"({"window_ns":0})");

    app::App_settings_paths paths{};
    paths.logging = {logging_global, logging_local};
    paths.metrics = {metrics_global, metrics_local};
    paths.tracing = {tracing_global, tracing_local};

    app::App_settings settings{};
    const core::Status loaded = app::load_app_settings(paths, settings);
    for (const auto& diagnostic : settings.metrics.diagnostics) {
        std::fprintf(stderr, "%s\n", diagnostic.to_string().c_str());
    }
    for (const auto& diagnostic : settings.tracing.diagnostics) {
        std::fprintf(stderr, "%s\n", diagnostic.to_string().c_str());
    }
    if (!loaded.has_value()) {
        return fail("failed to load app settings");
    }

    // App owns multi-megabyte providers: static storage, never the stack.
    static Demo_app application;
    if (!application.initialize(settings).has_value()) {
        return fail("App::initialize failed");
    }

    // --- logger access ----------------------------------------------------
    application.logger().info("application started from file settings");

    // --- metric factory ---------------------------------------------------
    const auto meter = application.meter("demo", "1.0");
    if (!meter.has_value()) {
        return fail("app.meter failed");
    }
    const auto ticks = (*meter)->create_counter<metrics::ConcurrencyPolicy::SingleThreaded,
                                                metrics::AccessPolicy::Readable>("ticks");
    if (!ticks.has_value()) {
        return fail("create_counter failed");
    }

    // --- tracer factory ---------------------------------------------------
    const auto tracer = application.tracer_for("demo.loop");
    if (!tracer.has_value()) {
        return fail("app.tracer_for failed");
    }
    auto loop_span = (*tracer)->start_span("lifecycle", tracing::SpanKind::Internal);

    std::jthread signal_thread{[] {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        static_cast<void>(::kill(::getpid(), SIGINT));
    }};

    std::uint64_t tick_count = 0;
    const app::App_stop_reason reason = application.run([&] {
        (*ticks)->inc();
        ++tick_count;
        application.logger().debug("tick {}", tick_count);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    });
    static_cast<void>(loop_span.set_attribute("ticks", static_cast<std::int64_t>(tick_count)));
    static_cast<void>(loop_span.end());
    signal_thread.join();

    if (reason != app::App_stop_reason::Sigint) {
        return fail("run() did not return Sigint");
    }
    if (!application.shutdown().has_value()) {
        return fail("shutdown reported a flush failure");
    }
    if (tick_count == 0U) {
        return fail("no ticks ran before the signal");
    }

    // --- verification -----------------------------------------------------
    const std::string log_file = find_log_file();
    if (log_file.empty()) {
        return fail("no log file was produced");
    }
    if (!file_contains(log_file, "shutdown reason: sigint (2)")) {
        return fail("shutdown reason was not logged");
    }
    if (!file_contains(std::string(k_metrics_file), "ticks")) {
        return fail("metric batch was not written");
    }
    if (!file_contains(std::string(k_trace_file), "lifecycle")) {
        return fail("trace span was not written");
    }

    std::printf("ex_app_lifecycle: OK (ticks=%llu, reason=%s)\n",
                static_cast<unsigned long long>(tick_count),
                std::string(app::to_string(reason)).c_str());
    return 0;
}
