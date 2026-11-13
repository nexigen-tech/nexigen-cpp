// nexigen::integration — generator application (change add-suite-2-apps;
// spec examples/suite-2-apps).
//
// Produces one DataSample per second and publishes it over nexigen::com with a
// tracing::TraceHeader carrying the per-sample trace, so the receiver can link
// its spans to the generator's. Telemetry (logs, metrics, spans) is published
// to the three daemons through the iceoryx2 sinks; spans additionally land in a
// JSON-lines file via a composite sink so the shared trace id is observable.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#include <nexigen/chrono/null_sink.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/iceoryx2_logger.hpp>
#include <nexigen/logging/iceoryx2_logger_config.hpp>
#include <nexigen/metrics/iceoryx2_metric_sink.hpp>
#include <nexigen/metrics/iceoryx2_metric_sink_config.hpp>
#include <nexigen/metrics/policy_enums.hpp>
#include <nexigen/tracing/iceoryx2_trace_sink.hpp>
#include <nexigen/tracing/iceoryx2_trace_sink_config.hpp>
#include <nexigen/tracing/scoped_span.hpp>
#include <nexigen/tracing/span_kind.hpp>
#include <nexigen/tracing/span_status.hpp>
#include <nexigen/tracing/trace_header.hpp>

#include "data_sample.hpp"

namespace {

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;
namespace logging = nexigen::logging;
namespace metrics = nexigen::metrics;
namespace tracing = nexigen::tracing;
namespace com = nexigen::com;

using metrics::AccessPolicy;
using metrics::ConcurrencyPolicy;
using metrics::ResetPolicy;
constexpr ConcurrencyPolicy kMultiThreaded = ConcurrencyPolicy::MultiThreaded;
constexpr AccessPolicy kReadable = AccessPolicy::Readable;
constexpr ResetPolicy kResetOnRead = ResetPolicy::ResetOnRead;

constexpr std::string_view kDataService = "Nexigen/Demo/DataStream";
constexpr std::string_view kMessage = "hello-from-generator";

// Standalone (non-suite) defaults, used by the CTest smoke run.
constexpr std::string_view kStandaloneSettingsDir = "./nexigen-suite-2-apps-settings";
constexpr std::string_view kStandaloneOutputDir = "./nexigen-suite-2-apps-output";
constexpr std::uint64_t kDefaultWorkerSeconds = 2;

// Shared telemetry node: the iceoryx2 metric/trace sinks borrow a com::Node that
// must outlive them. A function-local static lives for the process lifetime,
// which outlives the heap-allocated Infrastructure (a single instance per run).
com::Node* telemetry_node() noexcept {
    static std::optional<com::Node> node = []() -> std::optional<com::Node> {
        auto created = com::Node::create();
        if (!created) {
            return std::nullopt;
        }
        return std::optional<com::Node>{std::move(*created)};
    }();
    return node.has_value() ? &*node : nullptr;
}

// Stateless policy: telemetry reaches the daemons over iceoryx2. The logger
// creates its own node internally; the metric/trace sinks borrow the shared
// telemetry node.
struct SuitePolicy {
    using Logger = logging::Iceoryx2Logger<>;
    using MetricsSink = metrics::Iceoryx2MetricSink;
    using TraceSink = tracing::Iceoryx2TraceSink;
    using Sink = chrono::NullSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Settings& settings,
                                       std::string_view) noexcept {
        logging::Iceoryx2LoggerConfig config{};
        config.service_name = "Nexigen/Log/nexigen";
        config.app_id = "GENS";
        config.initial_level = settings.options.initial_log_level;
        config.worker_thread = settings.options.worker_thread;
        config.worker_poll_interval_us = settings.options.worker_poll_interval_us;
        out.emplace();
        return out->initialize(config);
    }

    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&,
                                             std::string_view) noexcept {
        com::Node* node = telemetry_node();
        if (node == nullptr) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        metrics::Iceoryx2MetricSinkConfig config{};
        config.service_name = "nexigen/metrics/stream";
        config.app_id = "GENS";
        out.emplace(config, node);
        return out->start();
    }

    static core::Status emplace_trace_sink(std::optional<TraceSink>& out,
                                           const tracing::Settings&,
                                           std::string_view) noexcept {
        com::Node* node = telemetry_node();
        if (node == nullptr) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        tracing::Iceoryx2TraceSinkConfig config{};
        config.service_name = "nexigen/trace/stream";
        config.app_id = "GENS";
        out.emplace(config, node);
        return {};
    }

    static core::Status emplace_chrono_sink(std::optional<Sink>& out,
                                            const chrono::Settings&) noexcept {
        out.emplace();
        return {};
    }
};

constexpr std::string_view kLoggingSettings = R"({
  "initial_log_level": "info",
  "spsc_ring_buffer_capacity": 128,
  "message_capacity": 128,
  "max_producer_threads": 32
}
)";

constexpr std::string_view kMetricsSettings = R"({
  "collect_interval_ns": 1000000000,
  "max_counters": 16,
  "max_gauges": 16
}
)";

constexpr std::string_view kTracingSettings = R"({
  "max_tracers": 4
}
)";

constexpr std::string_view kChronoSettings = R"({
  "collect_interval_ns": 1000000000
}
)";

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

void print_error(std::string_view message) {
    std::fprintf(stderr, "ex_generator: %s\n", std::string(message).c_str());
}

