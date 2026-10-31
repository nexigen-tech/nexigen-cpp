// nexigen::app — command-line driven Infrastructure example.
//
// One main() prepares its own local settings and output directories, writes
// four local settings files, parses the standard command-line options, loads
// the settings through the command-line overload, and runs a short bounded
// workload that writes logs, metrics, traces and chrono snapshots to files.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nexigen/app/infrastructure.hpp>
#include <nexigen/chrono/json_file_chrono_sink.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/metrics/json_file_metric_sink.hpp>
#include <nexigen/tracing/json_file_trace_sink.hpp>

namespace {

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;
namespace logging = nexigen::logging;
namespace metrics = nexigen::metrics;
namespace tracing = nexigen::tracing;

constexpr std::string_view kLocalSettingsDir = "./nexigen-example-settings";
constexpr std::string_view kOutputDir = "./nexigen-example-output";
constexpr std::string_view kLogPath = "./nexigen-example-output/app.jsonl";
constexpr std::string_view kMetricsPath = "./nexigen-example-output/metrics.jsonl";
constexpr std::string_view kTracesPath = "./nexigen-example-output/traces.jsonl";
constexpr std::string_view kChronoPath = "./nexigen-example-output/chrono.jsonl";

constexpr std::uint64_t kTickCap = 500;
constexpr std::uint64_t kLogEvery = 50;
constexpr std::uint64_t kCollectEvery = 100;

constexpr std::string_view kLoggingSettings = R"({
  "log_root_directory": "./nexigen-example-output",
  "app_name": "nexigen-example",
  "initial_log_level": "info",
  "spsc_ring_buffer_capacity": 128,
  "message_capacity": 128,
  "max_producer_threads": 32
}
)";

constexpr std::string_view kMetricsSettings = R"({
  "collect_interval_ns": 0
}
)";

constexpr std::string_view kTracingSettings = R"({
  "max_tracers": 4
}
)";

constexpr std::string_view kChronoSettings = R"({
  "collect_interval_ns": 0
}
)";

/// Stateless policy: all four sinks write into the fixed output directory.
struct Cli_file_policy {
    using Logger = logging::FileLogger<>;
    using Metrics_sink = metrics::JsonFileMetricSink;
    using Trace_sink = tracing::JsonFileTraceSink;
    using Chrono_sink = chrono::JsonFileChronoSink;

    static core::Status emplace_logger(
        std::optional<Logger>& out,
        const logging::Logging_settings& settings) noexcept {
        const core::Status capacity =
            logging::check_capacity<Logger::capacity(), Logger::max_threads(), 128>(settings);
        if (!capacity) {
            return capacity;
        }
        logging::FileLogger_config config{};
        const core::Status applied = logging::apply_to(settings, config);
        if (!applied) {
            return applied;
        }
        // One plain app.jsonl at the output root: no hourly subdirectories.
        config.rotation.kind = logging::Rotation_schedule::None;
        out.emplace();
        return out->initialize(config);
    }

    static core::Status emplace_metrics_sink(
        std::optional<Metrics_sink>& out,
        const app::Metrics_settings&) noexcept {
        out.emplace(metrics::JsonFileMetricSink_config{std::string(kMetricsPath), false});
        return {};
    }

    static core::Status emplace_trace_sink(
        std::optional<Trace_sink>& out,
        const app::Tracing_settings&) noexcept {
        out.emplace(tracing::JsonFileTraceSink_config{
            std::string(kTracesPath), tracing::RotationPolicy::None, 0, 0});
        return {};
    }

    static core::Status emplace_chrono_sink(
        std::optional<Chrono_sink>& out,
        const chrono::Chrono_settings&) noexcept {
        out.emplace(chrono::JsonFileChronoSink_config{std::string(kChronoPath), false});
        return {};
    }
};

void print_error(std::string_view message) {
    std::fprintf(stderr, "ex_app_cli_settings: %s\n", std::string(message).c_str());
}

int fail(std::string_view message) {
    print_error(message);
    return 1;
}

template <class Result>
int fail_status(std::string_view action, const Result& status) {
    print_error(std::string(action) + " failed: " +
                std::string(core::to_string(status.error())));
    return 1;
}

bool write_settings_file(std::string_view path, std::string_view contents) {
    std::ofstream file{std::string(path), std::ios::out | std::ios::trunc};
    if (!file) {
        print_error("cannot open settings file: " + std::string(path));
        return false;
    }
    file << contents;
    file.close();
    if (!file) {
        print_error("cannot write settings file: " + std::string(path));
        return false;
    }
    return true;
}

bool prepare_directories() {
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(kOutputDir), ec);
    if (ec) {
        print_error("cannot clear output directory " + std::string(kOutputDir) + ": " +
                    ec.message());
        return false;
    }
    std::filesystem::create_directories(std::filesystem::path(kOutputDir), ec);
    if (ec) {
        print_error("cannot create output directory " + std::string(kOutputDir) + ": " +
                    ec.message());
        return false;
    }
    std::filesystem::create_directories(std::filesystem::path(kLocalSettingsDir), ec);
    if (ec) {
        print_error("cannot create local settings directory " + std::string(kLocalSettingsDir) +
                    ": " + ec.message());
        return false;
    }
    return true;
}

bool prepare_settings() {
    const std::pair<std::string_view, std::string_view> files[] = {
        {std::string_view("logging.json"), kLoggingSettings},
        {std::string_view("metrics.json"), kMetricsSettings},
        {std::string_view("tracing.json"), kTracingSettings},
        {std::string_view("chrono.json"), kChronoSettings},
    };
    for (const auto& [name, contents] : files) {
        const std::string path = std::string(kLocalSettingsDir) + "/" + std::string(name);
        if (!write_settings_file(path, contents)) {
            return false;
        }
    }
    return true;
}

