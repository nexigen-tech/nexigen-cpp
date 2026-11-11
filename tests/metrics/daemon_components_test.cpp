// nexigen::metrics — MetricsDaemon detail components: aggregation, exposition,
// HTTP and history (change add-metrics-daemon; spec metrics/metrics-daemon).
// No com dependency, so it runs in the standard metrics suite.

#include "metrics_test_support.hpp"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nexigen/metrics/detail/exposition_buffer.hpp>
#include <nexigen/metrics/detail/history_writer.hpp>
#include <nexigen/metrics/detail/metric_aggregator.hpp>
#include <nexigen/metrics/detail/metric_http_server.hpp>
#include <nexigen/metrics/metric_frame.hpp>

namespace {

metrics::MetricFrame make_counter(const char* app, const char* metric, std::uint64_t value) {
    metrics::MetricFrame frame{};
    std::memcpy(frame.app_id, app, 4);
    frame.pid = 42;
    std::snprintf(frame.record.identity.meter_name, sizeof(frame.record.identity.meter_name),
                  "meter");
    std::snprintf(frame.record.identity.meter_version, sizeof(frame.record.identity.meter_version),
                  "1.0");
    std::snprintf(frame.record.identity.metric_name, sizeof(frame.record.identity.metric_name), "%s",
                  metric);
    frame.record.kind = metrics::MetricKind::Counter;
    frame.record.scalar.counter = value;
    return frame;
}

void test_aggregation_latest_wins() {
    metrics::detail::MetricAggregator aggregator{8};
    aggregator.update(make_counter("LIDR", "a", 1));
    aggregator.update(make_counter("LIDR", "a", 5));
    NX_CHECK(aggregator.size() == 1U);

    std::uint64_t latest = 0;
    aggregator.for_each([&latest](const metrics::MetricFrame& frame) {
        latest = frame.record.scalar.counter;
    });
    NX_CHECK(latest == 5U);

    // A different metric name is a distinct key.
    aggregator.update(make_counter("LIDR", "b", 9));
    NX_CHECK(aggregator.size() == 2U);

    // A different app is a distinct key too.
    aggregator.update(make_counter("CAMR", "a", 3));
    NX_CHECK(aggregator.size() == 3U);
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

void test_exposition_and_http() {
    metrics::detail::MetricAggregator aggregator{8};
    aggregator.update(make_counter("LIDR", "requests_total", 7));

    auto exposition = std::make_unique<metrics::detail::ExpositionBuffer>();
    exposition->render(aggregator);
    const std::string_view body = exposition->current();
    NX_CHECK(body.find("# TYPE requests_total counter") != std::string_view::npos);
    NX_CHECK(body.find("requests_total{app=\"LIDR\"") != std::string_view::npos);

    metrics::detail::MetricHttpServer server;
    NX_CHECK(server.start("127.0.0.1", 0, "/metrics"));
    NX_CHECK(server.port() != 0U);
    std::atomic<bool> stop{false};
    std::jthread thread([&] { server.serve(stop, *exposition); });

    int status = -1;
    const std::string ok = http_request(server.port(), "/metrics", status);
    NX_CHECK(status == 200);
    NX_CHECK(ok.find("requests_total") != std::string::npos);
    NX_CHECK(ok.find("text/plain; version=0.0.4") != std::string::npos);

    const std::string missing = http_request(server.port(), "/nope", status);
    NX_CHECK(status == 404);

    stop.store(true, std::memory_order_relaxed);
    thread.join();
    server.stop_listen();
}

void test_history_append_and_rotation() {
    char path[] = "/tmp/nexigen_metrics_history_XXXXXX";
    const int fd = ::mkstemp(path);
    NX_CHECK(fd >= 0);
    if (fd < 0) {
        return;
    }
    ::close(fd);

    metrics::MetricFrame frame = make_counter("LIDR", "hist_metric", 11);
    const std::string line = metrics::detail::build_history_line(&frame, 1);
    NX_CHECK(line.find("hist_metric") != std::string::npos);
    NX_CHECK(line.find("\"record_count\":1") != std::string::npos);

    // A tiny quota forces rotation after the first line fills the file.
    metrics::detail::HistoryWriter writer{path, 64};
    NX_CHECK(writer.open());
    NX_CHECK(writer.append(line + "\n"));
    NX_CHECK(writer.append(line + "\n"));
    writer.flush();
    writer.close();

    const std::string rotated = std::string(path) + ".1";
    NX_CHECK(::access(rotated.c_str(), F_OK) == 0); // rotation happened
    ::unlink(path);
    ::unlink(rotated.c_str());
}

void test_daemon_components() {
    test_aggregation_latest_wins();
    test_exposition_and_http();
    test_history_append_and_rotation();
}

} // namespace

void suite_daemon_components() {
    run_suite("daemon_components", test_daemon_components);
}
