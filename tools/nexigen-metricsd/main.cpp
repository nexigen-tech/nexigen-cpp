// nexigen-metricsd — центральный демон сбора метрик (change add-metrics-daemon;
// spec metrics/metrics-daemon).
//
// Подписывается на сервис iceoryx2, куда Iceoryx2MetricSink публикует кадры
// метрик, агрегирует последнее значение по (app, meter, metric), отдаёт
// Prometheus-текст на HTTP и ведёт квотируемую JSONL-историю на диске.
//
// Использование: nexigen-metricsd --config <path-to-metrics-daemon-config.json>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <csignal>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/utc_offset.hpp>
#include <nexigen/com/callback_progression.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/waitset.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/metrics/detail/exposition_buffer.hpp>
#include <nexigen/metrics/detail/history_writer.hpp>
#include <nexigen/metrics/detail/metric_aggregator.hpp>
#include <nexigen/metrics/detail/metric_http_server.hpp>
#include <nexigen/metrics/metric_daemon_config.hpp>
#include <nexigen/metrics/metric_frame.hpp>

namespace com = nexigen::com;
namespace metrics = nexigen::metrics;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;

namespace {

std::atomic<bool> g_stop{false};
void signal_handler(int) {
    g_stop.store(true, std::memory_order_relaxed);
}

/// Load MetricDaemonConfig from a JSON file.
core::Status load_config(const char* path, metrics::MetricDaemonConfig& cfg) noexcept {
    std::ifstream file(path);
    if (!file) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    metrics::from_json(j, cfg);
    return {};
}

bool ensure_dir(const std::string& dir) noexcept {
    if (dir.empty()) {
        return false;
    }
    if (::mkdir(dir.c_str(), 0755) == 0) {
        return true;
    }
    return errno == EEXIST;
}

/// Parsed command line. `--help` reports an early exit code through the return
/// value; everything else fills the options.
struct Options {
    const char* config_path{nullptr};
};

[[nodiscard]] std::optional<int> parse_args(int argc, char* argv[], Options& options) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            options.config_path = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("nexigen-metricsd - Metrics Daemon\n");
            std::printf("Usage: nexigen-metricsd --config <path>\n");
            return 0;
        }
    }
    if (options.config_path == nullptr) {
        std::fprintf(stderr, "nexigen-metricsd: missing --config <path>\n");
        return 1;
    }
    return std::nullopt;
}

/// Frames collected since the last flush, plus the history line built from them.
struct Window {
    std::vector<metrics::MetricFrame> frames{};
    std::size_t count{0U};

    void flush(std::int64_t utc_offset_ns, metrics::detail::HistoryWriter& history) {
        if (count == 0U) {
            return;
        }
        std::string line = metrics::detail::build_history_line(frames.data(), count, utc_offset_ns);
        line.push_back('\n');
        static_cast<void>(history.append(line));
        count = 0U;
    }
};

/// HTTP scrape front end: the server, its stop flag and its serving thread.
struct HttpFront {
    metrics::detail::MetricHttpServer server{};
    std::atomic<bool> stop{false};
    std::jthread thread{};
};

bool start_http(HttpFront& front, const metrics::MetricDaemonConfig& cfg,
                const metrics::detail::ExpositionBuffer& exposition) {
    if (!cfg.enable_http) {
        return true;
    }
    if (!front.server.start(cfg.bind_address, cfg.port, cfg.endpoint)) {
        std::fprintf(stderr, "nexigen-metricsd: cannot bind %s:%u%s\n", cfg.bind_address.c_str(),
                     static_cast<unsigned>(cfg.port), cfg.endpoint.c_str());
        return false;
    }
    auto& server = front.server;
    auto& stop = front.stop;
    front.thread = std::jthread([&server, &stop, &exposition] { server.serve(stop, exposition); });
    return true;
}

void stop_http(HttpFront& front) {
    front.stop.store(true, std::memory_order_relaxed);
    if (front.thread.joinable()) {
        front.thread.join();
    }
    front.server.stop_listen();
}

/// Flush/render cadence: the configured interval, or 500 ms when unset.
std::chrono::milliseconds flush_interval_of(const metrics::MetricDaemonConfig& cfg) {
    return std::chrono::milliseconds(cfg.batch_flush_interval_ms > 0U ? cfg.batch_flush_interval_ms
                                                                      : 500U);
}

/// Drain every queued frame into the window, then render/flush on the interval.
void drain_frames(com::Subscriber<metrics::MetricFrame>& subscriber,
                  metrics::detail::MetricAggregator& aggregator, Window& window,
                  std::chrono::milliseconds flush_interval,
                  std::chrono::steady_clock::time_point& last_render,
                  metrics::detail::ExpositionBuffer& exposition,
                  metrics::detail::HistoryWriter& history, std::int64_t utc_offset_ns) {
    for (;;) {
        auto got = subscriber.receive();
        if (!got || !got->has_value()) {
            break;
        }
        const metrics::MetricFrame& frame = (*got)->payload();
        aggregator.update(frame);
        if (window.count == window.frames.size()) {
            window.flush(utc_offset_ns, history);
        }
        window.frames[window.count++] = frame;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_render >= flush_interval) {
        window.flush(utc_offset_ns, history);
        exposition.render(aggregator);
        history.flush();
        last_render = now;
    }
}

void print_banner(const metrics::MetricDaemonConfig& cfg, const HttpFront& http,
                  const std::string& history_path) {
    std::printf("nexigen-metricsd: subscribing to %s", cfg.service_name.c_str());
    if (cfg.enable_http) {
        std::printf(", serving %s:%u%s", cfg.bind_address.c_str(),
                    static_cast<unsigned>(http.server.port()), cfg.endpoint.c_str());
    }
    std::printf(", history %s\n", history_path.c_str());
}

