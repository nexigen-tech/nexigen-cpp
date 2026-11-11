// nexigen::app — configurable suite-application example (ex_suite_app).
//
// One binary reused as several distinct instances by the nexigen-exec suite
// demo. The role and its timers come from environment variables set by the
// manifest (design D1/D5 of change add-suite-demo), while the only
// command-line argument is the settings directory the execution_manager generates for the
// instance. The workload writes all four telemetry streams and records one
// instrument of every metric kind, all ResetOnRead with a one-second
// collection cadence.
//
// Roles:
//   worker  — run a bounded workload and exit 0 (CTest smoke), or run
//             indefinitely when NEXIGEN_DEMO_RUN_FOREVER=1 (suite instance).
//   degrade — after NEXIGEN_DEMO_EXIT_AFTER_S, shut down cleanly and exit 1.
//   failure — after NEXIGEN_DEMO_ABORT_AFTER_S, std::abort() (dies by SIGABRT).
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <nexigen/app/infrastructure.hpp>
#include <nexigen/chrono/json_file_sink.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/metrics/json_file_metric_sink.hpp>
#include <nexigen/metrics/policy_enums.hpp>
#include <nexigen/tracing/json_file_trace_sink.hpp>

namespace {

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;
namespace logging = nexigen::logging;
namespace metrics = nexigen::metrics;
namespace tracing = nexigen::tracing;

using metrics::AccessPolicy;
using metrics::ConcurrencyPolicy;
using metrics::ResetPolicy;
constexpr ConcurrencyPolicy kMultiThreaded = ConcurrencyPolicy::MultiThreaded;
constexpr AccessPolicy kReadable = AccessPolicy::Readable;
constexpr ResetPolicy kResetOnRead = ResetPolicy::ResetOnRead;

// Standalone (non-suite) defaults, used by the CTest smoke run: the app
// prepares its own settings and output directories when no --global-settings
// or --local-settings directory is supplied on the command line.
constexpr std::string_view kStandaloneSettingsDir = "./nexigen-suite-demo-settings";
constexpr std::string_view kStandaloneOutputDir = "./nexigen-suite-demo-output";

// Paced workload: 10 ms ticks, one log record per 600 ms (~1.7/s), one trace
// batch span per 2 s. The background workers (collect_interval_ns = 1 s)
// deliver metric batches and chrono snapshots once per second.
constexpr std::chrono::milliseconds kTickPeriod{10};
constexpr std::chrono::milliseconds kLogPeriod{600};
constexpr std::chrono::seconds kBatchPeriod{2};

// Bounded worker run for the CTest smoke; the suite manifest sets
// NEXIGEN_DEMO_RUN_FOREVER=1 for the long-running worker instance.
constexpr std::uint64_t kDefaultWorkerSeconds = 3;
constexpr std::uint64_t kDefaultDegradeSeconds = 60;
constexpr std::uint64_t kDefaultFailureSeconds = 300;

// Explicit-bucket latency bounds in nanoseconds (1 ms .. 100 ms).
constexpr double kLatencyBounds[] = {1'000'000.0,  5'000'000.0,  10'000'000.0,
                                     20'000'000.0, 50'000'000.0, 100'000'000.0};
constexpr std::int32_t kExponentialScale = 10;

enum class DemoRole { Worker, Degrade, Failure };

struct DemoConfig {
    DemoRole role{DemoRole::Worker};
    bool run_forever{false};
    std::uint64_t worker_seconds{kDefaultWorkerSeconds};
    std::uint64_t exit_after_s{kDefaultDegradeSeconds};
    std::uint64_t abort_after_s{kDefaultFailureSeconds};
};

std::optional<std::string_view> env(std::string_view name) {
    const char* value = std::getenv(std::string(name).c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string_view{value};
}

std::uint64_t env_u64(std::string_view name, std::uint64_t fallback) {
    const std::optional<std::string_view> value = env(name);
    if (!value.has_value() || value->empty()) {
        return fallback;
    }
    std::uint64_t out = 0;
    for (const char c : *value) {
        if (c < '0' || c > '9') {
            return fallback;
        }
        out = out * 10U + static_cast<std::uint64_t>(c - '0');
    }
    return out;
}

DemoConfig load_demo_config() {
    DemoConfig config{};
    const std::optional<std::string_view> role = env("NEXIGEN_DEMO_ROLE");
    if (role.has_value()) {
        if (*role == "degrade") {
            config.role = DemoRole::Degrade;
        } else if (*role == "failure") {
            config.role = DemoRole::Failure;
        }
    }
    config.run_forever = env("NEXIGEN_DEMO_RUN_FOREVER").value_or("") == "1";
    config.worker_seconds = env_u64("NEXIGEN_DEMO_WORKER_SECONDS", kDefaultWorkerSeconds);
    config.exit_after_s = env_u64("NEXIGEN_DEMO_EXIT_AFTER_S", kDefaultDegradeSeconds);
    config.abort_after_s = env_u64("NEXIGEN_DEMO_ABORT_AFTER_S", kDefaultFailureSeconds);
    return config;
}

std::string_view role_name(DemoRole role) {
    switch (role) {
        case DemoRole::Worker:  return "worker";
        case DemoRole::Degrade: return "degrade";
        case DemoRole::Failure: return "failure";
    }
    return "worker";
}

// Stateless policy: all four sinks write into the configured directories.
struct SuitePolicy {
    using Logger = logging::FileLogger<>;
    using MetricsSink = metrics::JsonFileMetricSink;
    using TraceSink = tracing::JsonFileTraceSink;
    using Sink = chrono::JsonFileSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Settings& settings,
                                       std::string_view app_name) noexcept {
        const core::Status capacity =
            logging::check_capacity<Logger::capacity(), Logger::max_threads(), 128>(settings);
        if (!capacity) {
            return capacity;
        }
        logging::FileLoggerConfig config{};
        const core::Status applied = logging::apply_to(settings, config);
        if (!applied) {
            return applied;
        }
        config.app_name = app_name;
        config.rotation.kind = logging::RotationSchedule::None;
        out.emplace();
        return out->initialize(config);
    }

    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings& settings,
                                             std::string_view app_name) noexcept {
        if (settings.sink.path.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        metrics::JsonFileMetricSinkConfig config = settings.sink;
        config.app_name = std::string(app_name);
        out.emplace(config);
        return {};
    }

    static core::Status emplace_trace_sink(std::optional<TraceSink>& out,
                                           const tracing::Settings& settings,
                                           std::string_view app_name) noexcept {
        if (settings.sink.path.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        tracing::JsonFileTraceSinkConfig config = settings.sink;
        config.app_name = std::string(app_name);
        out.emplace(config);
        return {};
    }

    static core::Status emplace_chrono_sink(std::optional<Sink>& out,
                                            const chrono::Settings& settings) noexcept {
        if (settings.sink.path.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        out.emplace(settings.sink);
        return {};
    }
};

// Standalone settings written when no directory option is supplied. Mirrors the
// committed examples/suite/settings/ global files, plus sink paths into the
// standalone output directory.
constexpr std::string_view kLoggingSettings = R"({
  "log_root_directory": "./nexigen-suite-demo-output",
  "app_name": "ex-suite-app",
  "initial_log_level": "info",
  "spsc_ring_buffer_capacity": 128,
  "message_capacity": 128,
  "max_producer_threads": 32
}
)";

constexpr std::string_view kMetricsSettings = R"({
  "collect_interval_ns": 1000000000,
  "sink": { "path": "./nexigen-suite-demo-output/metrics.jsonl", "append": false }
}
)";