bool has_directory_option(std::span<const std::string_view> args) {
    for (const std::string_view token : args) {
        if (token == "--") {
            break;
        }
        if (token.starts_with("-gs") || token.starts_with("--global-settings") ||
            token.starts_with("-ls") || token.starts_with("--local-settings")) {
            return true;
        }
    }
    return false;
}

void print_loaded_diagnostics(const app::App_settings& loaded) {
    for (const auto& diagnostic : loaded.logging.diagnostics) {
        print_error("logging: " + diagnostic.to_string());
    }
    for (const auto& diagnostic : loaded.metrics.diagnostics) {
        print_error("metrics: " + diagnostic.to_string());
    }
    for (const auto& diagnostic : loaded.tracing.diagnostics) {
        print_error("tracing: " + diagnostic.to_string());
    }
    for (const auto& diagnostic : loaded.chrono.diagnostics) {
        print_error("chrono: " + diagnostic.to_string());
    }
}

bool artifact_present(std::string_view path) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(std::filesystem::path(path), ec);
    if (ec) {
        print_error("missing artifact " + std::string(path) + ": " + ec.message());
        return false;
    }
    if (size == 0) {
        print_error("empty artifact " + std::string(path));
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    // Inputs are prepared before any command-line argument is applied.
    if (!prepare_directories() || !prepare_settings()) {
        return 1;
    }

    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0) + 2);
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    if (!has_directory_option(args)) {
        args.emplace_back("--local-settings");
        args.emplace_back(kLocalSettingsDir);
    }

    const app::Command_line_result parsed =
        app::parse_command_line(std::span<const std::string_view>{args});
    if (!parsed.has_value()) {
        for (const auto& diagnostic : parsed.error().diagnostics) {
            std::fprintf(stderr, "ex_app_cli_settings: %s: %s\n", diagnostic.option.c_str(),
                         diagnostic.message.c_str());
        }
        const std::string_view usage = app::command_line_usage();
        static_cast<void>(std::fwrite(usage.data(), 1, usage.size(), stderr));
        return 2;
    }
    const app::Command_line_options options = *parsed;
    if (options.help) {
        const std::string_view usage = app::command_line_usage();
        static_cast<void>(std::fwrite(usage.data(), 1, usage.size(), stdout));
        return 0;
    }

    auto created = app::make_infrastructure<Cli_file_policy, 256, 8>();
    if (!created.has_value()) {
        return fail("make_infrastructure failed");
    }
    auto infra = std::move(*created);

    app::App_settings loaded{};
    const core::Status initialized = infra->initialize(options, loaded);
    if (!initialized) {
        print_loaded_diagnostics(loaded);
        return fail_status("Infrastructure::initialize", initialized);
    }

    infra->logger().info("cli settings example ready");
    infra->logger().info("local settings: {}",
                         std::string_view{options.local_settings_dir});
    infra->logger().debug("debug records require --log-level debug or a debug settings file");

    const auto meter = infra->meter("cli-example", "1.0");
    if (!meter.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status("Infrastructure::meter", meter);
    }
    const auto counter =
        (*meter)->create_counter<metrics::ConcurrencyPolicy::SingleThreaded,
                                 metrics::AccessPolicy::Readable>("ticks");
    if (!counter.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status("create_counter", counter);
    }
    const auto gauge =
        (*meter)->create_gauge<std::int64_t, metrics::ConcurrencyPolicy::SingleThreaded,
                               metrics::AccessPolicy::Readable>("last_tick");
    if (!gauge.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status("create_gauge", gauge);
    }

    const auto tracer = infra->tracer_for("cli-example.loop");
    if (!tracer.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status("Infrastructure::tracer_for", tracer);
    }

    auto span = (*tracer)->start_span("tick-loop", tracing::SpanKind::Internal);
    static_cast<void>(span.set_attribute("tick_cap", static_cast<std::int64_t>(kTickCap)));

    bool workload_ok = true;
    std::uint64_t ticks = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (ticks < kTickCap && std::chrono::steady_clock::now() < deadline) {
        (*counter)->inc();
        (*gauge)->set(static_cast<std::int64_t>(ticks));
        ++ticks;
        if (ticks % kLogEvery == 0) {
            infra->logger().info("tick {}", ticks);
        }
        if (ticks % kCollectEvery == 0) {
            if (!infra->metrics().collect_now() || !infra->chrono().snapshot_now()) {
                workload_ok = false;
                print_error("periodic metric collection or chrono snapshot failed");
                break;
            }
        }
    }

    if (workload_ok && (!infra->metrics().collect_now() || !infra->chrono().snapshot_now())) {
        workload_ok = false;
        print_error("final metric collection or chrono snapshot failed");
    }
    infra->logger().info("workload finished after {} ticks", ticks);
    static_cast<void>(span.set_attribute("ticks", static_cast<std::int64_t>(ticks)));
    static_cast<void>(span.set_status(workload_ok ? tracing::SpanStatus::Ok
                                                  : tracing::SpanStatus::Error));
    static_cast<void>(span.end());

    const core::Status shutdown_status = infra->shutdown();
    if (!workload_ok) {
        return 1;
    }
    if (!shutdown_status) {
        return fail_status("Infrastructure::shutdown", shutdown_status);
    }

    if (!artifact_present(kLogPath) || !artifact_present(kMetricsPath) ||
        !artifact_present(kTracesPath) || !artifact_present(kChronoPath)) {
        return 1;
    }

    std::printf("ex_app_cli_settings: OK (ticks=%llu, output=%s)\n",
                static_cast<unsigned long long>(ticks), std::string(kOutputDir).c_str());
    return 0;
}
