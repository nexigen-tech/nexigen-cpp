// End-to-end MetricsDaemon pipeline (change add-metrics-daemon; spec
// metrics/metrics-daemon): MetricProvider → Iceoryx2MetricSink → subscriber →
// aggregation → Prometheus HTTP → JSONL history. COM-gated.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/metrics/detail/exposition_buffer.hpp>
#include <nexigen/metrics/detail/history_writer.hpp>
#include <nexigen/metrics/detail/metric_aggregator.hpp>
#include <nexigen/metrics/detail/metric_http_server.hpp>
#include <nexigen/metrics/iceoryx2_metric_sink.hpp>
#include <nexigen/metrics/iceoryx2_metric_sink_config.hpp>
#include <nexigen/metrics/metric_frame.hpp>
#include <nexigen/metrics/metric_provider.hpp>

namespace metrics = nexigen::metrics;
namespace com = nexigen::com;

namespace {

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

void run_suite(const char* name, void (*body)()) {
    const int checks_before = g_checks;
    const int failures_before = g_failures;
    body();
    std::printf("[%-22s] %d checks, %d failed\n", name, g_checks - checks_before,
                g_failures - failures_before);
}

std::string http_request(std::uint16_t port, const char* path, int& status) {
    status = -1;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return {};
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return {};
    }
    char request[256];
    const int request_size =
        std::snprintf(request, sizeof(request),
                      "GET %s HTTP/1.1\r\nHost: test\r\nConnection: close\r\n\r\n", path);
    static_cast<void>(::send(fd, request, static_cast<std::size_t>(request_size), 0));
    std::string response;
    char buffer[4096];
    for (;;) {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(n));
    }
    ::close(fd);
    if (response.rfind("HTTP/1.1 ", 0) == 0) {
        status = std::atoi(response.c_str() + 9);
    }
    return response;
}

void test_daemon_e2e() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }

    metrics::Iceoryx2MetricSinkConfig sink_config;
    sink_config.app_id = "LIDR";
    sink_config.service_name = "Nexigen/Test/MetricsdE2E";
    sink_config.subscriber_max_buffer_size = 16;
    metrics::Iceoryx2MetricSink sink{sink_config, &*node};
    NX_CHECK(sink.start().has_value());

    const auto service = com::ServiceName::create(sink_config.service_name.c_str());
    NX_CHECK(service.has_value());
    auto subscriber = com::Subscriber<metrics::MetricFrame>::create(*node, *service, 16U, 16U);
    NX_CHECK(subscriber.has_value());
    if (!subscriber.has_value()) {
        return;
    }

    const std::string event_service = sink_config.service_name + "Event";
    const auto event_name = com::ServiceName::create(event_service.c_str());
    NX_CHECK(event_name.has_value());
    auto listener = com::EventListener::create(*node, *event_name);
    NX_CHECK(listener.has_value());

    metrics::MetricProviderConfig provider_config;
    provider_config.collect_interval = std::chrono::nanoseconds{0};
    metrics::MetricProvider provider{provider_config, sink};
    auto meter = provider.get_meter("e2e", "1.0");
    NX_CHECK(meter.has_value());
    auto counter = (*meter)->create_counter("e2e_total");
    NX_CHECK(counter.has_value());
    (*counter)->add(5U);

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // discovery
    NX_CHECK(provider.collect_now().has_value());

    // Daemon-side drain: frames → aggregation window.
    metrics::detail::MetricAggregator aggregator{16};
    std::vector<metrics::MetricFrame> window;
    for (int attempt = 0; attempt < 1000 && aggregator.size() == 0U; ++attempt) {
        bool drained = false;
        for (;;) {
            auto got = subscriber->receive();
            if (!got || !got->has_value()) {
                break;
            }
            const metrics::MetricFrame& frame = (*got)->payload();
            aggregator.update(frame);
            window.push_back(frame);
            drained = true;
        }
        if (!drained) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    NX_CHECK(aggregator.size() >= 1U);

    auto exposition = std::make_unique<metrics::detail::ExpositionBuffer>();
    exposition->render(aggregator);
    NX_CHECK(exposition->current().find("e2e_total") != std::string_view::npos);
    NX_CHECK(exposition->current().find("# TYPE e2e_total counter") != std::string_view::npos);

    // Scrape the Prometheus endpoint.
    metrics::detail::MetricHttpServer server;
    NX_CHECK(server.start("127.0.0.1", 0, "/metrics"));
    std::atomic<bool> http_stop{false};
    std::jthread http_thread([&] { server.serve(http_stop, *exposition); });

    int status = -1;
    const std::string ok = http_request(server.port(), "/metrics", status);
    NX_CHECK(status == 200);
    NX_CHECK(ok.find("e2e_total") != std::string::npos);
    static_cast<void>(http_request(server.port(), "/nope", status));
    NX_CHECK(status == 404);

    http_stop.store(true, std::memory_order_relaxed);
    http_thread.join();
    server.stop_listen();

    // JSONL history line for the window.
    char path[] = "/tmp/nexigen_metricsd_e2e_XXXXXX";
    const int fd = ::mkstemp(path);
    NX_CHECK(fd >= 0);
    if (fd >= 0) {
        ::close(fd);
        metrics::detail::HistoryWriter writer{path, 1U << 20};
        NX_CHECK(writer.open());
        if (!window.empty()) {
            std::string line = metrics::detail::build_history_line(window.data(), window.size());
            line.push_back('\n');
            NX_CHECK(writer.append(line));
        }
        writer.flush();
        writer.close();

        std::ifstream file(path);
        const std::string content((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
        NX_CHECK(content.find("e2e_total") != std::string::npos);
        NX_CHECK(content.find("record_count") != std::string::npos);
        ::unlink(path);
    }

    sink.stop();
}

} // namespace

int main() {
    run_suite("daemon_e2e", test_daemon_e2e);
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