constexpr std::string_view kTracingSettings = R"({
  "max_tracers": 4,
  "sink": {
    "path": "./nexigen-suite-demo-output/traces.jsonl",
    "rotation": "None",
    "max_spans_per_window": 0,
    "window_ns": 0
  }
}
)";

constexpr std::string_view kChronoSettings = R"({
  "collect_interval_ns": 1000000000,
  "sink": { "path": "./nexigen-suite-demo-output/chrono.jsonl", "append": false }
}
)";

void print_error(std::string_view message) {
    std::fprintf(stderr, "ex_suite_app: %s\n", std::string(message).c_str());
}

int fail(std::string_view message) {
    print_error(message);
    return 1;
}

template <class Result> int fail_status(std::string_view action, const Result& status) {
    print_error(std::string(action) + " failed: " + std::string(core::to_string(status.error())));
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

bool prepare_standalone_directories() {
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(kStandaloneOutputDir), ec);
    if (ec) {
        print_error("cannot clear output directory " + std::string(kStandaloneOutputDir) + ": " +
                    ec.message());
        return false;
    }
    std::filesystem::create_directories(std::filesystem::path(kStandaloneOutputDir), ec);
    if (ec) {
        print_error("cannot create output directory " + std::string(kStandaloneOutputDir) + ": " +
                    ec.message());
        return false;
    }
    std::filesystem::create_directories(std::filesystem::path(kStandaloneSettingsDir), ec);
    if (ec) {
        print_error("cannot create settings directory " + std::string(kStandaloneSettingsDir) +
                    ": " + ec.message());
        return false;
    }
    return true;
}

