// nexigen::app — command-line driven Infrastructure example.
//
// One main() prepares its own local settings and output directories, writes
// four local settings files, parses the standard command-line options, loads
// the settings through the command-line overload, and runs a fifteen-second
// paced workload that writes logs, metric batches, three nested traces and
// chrono snapshots to files.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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

// Paced workload: fifteen seconds of 10 ms ticks (~1500), one log record per
// 500 ms, one trace batch per 2 s. The background workers configured with
// collect_interval_ns = 1 s deliver metric batches and chrono snapshots.
constexpr std::chrono::seconds kRunDuration{15};
constexpr std::chrono::milliseconds kTickPeriod{10};
constexpr std::chrono::milliseconds kLogPeriod{500};
constexpr std::chrono::seconds kBatchPeriod{2};

// Self-verification floors (spec app/application "Runnable command-line
// settings example"): periodic collection and the multi-trace shape.
constexpr std::uint64_t kMinMetricBatches = 10;
constexpr std::uint64_t kMinChronoSnapshots = 10;
constexpr std::uint64_t kMinTraceIds = 3;
constexpr std::uint64_t kMinParentEdges = 1;
constexpr std::uint64_t kMinSpanKinds = 2;

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
  "collect_interval_ns": 1000000000
}
)";

// --fail-demo writes this instead of kMetricsSettings so the failure travels
// the real settings-loading path (design D2).
constexpr std::string_view kMalformedMetricsSettings = R"({"collect_interval_ns": )";

constexpr std::string_view kTracingSettings = R"({
  "max_tracers": 4
}
)";

constexpr std::string_view kChronoSettings = R"({
  "collect_interval_ns": 1000000000
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