int fail(std::string_view message) {
    print_error(message);
    return 1;
}

std::string_view render_error(core::ErrorCode error) noexcept { return core::to_string(error); }
std::string_view render_error(com::Error error) noexcept { return com::to_string(error); }

template <class Result> int fail_status(std::string_view action, const Result& status) {
    print_error(std::string(action) + " failed: " + std::string(render_error(status.error())));
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
    return !file.fail();
}

bool prepare_standalone() {
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(kStandaloneOutputDir), ec);
    std::filesystem::create_directories(std::filesystem::path(kStandaloneOutputDir), ec);
    std::filesystem::create_directories(std::filesystem::path(kStandaloneSettingsDir), ec);
    const std::pair<std::string_view, std::string_view> files[] = {
        {"logging.json", kLoggingSettings},
        {"metrics.json", kMetricsSettings},
        {"tracing.json", kTracingSettings},
        {"chrono.json", kChronoSettings},
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
    if (!help_requested && !has_directory_option(args)) {
        if (!prepare_standalone()) {
            return 1;
        }
        args.emplace_back("--local-settings");
        args.emplace_back(kStandaloneSettingsDir);
    }
    const app::CommandLineResult parsed =
        app::parse_command_line(std::span<const std::string_view>{args});
    if (!parsed.has_value()) {
        for (const auto& diagnostic : parsed.error().diagnostics) {
            std::fprintf(stderr, "ex_generator: %s: %s\n", diagnostic.option.c_str(),
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

using Infrastructure = app::Infrastructure<SuitePolicy, 256, 8>;

core::Status initialize_infrastructure(Infrastructure& infra, const app::CommandLineOptions& options,
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

} // namespace

int main(int argc, char* argv[]) {
    const bool run_forever = env("NEXIGEN_RUN_FOREVER").value_or("") == "1";
    const std::uint64_t worker_seconds = env_u64("NEXIGEN_WORKER_SECONDS", kDefaultWorkerSeconds);

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

    infra->logger().info("generator ready");

    const auto meter = infra->meter("generator", "1.0");
    if (!meter.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status("Infrastructure::meter", meter);
    }
    const auto counter =
        (*meter)->create_counter<kMultiThreaded, kReadable, kResetOnRead>("samples_sent");
    const auto gauge =
        (*meter)->create_gauge<std::int64_t, kMultiThreaded, kReadable, kResetOnRead>(
            "last_sequence");
    if (!counter.has_value() || !gauge.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail("one or more instruments could not be created");
    }

    const auto tracer = infra->tracer_for("generator.loop");
    if (!tracer.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status("Infrastructure::tracer_for", tracer);
    }

    auto node = com::Node::create();
    if (!node.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status("com::Node::create", node);
    }
    auto service = com::ServiceName::create(std::string(kDataService).c_str());
    if (!service.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status("com::ServiceName::create", service);
    }
    auto publisher = com::Publisher<suite2_apps::DataSample, tracing::TraceHeader>::create(
        *node, *service);
    if (!publisher.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status("com::Publisher::create", publisher);
    }

    const auto start = std::chrono::steady_clock::now();
    const std::chrono::steady_clock::time_point deadline =
        run_forever ? std::chrono::steady_clock::time_point::max()
                    : start + std::chrono::seconds{worker_seconds};
    std::uint64_t seq = 0;
    std::uint32_t publish_seq = 0;
    auto next_sample = start;

    const app::InfrastructureStopReason reason = infra->run([&]() -> bool {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        if (now < next_sample) {
            std::this_thread::sleep_until(next_sample);
            return true;
        }
        next_sample += std::chrono::seconds{1};

        NEXIGEN_TRACE_NEW_ROOT("DataGenerator");
        auto span = (*tracer)->start_span("generate-sample", tracing::SpanKind::Producer);
        static_cast<void>(span.set_attribute("sequence", static_cast<std::int64_t>(seq)));

        suite2_apps::DataSample sample{};
        sample.sequence = seq;
        const std::size_t take =
            kMessage.size() < sizeof(sample.message) ? kMessage.size() : sizeof(sample.message) - 1;
        std::memcpy(sample.message, kMessage.data(), take);
        sample.message[take] = '\0';

        auto loaned = publisher->loan_uninit();
        if (loaned.has_value()) {
            auto mut = loaned->write_payload(sample);
            const tracing::TraceHeader header = tracing::make_trace_header(publish_seq++);
            mut.user_header_mut() = header;
            const auto sent = publisher->send(std::move(mut));
            if (!sent) {
                infra->logger().warning("publish failed: {}", com::to_string(sent.error()));
            } else {
                (*counter)->inc();
                (*gauge)->set(static_cast<std::int64_t>(seq));
                infra->logger().debug("published sample seq={} trace_id={:016x}", seq,
                                      header.trace_id.value());
            }
        } else {
            infra->logger().warning("loan_uninit failed: {}", com::to_string(loaned.error()));
        }

        static_cast<void>(span.set_status(tracing::SpanStatus::Ok));
        ++seq;
        return true;
    });

    const core::Status shutdown_status = infra->shutdown();
    if (!shutdown_status) {
        return fail_status("Infrastructure::shutdown", shutdown_status);
    }

    std::printf("ex_generator: done (samples=%llu, reason=%s)\n",
                static_cast<unsigned long long>(seq), std::string(app::to_string(reason)).c_str());
    return 0;
}