bool prepare_standalone_settings() {
    const std::pair<std::string_view, std::string_view> files[] = {
        {std::string_view("logging.json"), kLoggingSettings},
        {std::string_view("metrics.json"), kMetricsSettings},
        {std::string_view("tracing.json"), kTracingSettings},
        {std::string_view("chrono.json"), kChronoSettings},
    };
    for (const auto& [name, contents] : files) {
        const std::string path = std::string(kStandaloneSettingsDir) + "/" + std::string(name);
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

} // namespace

/// Collect argv, prepare the standalone settings when no directory option was
/// passed, and parse the command line. `--help` is a caller action, not a
/// startup request: no directory preparation happens in that case.
std::optional<int> prepare_command_line(int argc, char* argv[], std::vector<std::string_view>& args,
                                        app::CommandLineOptions& options) {
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0) + 2);
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    bool help_requested = false;
    for (const std::string_view token : args) {
        if (token == "-h" || token == "--help") {
            help_requested = true;
            break;
        }
    }

    // Standalone (no directory option) prepares its own settings, mirroring the
    // execution_manager-generated instance settings for a non-suite run.
    if (!help_requested && !has_directory_option(args)) {
        if (!prepare_standalone_directories() || !prepare_standalone_settings()) {
            return 1;
        }
        args.emplace_back("--local-settings");
        args.emplace_back(kStandaloneSettingsDir);
    }

    const app::CommandLineResult parsed =
        app::parse_command_line(std::span<const std::string_view>{args});
    if (!parsed.has_value()) {
        for (const auto& diagnostic : parsed.error().diagnostics) {
            std::fprintf(stderr, "ex_suite_app: %s: %s\n", diagnostic.option.c_str(),
                         diagnostic.message.c_str());
        }
        const std::string_view usage = app::command_line_usage();
        static_cast<void>(std::fwrite(usage.data(), 1, usage.size(), stderr));
        return 2;
    }
    options = *parsed;
    if (options.help) {
        const std::string_view usage = app::command_line_usage();
        static_cast<void>(std::fwrite(usage.data(), 1, usage.size(), stdout));
        return 0;
    }
    return std::nullopt;
}

/// The concrete infrastructure this example builds (one logger, one meter tree,
/// one tracer, one chrono sink).
using Infrastructure = app::Infrastructure<SuitePolicy, 256, 8>;

/// Initialize the infrastructure and report every settings diagnostic on
/// failure; the caller surfaces the status.
core::Status initialize_infrastructure(Infrastructure& infra,
                                       const app::CommandLineOptions& options,
                                       app::Settings& loaded) {
    const core::Status initialized = infra.initialize(options, loaded);
    if (initialized) {
        return initialized;
    }
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
    return initialized;
}

/// Every instrument kind must exist; the diagnostic is printed once here.
bool instruments_created(bool counter, bool gauge, bool state, bool statistics, bool explicit_hist,
                         bool exp_hist) {
    const bool ok = counter && gauge && state && statistics && explicit_hist && exp_hist;
    if (!ok) {
        print_error("one or more instruments could not be created");
    }
    return ok;
}

/// Role deadline: a failing role aborts at its own deadline, worker/degrade
/// request a stop there, and an unbounded worker runs until a signal.
std::chrono::steady_clock::time_point
compute_deadline(const DemoConfig& config, std::chrono::steady_clock::time_point start) {
    if (config.role == DemoRole::Failure) {
        return start + std::chrono::seconds{config.abort_after_s};
    }
    if (config.role == DemoRole::Degrade) {
        return start + std::chrono::seconds{config.exit_after_s};
    }
    if (config.run_forever) {
        return std::chrono::steady_clock::time_point::max();
    }
    return start + std::chrono::seconds{config.worker_seconds};
}