bool prepare_settings(bool fail_demo) {
    const std::pair<std::string_view, std::string_view> files[] = {
        {std::string_view("logging.json"), kLoggingSettings},
        {std::string_view("metrics.json"),
         fail_demo ? kMalformedMetricsSettings : kMetricsSettings},
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

/// Strip the example-local demo token before the standard parser sees the
/// arguments (design D1); true when at least one occurrence was removed.
bool strip_token(std::vector<std::string_view>& args, std::string_view token) {
    bool found = false;
    std::size_t write = 0;
    for (std::size_t read = 0; read < args.size(); ++read) {
        if (args[read] == token) {
            found = true;
            continue;
        }
        args[write++] = args[read];
    }
    args.resize(write);
    return found;
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

/// Count the JSONL records in an artifact (one record per non-empty line).
bool count_lines(std::string_view path, std::uint64_t& out) {
    std::ifstream file{std::string(path)};
    if (!file) {
        print_error("missing artifact " + std::string(path));
        return false;
    }
    std::uint64_t lines = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            ++lines;
        }
    }
    out = lines;
    return true;
}

/// Count the distinct trace ids recorded in traces.jsonl.
bool count_trace_ids(std::string_view path, std::uint64_t& out) {
    std::ifstream file{std::string(path)};
    if (!file) {
        print_error("missing artifact " + std::string(path));
        return false;
    }
    constexpr std::string_view kKey = "\"trace_id\":\"";
    std::set<std::string> ids{};
    std::string line;
    while (std::getline(file, line)) {
        const std::size_t at = line.find(kKey);
        if (at == std::string::npos) {
            continue;
        }
        const std::size_t begin = at + kKey.size();
        const std::size_t end = line.find('"', begin);
        if (end == std::string::npos) {
            continue;
        }
        ids.insert(line.substr(begin, end - begin));
    }
    out = ids.size();
    return true;
}

/// Scan the recorded span shape: parent-child edges (non-zero parent_span_id)
/// and distinct span kinds (design D3).
bool scan_trace_shape(std::string_view path, std::uint64_t& edges, std::uint64_t& kinds) {
    std::ifstream file{std::string(path)};
    if (!file) {
        print_error("missing artifact " + std::string(path));
        return false;
    }
    constexpr std::string_view kParentKey = "\"parent_span_id\":\"";
    constexpr std::string_view kKindKey = "\"kind\":\"";
    constexpr std::string_view kRootParent = "0000000000000000";
    std::set<std::string> seen_kinds{};
    std::uint64_t edge_count = 0;
    std::string line;
    while (std::getline(file, line)) {
        const std::size_t parent_at = line.find(kParentKey);
        if (parent_at != std::string::npos) {
            const std::size_t begin = parent_at + kParentKey.size();
            const std::size_t end = line.find('"', begin);
            if (end != std::string::npos &&
                line.compare(begin, end - begin, kRootParent.data()) != 0) {
                ++edge_count;
            }
        }
        const std::size_t kind_at = line.find(kKindKey);
        if (kind_at != std::string::npos) {
            const std::size_t begin = kind_at + kKindKey.size();
            const std::size_t end = line.find('"', begin);
            if (end != std::string::npos) {
                seen_kinds.insert(line.substr(begin, end - begin));
            }
        }
    }
    edges = edge_count;
    kinds = seen_kinds.size();
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0) + 2);
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    // The demo token is example-local: strip it before the standard parser
    // (design D1).
    const bool fail_demo = strip_token(args, "--fail-demo");

    // Inputs are prepared before any command-line argument is applied.
    if (!prepare_directories() || !prepare_settings(fail_demo)) {
        return 1;
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

    const auto tracer = infra->tracer_for("cli-example.loop");
    if (!tracer.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status("Infrastructure::tracer_for", tracer);
    }

    // Trace 1 of 3: startup. The tracer exists only after initialize(), so the
    // root covers the post-initialization phases with explicit-parent children.
    // The instruments are MultiThreaded: a background harvester runs every
    // collect_interval, and single-threaded storage must not be harvested
    // concurrently with recording (README hot-path rules).
    using Counter_type = metrics::Counter<metrics::ConcurrencyPolicy::MultiThreaded,
                                         metrics::AccessPolicy::Readable>;
    using Gauge_type = metrics::Gauge<std::int64_t, metrics::ConcurrencyPolicy::MultiThreaded,
                                      metrics::AccessPolicy::Readable>;
    Counter_type* counter = nullptr;
    Gauge_type* gauge = nullptr;
    {
        auto startup = (*tracer)->start_span("startup", tracing::SpanKind::Internal);
        static_cast<void>(startup.set_attribute(
            "settings_dir", std::string_view{options.local_settings_dir}));
        const tracing::SpanContext startup_ctx = startup.context();
        {
            auto create = (*tracer)->start_span(
                "create-instruments", tracing::SpanKind::Internal, &startup_ctx);
            const auto meter = infra->meter("cli-example", "1.0");
            if (!meter.has_value()) {
                static_cast<void>(infra->shutdown());
                return fail_status("Infrastructure::meter", meter);
            }
            const auto made_counter =
                (*meter)->create_counter<metrics::ConcurrencyPolicy::MultiThreaded,
                                         metrics::AccessPolicy::Readable>("ticks");
            if (!made_counter.has_value()) {
                static_cast<void>(infra->shutdown());
                return fail_status("create_counter", made_counter);
            }
            const auto made_gauge =
                (*meter)->create_gauge<std::int64_t, metrics::ConcurrencyPolicy::MultiThreaded,
                                       metrics::AccessPolicy::Readable>("last_tick");
            if (!made_gauge.has_value()) {
                static_cast<void>(infra->shutdown());
                return fail_status("create_gauge", made_gauge);
            }
            counter = *made_counter;
            gauge = *made_gauge;
            static_cast<void>(
                create.set_attribute("instruments", static_cast<std::int64_t>(3)));
            static_cast<void>(create.set_status(tracing::SpanStatus::Ok));
            static_cast<void>(create.end());
        }
        {
            auto check = (*tracer)->start_span("check-settings", tracing::SpanKind::Internal,
                                               &startup_ctx);
            const std::int64_t diagnostics =
                static_cast<std::int64_t>(loaded.logging.diagnostics.size() +
                                          loaded.metrics.diagnostics.size() +
                                          loaded.tracing.diagnostics.size() +
                                          loaded.chrono.diagnostics.size());
            static_cast<void>(check.set_attribute("diagnostics", diagnostics));
            static_cast<void>(check.set_status(tracing::SpanStatus::Ok));
            static_cast<void>(check.end());
        }
        static_cast<void>(startup.set_status(tracing::SpanStatus::Ok));
        static_cast<void>(startup.end());
    }

    // Trace 2 of 3: tick-loop. Each outer iteration opens one Producer batch
    // span for up to 2 s of paced ticks and closes it with a nested Consumer
    // collect child (the per-batch collection report).
    bool workload_ok = true;
    std::uint64_t ticks = 0;
    std::uint64_t batches = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + kRunDuration;
    {
        auto loop = (*tracer)->start_span("tick-loop", tracing::SpanKind::Internal);
        static_cast<void>(loop.set_attribute(
            "settings_dir", std::string_view{options.local_settings_dir}));
        static_cast<void>(loop.set_attribute("tick_period_ms", kTickPeriod.count()));
        const tracing::SpanContext loop_ctx = loop.context();

        auto next_tick = start;
        auto next_log = start + kLogPeriod;
        while (std::chrono::steady_clock::now() < deadline) {
            auto batch = (*tracer)->start_span("tick-batch", tracing::SpanKind::Producer,
                                               &loop_ctx);
            auto batch_deadline = std::chrono::steady_clock::now() + kBatchPeriod;
            if (batch_deadline > deadline) {
                batch_deadline = deadline;
            }
            while (std::chrono::steady_clock::now() < batch_deadline) {
                counter->inc();
                gauge->set(static_cast<std::int64_t>(ticks));
                ++ticks;
                next_tick += kTickPeriod;
                std::this_thread::sleep_until(next_tick);
                if (std::chrono::steady_clock::now() >= next_log) {
                    infra->logger().info("tick {}", ticks);
                    next_log = std::chrono::steady_clock::now() + kLogPeriod;
                }
            }

            const auto batch_now = std::chrono::steady_clock::now();
            const tracing::SpanContext batch_ctx = batch.context();
            auto collect =
                (*tracer)->start_span("collect", tracing::SpanKind::Consumer, &batch_ctx);
            static_cast<void>(collect.set_attribute("batch", static_cast<std::int64_t>(batches)));
            infra->logger().info("batch {} closed at tick {}", batches, ticks);
            static_cast<void>(collect.set_status(tracing::SpanStatus::Ok));
            static_cast<void>(collect.end());

            static_cast<void>(batch.set_attribute("batch", static_cast<std::int64_t>(batches)));
            static_cast<void>(batch.set_attribute("ticks", static_cast<std::int64_t>(ticks)));
            static_cast<void>(batch.set_attribute(
                "elapsed_ms",
                std::chrono::duration_cast<std::chrono::milliseconds>(batch_now - start).count()));
            static_cast<void>(batch.set_status(tracing::SpanStatus::Ok));
            static_cast<void>(batch.end());
            ++batches;
        }

        static_cast<void>(loop.set_attribute("ticks", static_cast<std::int64_t>(ticks)));
        static_cast<void>(loop.set_attribute("batches", static_cast<std::int64_t>(batches)));
        static_cast<void>(loop.set_status(tracing::SpanStatus::Ok));
        static_cast<void>(loop.end());
    }

    // Trace 3 of 3: shutdown. The final collection pair and the last log write
    // run under explicit-parent children before shutdown flushes the sinks.
    {
        auto stop = (*tracer)->start_span("shutdown", tracing::SpanKind::Internal);
        const tracing::SpanContext stop_ctx = stop.context();
        {
            auto collect = (*tracer)->start_span("final-collect", tracing::SpanKind::Internal,
                                                 &stop_ctx);
            if (!infra->metrics().collect_now() || !infra->chrono().snapshot_now()) {
                workload_ok = false;
                print_error("final metric collection or chrono snapshot failed");
                static_cast<void>(collect.set_status(tracing::SpanStatus::Error));
            } else {
                static_cast<void>(collect.set_status(tracing::SpanStatus::Ok));
            }
            static_cast<void>(collect.end());
        }
        {
            auto flush = (*tracer)->start_span("flush-artifacts", tracing::SpanKind::Internal,
                                               &stop_ctx);
            infra->logger().info("workload finished after {} ticks", ticks);
            static_cast<void>(flush.set_attribute("ticks", static_cast<std::int64_t>(ticks)));
            static_cast<void>(flush.set_status(tracing::SpanStatus::Ok));
            static_cast<void>(flush.end());
        }
        static_cast<void>(stop.set_status(workload_ok ? tracing::SpanStatus::Ok
                                                      : tracing::SpanStatus::Error));
        static_cast<void>(stop.end());
    }

    const core::Status shutdown_status = infra->shutdown();
    if (!workload_ok) {
        return 1;
    }
    if (!shutdown_status) {
        return fail_status("Infrastructure::shutdown", shutdown_status);
    }

    if (!artifact_present(kLogPath)) {
        return 1;
    }
    std::uint64_t metric_batches = 0;
    std::uint64_t chrono_snapshots = 0;
    std::uint64_t trace_ids = 0;
    std::uint64_t parent_edges = 0;
    std::uint64_t span_kinds = 0;
    if (!count_lines(kMetricsPath, metric_batches) ||
        !count_lines(kChronoPath, chrono_snapshots) ||
        !count_trace_ids(kTracesPath, trace_ids) ||
        !scan_trace_shape(kTracesPath, parent_edges, span_kinds)) {
        return 1;
    }
    if (metric_batches < kMinMetricBatches) {
        print_error("too few metric batches: " + std::to_string(metric_batches));
        return 1;
    }
    if (chrono_snapshots < kMinChronoSnapshots) {
        print_error("too few chrono snapshots: " + std::to_string(chrono_snapshots));
        return 1;
    }
    if (trace_ids < kMinTraceIds) {
        print_error("too few distinct trace ids: " + std::to_string(trace_ids));
        return 1;
    }
    if (parent_edges < kMinParentEdges) {
        print_error("no parent-child span edges in " + std::string(kTracesPath));
        return 1;
    }
    if (span_kinds < kMinSpanKinds) {
        print_error("too few span kinds: " + std::to_string(span_kinds));
        return 1;
    }

    std::printf(
        "ex_app_cli_settings: OK (ticks=%llu, batches=%llu, metric_batches=%llu, "
        "chrono_snapshots=%llu, trace_ids=%llu, parent_edges=%llu, span_kinds=%llu, "
        "output=%s)\n",
        static_cast<unsigned long long>(ticks), static_cast<unsigned long long>(batches),
        static_cast<unsigned long long>(metric_batches),
        static_cast<unsigned long long>(chrono_snapshots),
        static_cast<unsigned long long>(trace_ids),
        static_cast<unsigned long long>(parent_edges),
        static_cast<unsigned long long>(span_kinds), std::string(kOutputDir).c_str());
    return 0;
}