/// Open the transport, history and HTTP front end, then run the drain loop.
int run_session(const metrics::MetricDaemonConfig& cfg) {
    // Best-effort affinity for the drain thread; an empty spec is a no-op.
    {
        core::ThreadApplyReport report{};
        static_cast<void>(core::apply_thread_spec_self(cfg.bg_cpu_affinity, report));
    }

    // UTC offset for the history line's utc_ns (best-effort; 0 on failure).
    std::int64_t utc_offset_ns = 0;
    static_cast<void>(chrono::sample_utc_offset(utc_offset_ns));

    if (!ensure_dir(cfg.storage_directory)) {
        std::fprintf(stderr, "nexigen-metricsd: cannot create storage directory %s\n",
                     cfg.storage_directory.c_str());
        return 1;
    }

    auto node = com::Node::create();
    if (!node) {
        std::fprintf(stderr, "nexigen-metricsd: failed to create iceoryx2 node\n");
        return 1;
    }

    const std::uint32_t capacity = cfg.max_metrics > 0U ? cfg.max_metrics : 256U;
    const std::uint64_t buffer_depth = capacity;

    const auto service = com::ServiceName::create(cfg.service_name.c_str());
    if (!service) {
        std::fprintf(stderr, "nexigen-metricsd: invalid service name '%s'\n",
                     cfg.service_name.c_str());
        return 1;
    }
    // The sink sizes the service maximum; the daemon requests the same depth so
    // a full publish burst is retained instead of the transport default of 2.
    auto subscriber =
        com::Subscriber<metrics::MetricFrame>::create(*node, *service, buffer_depth, buffer_depth);
    if (!subscriber) {
        std::fprintf(stderr, "nexigen-metricsd: failed to create subscriber\n");
        return 1;
    }

    const std::string event_service = cfg.service_name + "Event";
    const auto event_name = com::ServiceName::create(event_service.c_str());
    if (!event_name) {
        std::fprintf(stderr, "nexigen-metricsd: invalid event service name\n");
        return 1;
    }
    auto listener = com::EventListener::create(*node, *event_name);
    if (!listener) {
        std::fprintf(stderr, "nexigen-metricsd: failed to create event listener\n");
        return 1;
    }

    auto aggregator = std::make_unique<metrics::detail::MetricAggregator>(capacity);
    auto exposition = std::make_unique<metrics::detail::ExpositionBuffer>();

    const std::string history_path = cfg.storage_directory + "/metrics_history.jsonl";
    const std::uint64_t max_bytes = cfg.max_storage_mb * 1024ULL * 1024ULL;
    metrics::detail::HistoryWriter history{history_path, max_bytes};
    if (!history.open()) {
        std::fprintf(stderr, "nexigen-metricsd: cannot open history file %s\n",
                     history_path.c_str());
        return 1;
    }

    HttpFront http;
    if (!start_http(http, cfg, *exposition)) {
        return 1;
    }

    // Zero-polling drain: the WaitSet blocks in the kernel until a frame event
    // or the interval timer fires, so an idle daemon costs 0% service-core CPU.
    // Signal handling is ours (create_without_signal_handling) so g_stop drives
    // shutdown; the interval timer bounds the exit latency.
    auto ws = com::WaitSet::create_without_signal_handling();
    if (!ws) {
        std::fprintf(stderr, "nexigen-metricsd: failed to create WaitSet\n");
        return 1;
    }
    auto guard_ev = ws->attach_notification(*listener);
    if (!guard_ev) {
        std::fprintf(stderr, "nexigen-metricsd: failed to attach event listener\n");
        return 1;
    }
    const auto flush_interval = flush_interval_of(cfg);
    auto guard_tm = ws->attach_interval(flush_interval);
    if (!guard_tm) {
        std::fprintf(stderr, "nexigen-metricsd: failed to attach flush timer\n");
        return 1;
    }

    print_banner(cfg, http, history_path);

    // Pre-allocated window buffer: one line per flush interval.
    Window window;
    window.frames.resize(capacity);
    auto last_render = std::chrono::steady_clock::now();

    while (!g_stop.load(std::memory_order_relaxed)) {
        const auto status = ws->wait_and_process([&](com::WaitSetAttachmentId id) {
            static_cast<void>(id);
            drain_frames(*subscriber, *aggregator, window, flush_interval, last_render, *exposition,
                         history, utc_offset_ns);
            return com::CallbackProgression::Continue;
        });
        if (!status) {
            break;
        }
    }

    window.flush(utc_offset_ns, history);
    history.flush();
    history.close();
    stop_http(http);
    std::printf("nexigen-metricsd: stopped\n");
    return 0;
}

int run_daemon(const Options& options) {
    metrics::MetricDaemonConfig cfg;
    if (!load_config(options.config_path, cfg)) {
        std::fprintf(stderr, "nexigen-metricsd: failed to load config from %s\n",
                     options.config_path);
        return 1;
    }
    if (!cfg.validate()) {
        std::fprintf(stderr, "nexigen-metricsd: invalid config\n");
        return 1;
    }

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    return run_session(cfg);
}

} // namespace

int main(int argc, char* argv[]) {
    Options options;
    const std::optional<int> early_exit = parse_args(argc, argv, options);
    if (early_exit.has_value()) {
        return *early_exit;
    }
    return run_daemon(options);
}