int main(int argc, char* argv[]) {
    const DemoConfig config = load_demo_config();

    std::vector<std::string_view> args;
    app::CommandLineOptions options;
    const std::optional<int> early_exit = prepare_command_line(argc, argv, args, options);
    if (early_exit.has_value()) {
        return *early_exit;
    }

    auto created = app::make_infrastructure<SuitePolicy, 256, 8>();
    if (!created.has_value()) {
        return fail("make_infrastructure failed");
    }
    auto infra = std::move(*created);

    app::Settings loaded{};
    const core::Status initialized = initialize_infrastructure(*infra, options, loaded);
    if (!initialized) {
        return fail_status("Infrastructure::initialize", initialized);
    }

    infra->logger().info("suite app ready (role={})", role_name(config.role));

    const auto meter = infra->meter("suite-app", "1.0");
    if (!meter.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status("Infrastructure::meter", meter);
    }

    const auto counter = (*meter)->create_counter<kMultiThreaded, kReadable, kResetOnRead>("ticks");
    const auto gauge =
        (*meter)->create_gauge<std::int64_t, kMultiThreaded, kReadable, kResetOnRead>(
            "loop_counter");
    const auto state = (*meter)->create_state<kMultiThreaded, kReadable, kResetOnRead>("role");
    const auto statistics =
        (*meter)->create_statistics<kMultiThreaded, kReadable, kResetOnRead>("tick_latency");
    const auto explicit_hist =
        (*meter)->create_explicit_histogram<kMultiThreaded, kReadable, kResetOnRead>(
            "latency_explicit", std::span<const double>{kLatencyBounds});
    const auto exp_hist =
        (*meter)->create_exponential_histogram<kMultiThreaded, kReadable, kResetOnRead>(
            "latency_exp", kExponentialScale);
    if (!instruments_created(counter.has_value(), gauge.has_value(), state.has_value(),
                             statistics.has_value(), explicit_hist.has_value(),
                             exp_hist.has_value())) {
        static_cast<void>(infra->shutdown());
        return 1;
    }

    const auto tracer = infra->tracer_for("suite-app.loop");
    if (!tracer.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status("Infrastructure::tracer_for", tracer);
    }

    auto root_span = (*tracer)->start_span("suite-run", tracing::SpanKind::Internal);
    static_cast<void>(root_span.set_attribute("role", std::string_view{role_name(config.role)}));
    const tracing::SpanContext root_ctx = root_span.context();

    // Deadline for the role: failure aborts at its deadline, worker/degrade
    // request a stop there (or run until a signal for a forever worker).
    const auto start = std::chrono::steady_clock::now();
    const std::chrono::steady_clock::time_point deadline = compute_deadline(config, start);

    std::uint64_t ticks = 0;
    std::uint64_t batches = 0;
    auto next_tick = start;
    auto next_log = start + kLogPeriod;
    auto next_batch = start + kBatchPeriod;
    auto prev = start;

    const app::InfrastructureStopReason reason = infra->run([&]() -> bool {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            if (config.role == DemoRole::Failure) {
                // Simulate a genuine crash: no graceful shutdown, SIGABRT.
                std::abort();
            }
            return false; // request a stop for worker (bounded) / degrade
        }

        const auto elapsed_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - prev).count();
        prev = now;
        (*counter)->inc();
        (*gauge)->set(static_cast<std::int64_t>(ticks));
        static_cast<void>((*state)->set(role_name(config.role)));
        (*statistics)->record(static_cast<double>(elapsed_ns));
        (*explicit_hist)->record(static_cast<double>(elapsed_ns));
        (*exp_hist)->record(static_cast<double>(elapsed_ns));
        ++ticks;

        if (now >= next_batch) {
            auto batch_span =
                (*tracer)->start_span("tick-batch", tracing::SpanKind::Producer, &root_ctx);
            static_cast<void>(
                batch_span.set_attribute("batch", static_cast<std::int64_t>(batches)));
            static_cast<void>(batch_span.set_attribute("ticks", static_cast<std::int64_t>(ticks)));
            static_cast<void>(batch_span.set_status(tracing::SpanStatus::Ok));
            static_cast<void>(batch_span.end());
            ++batches;
            next_batch = now + kBatchPeriod;
        }

        next_tick += kTickPeriod;
        std::this_thread::sleep_until(next_tick);
        if (std::chrono::steady_clock::now() >= next_log) {
            infra->logger().info("tick {}", ticks);
            next_log = std::chrono::steady_clock::now() + kLogPeriod;
        }
        return true;
    });

    static_cast<void>(root_span.set_attribute("ticks", static_cast<std::int64_t>(ticks)));
    static_cast<void>(root_span.set_attribute("batches", static_cast<std::int64_t>(batches)));
    static_cast<void>(root_span.set_status(tracing::SpanStatus::Ok));
    static_cast<void>(root_span.end());

    const core::Status shutdown_status = infra->shutdown();
    if (!shutdown_status) {
        return fail_status("Infrastructure::shutdown", shutdown_status);
    }

    std::printf("ex_suite_app: %s (ticks=%llu, batches=%llu, reason=%s)\n",
                std::string(role_name(config.role)).c_str(), static_cast<unsigned long long>(ticks),
                static_cast<unsigned long long>(batches),
                std::string(app::to_string(reason)).c_str());

    // Degrade exits non-zero after a clean shutdown so its telemetry is
    // flushed before the execution_manager retries the attempt.
    return config.role == DemoRole::Degrade ? 1 : 0;
}
