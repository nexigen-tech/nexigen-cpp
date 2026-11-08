// Split from hub_test.cpp: nexigen::hub HTTP/SSE server.

#include "hub_test_support.hpp"

void test_http_start_errors() {
    Fixture fixture("http_errors");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::StreamManager streaming;
    hub::HttpServer server(discovery, streaming);

    NX_CHECK(!server.start("").has_value());
    NX_CHECK(!server.start("noport").has_value());
    NX_CHECK(!server.start(":80").has_value());
    NX_CHECK(!server.start("127.0.0.1:").has_value());
    NX_CHECK(!server.start("localhost:abc").has_value());
    NX_CHECK(!server.start("127.0.0.1:70000").has_value());
    NX_CHECK(!server.start("bad host:80").has_value());
    server.set_diagnostic_sink(nullptr, nullptr);
    NX_CHECK(!server.to_string().empty());

    hub::HttpServer localhost(discovery, streaming);
    NX_CHECK(localhost.start("localhost:0").has_value());
    NX_CHECK(localhost.port() != 0);

    const int occupied = ::socket(AF_INET, SOCK_STREAM, 0);
    NX_CHECK(occupied >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr));
    NX_CHECK(::bind(occupied, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    NX_CHECK(::listen(occupied, 1) == 0);
    sockaddr_in bound{};
    socklen_t bound_length = sizeof(bound);
    NX_CHECK(::getsockname(occupied, reinterpret_cast<sockaddr*>(&bound), &bound_length) == 0);
    hub::HttpServer busy(discovery, streaming);
    NX_CHECK(!busy.start("127.0.0.1:" + std::to_string(ntohs(bound.sin_port))).has_value());
    static_cast<void>(::close(occupied));

    // A failed initial stream reconciliation is reported but does not prevent
    // the listener from starting.
    std::error_code remove_error;
    std::filesystem::remove(fixture.attempt_dir + "/traces.jsonl", remove_error);
    DiagnosticCapture capture;
    hub::StreamManager late;
    hub::HttpServer with_failure(discovery, late, capture_diagnostic, &capture);
    NX_CHECK(with_failure.start("127.0.0.1:0").has_value());
    NX_CHECK(diagnostic_contains(capture, "event=hub_sync_failed"));
}

// --- IPv6 listener (change fix-p1-races-process-correctness, spec
// hub/http-api "HTTP listener is loopback by default"; analysis #13): the
// bracket form binds AF_INET6 (V6ONLY=1 for a specific address, dual-stack
// for `::`), a request over `::1` round-trips, and malformed forms fail like
// their IPv4 counterparts. Gracefully skipped on hosts without IPv6
// loopback support.
void test_http_server_ipv6_listen() {
    Fixture fixture("http_v6");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::StreamManager streaming;
    hub::HttpServer probe_only(discovery, streaming);

    const int probe = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (probe < 0) {
        std::printf("http_v6: AF_INET6 unavailable in this environment; "
                    "skipped\n");
        remove_tree(fixture.root);
        return;
    }
    static_cast<void>(::close(probe));

    // Malformed IPv6 forms are rejects (multi-colon literals require
    // brackets; a missing port fails).
    NX_CHECK(!probe_only.start("[::1]").has_value());
    NX_CHECK(!probe_only.start("[::1]8080").has_value());
    NX_CHECK(!probe_only.start("[nope]:8080").has_value());
    NX_CHECK(!probe_only.start("::").has_value());
    NX_CHECK(!probe_only.start("::1:8080:9").has_value());

    write_text(fixture.attempt_dir + "/app.jsonl", "hello-v6\n");
    NX_CHECK(streaming.sync(discovery.tree()).has_value());
    hub::HttpServer server(discovery, streaming);
    NX_CHECK(server.start("[::1]:0").has_value());
    NX_CHECK(server.port() != 0);
    std::atomic<bool> stop{false};
    std::thread thread([&] { server.run(&stop); });

    const int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
    NX_CHECK(fd >= 0);
    sockaddr_in6 target{};
    target.sin6_family = AF_INET6;
    target.sin6_port = htons(server.port());
    static_cast<void>(::inet_pton(AF_INET6, "::1", &target.sin6_addr));
    NX_CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0);
    const std::string request = "GET / HTTP/1.1\r\nHost: [::1]\r\nConnection: close\r\n\r\n";
    static_cast<void>(::send(fd, request.data(), request.size(), 0));
    std::string text;
    char buffer[2048];
    NX_CHECK(wait_until(
        [&] {
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count > 0) {
                text.append(buffer, static_cast<std::size_t>(count));
            }
            return contains(text, "HTTP/1.1");
        },
        3000));
    static_cast<void>(::close(fd));

    stop.store(true);
    server.stop();
    thread.join();

    // Wildcard `[::]` binds dual-stack (IPV6_V6ONLY=0): an IPv4 loopback
    // connection at the same port meets the listening socket (queued by the
    // backlog, which is exactly what a dual bind promises).
    hub::HttpServer dual(discovery, streaming);
    NX_CHECK(dual.start("[::]:0").has_value());
    NX_CHECK(dual.port() != 0);
    const int v4fd = ::socket(AF_INET, SOCK_STREAM, 0);
    NX_CHECK(v4fd >= 0);
    sockaddr_in v4target{};
    v4target.sin_family = AF_INET;
    v4target.sin_port = htons(dual.port());
    static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &v4target.sin_addr));
    NX_CHECK(::connect(v4fd, reinterpret_cast<sockaddr*>(&v4target), sizeof(v4target)) == 0);
    static_cast<void>(::close(v4fd));
    dual.stop();
}

void test_http_sse_gaps() {
    Fixture fixture("http_gaps");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::StreamLimits limits{};
    limits.ring_records = 1;
    limits.max_line_bytes = 64;
    limits.subscriber_bytes = 4096;
    hub::StreamManager streaming(limits);
    NX_CHECK(streaming.sync(discovery.tree()).has_value());
    streaming.poll();
    write_text(fixture.attempt_dir + "/app.jsonl", "one\ntwo\nthree\n");
    streaming.poll();

    hub::HttpServer server(discovery, streaming);
    NX_CHECK(server.start("127.0.0.1:0").has_value());
    std::atomic<bool> stop{false};
    std::thread thread([&] { server.run(&stop); });
    const std::uint16_t port = server.port();

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    NX_CHECK(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr));
    NX_CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    timeval timeout{};
    timeout.tv_sec = 2;
    static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
    const std::string request = "GET /api/events?stream=" + stream_id(fixture, "app") +
                                "&since=0 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    static_cast<void>(::send(fd, request.data(), request.size(), 0));
    std::string text;
    char buffer[4096];
    NX_CHECK(wait_until(
        [&] {
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count > 0) {
                text.append(buffer, static_cast<std::size_t>(count));
            }
            return contains(text, "event: snapshot_end");
        },
        3000));
    NX_CHECK(contains(text, "event: gap"));
    NX_CHECK(contains(text, "\"sequence\":"));

    // A long line produces a live gap after the snapshot boundary.
    write_text(fixture.attempt_dir + "/app.jsonl", std::string(200, 'x') + "\n");
    std::string live;
    NX_CHECK(wait_until(
        [&] {
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count > 0) {
                live.append(buffer, static_cast<std::size_t>(count));
            }
            return contains(live, "event: gap");
        },
        3000));
    static_cast<void>(::close(fd));

    stop.store(true);
    server.stop();
    thread.join();
}

void test_http_fuzz_corpus() {
    Fixture fixture("http_fuzz");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::StreamManager streaming;
    NX_CHECK(streaming.sync(discovery.tree()).has_value());
    streaming.poll();
    hub::HttpServer server(discovery, streaming);
    NX_CHECK(server.start("127.0.0.1:0").has_value());
    std::atomic<bool> stop{false};
    std::thread thread([&] { server.run(&stop); });
    const std::uint16_t port = server.port();
    NX_CHECK(port != 0);

    auto exchange = [&](std::string_view payload) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        NX_CHECK(fd >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr));
        NX_CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        timeval timeout{};
        timeout.tv_usec = 200000;
        static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
        static_cast<void>(::send(fd, payload.data(), payload.size(), 0));
        static_cast<void>(::shutdown(fd, SHUT_WR));
        std::string response;
        char buffer[4096];
        for (;;) {
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count <= 0) break;
            response.append(buffer, static_cast<std::size_t>(count));
        }
        static_cast<void>(::close(fd));
        return response;
    };

    // Fixed-seed bytes exercise request-line, header, URL, and body edges.
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    auto next = [&]() -> std::uint32_t {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 33);
    };
    static constexpr char alphabet[] =
        "GET /api/health?stream=abc&since=1&limit=2 HTTP/1.1\r\n"
        "Host: localhost\r\nConnection: close\r\nLast-Event-ID: a:1\r\n"
        ":%?%\0\x01\x7f";
    constexpr std::size_t alphabet_size = sizeof(alphabet) - 1;
    for (int case_index = 0; case_index < 120; ++case_index) {
        const std::size_t length = next() % 512U;
        std::string payload;
        payload.reserve(length + 32U);
        if (case_index % 3 == 0) {
            payload += "GET /api/health HTTP/1.1\r\nHost: localhost\r\n";
        }
        for (std::size_t index = 0; index < length; ++index) {
            payload.push_back(alphabet[next() % alphabet_size]);
        }
        payload += "\r\n\r\n";
        const std::string response = exchange(payload);
        NX_CHECK(response.empty() || contains(response, "HTTP/1.1"));
    }

    const std::string health = exchange("GET /api/health HTTP/1.1\r\nHost: localhost\r\n"
                                        "Connection: close\r\n\r\n");
    NX_CHECK(contains(health, "200"));
    NX_CHECK(contains(health, "\"status\":\"ok\""));

    stop.store(true);
    server.stop();
    thread.join();
}

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
void test_http_fault_injection() {
    Fixture fixture("http_faults");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::StreamManager streaming;

    core::fault_injection::set_fault_hook(hub_fault_hook);
    for (const auto point :
         {core::fault_injection::FaultPoint::Hub_socket,
          core::fault_injection::FaultPoint::Hub_bind, core::fault_injection::FaultPoint::Hub_epoll,
          core::fault_injection::FaultPoint::Hub_eventfd}) {
        g_hub_fault_point = point;
        g_hub_fault_enabled = true;
        hub::HttpServer server(discovery, streaming);
        NX_CHECK(!server.start("127.0.0.1:0").has_value());
        g_hub_fault_enabled = false;
    }
    core::fault_injection::set_fault_hook(nullptr);
}
#endif

std::string test_http_server_request(std::uint16_t port, std::string_view method,
                                     std::string_view path, std::string_view extra_headers);
std::string test_http_server_raw(std::uint16_t port, std::string_view payload);
std::string test_http_server_sse_with_header(const Fixture& fixture, std::uint16_t port,
                                             std::string_view header);
template <class Request>
void test_http_server_routes(const Fixture& fixture, const Request& request);
template <class Raw> void test_http_server_malformed(const Raw& raw);
template <class Request> void test_http_server_methods(const Request& request);
template <class Request, class Raw>
void test_http_server_query_escape(const Fixture& fixture, const Request& request, const Raw& raw);
void test_http_server_keep_alive(std::uint16_t port);
void test_http_server_sse_live(const Fixture& fixture, std::uint16_t port);
void test_http_server_tree_topic(const Fixture& fixture, std::uint16_t port);
void test_http_server_all_stream_sse(std::uint16_t port);
void test_http_server_cursor_resume(const Fixture& fixture, std::uint16_t port);
template <class Raw, class Sse>
void test_http_server_header_case(const Fixture& fixture, const Raw& raw,
                                  const Sse& sse_with_header);
template <class Request>
void test_http_server_failure_report(const Fixture& fixture, DiagnosticCapture& capture,
                                     const Request& request);
void test_http_server_shutdown(std::atomic<bool>& stop, hub::HttpServer& server,
                               std::thread& thread);

void test_http_server() {
    Fixture fixture("http");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::StreamManager streaming;
    NX_CHECK(streaming.sync(discovery.tree()).has_value());
    streaming.poll();
    DiagnosticCapture capture;
    hub::HttpServer server(discovery, streaming, capture_diagnostic, &capture);
    NX_CHECK(server.start("127.0.0.1:0").has_value());
    std::atomic<bool> stop{false};
    std::thread thread([&] { server.run(&stop); });
    const std::uint16_t port = server.port();
    NX_CHECK(port != 0);

    auto request = [&](std::string_view method, std::string_view path,
                       std::string_view extra_headers = {}) {
        return test_http_server_request(port, method, path, extra_headers);
    };

    auto raw = [&](std::string_view payload) { return test_http_server_raw(port, payload); };

    test_http_server_routes(fixture, request);

    test_http_server_malformed(raw);

    test_http_server_methods(request);

    test_http_server_query_escape(fixture, request, raw);

    test_http_server_keep_alive(port);

    test_http_server_sse_live(fixture, port);

    test_http_server_tree_topic(fixture, port);

    test_http_server_all_stream_sse(port);

    test_http_server_cursor_resume(fixture, port);

    // Header names are case-insensitive, values tolerate whitespace, and a
    // similarly named header is not treated as the cursor.
    auto sse_with_header = [&](std::string_view header) {
        return test_http_server_sse_with_header(fixture, port, header);
    };

    test_http_server_header_case(fixture, raw, sse_with_header);

    test_http_server_failure_report(fixture, capture, request);

    test_http_server_shutdown(stop, server, thread);
}

std::string test_http_server_request(std::uint16_t port, std::string_view method,
                                     std::string_view path, std::string_view extra_headers) {
    {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        NX_CHECK(fd >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr));
        NX_CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        const std::string payload = std::string(method) + " " + std::string(path) +
                                    " HTTP/1.1\r\nHost: localhost\r\n" +
                                    std::string(extra_headers) + "Connection: close\r\n\r\n";
        static_cast<void>(::send(fd, payload.data(), payload.size(), 0));
        std::string response;
        char buffer[4096];
        for (;;) {
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count <= 0) break;
            response.append(buffer, static_cast<std::size_t>(count));
        }
        static_cast<void>(::close(fd));
        return response;
    }
}

std::string test_http_server_raw(std::uint16_t port, std::string_view payload) {
    {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        NX_CHECK(fd >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr));
        NX_CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        timeval timeout{};
        timeout.tv_sec = 2;
        static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
        static_cast<void>(::send(fd, payload.data(), payload.size(), 0));
        std::string response;
        char buffer[4096];
        for (;;) {
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count <= 0) break;
            response.append(buffer, static_cast<std::size_t>(count));
        }
        static_cast<void>(::close(fd));
        return response;
    }
}

template <class Request>
void test_http_server_routes(const Fixture& fixture, const Request& request) {
    NX_CHECK(contains(request("GET", "/api/health"), "\"status\":\"ok\""));
    NX_CHECK(contains(request("GET", "/api/tree"), "\"suite_id\":\"hil\""));
    NX_CHECK(contains(request("GET", "/api/streams"), "run-1:hil:alpha:1:1:app"));
    NX_CHECK(
        contains(request("GET", "/api/streams/" + stream_id(fixture, "app") + "/recent?limit=1"),
                 "\"records\""));
    NX_CHECK(contains(request("GET", "/"), "<!doctype html>"));
    NX_CHECK(contains(request("GET", "/app.js"), "EventSource"));
    NX_CHECK(contains(request("GET", "/style.css"), "--accent"));
    NX_CHECK(contains(request("GET", "/api/missing"), "404"));
    NX_CHECK(contains(request("GET", "/" + std::string(5000, 'a')), "400"));
    NX_CHECK(contains(request("GET", "/api/health", std::string(9000, 'x')), "400"));
}

template <class Raw> void test_http_server_malformed(const Raw& raw) {
    // Malformed request lines and unsupported versions get a bounded 400.
    NX_CHECK(contains(raw("GET\r\n\r\n"), "400"));
    NX_CHECK(contains(raw("GET /api/health HTTP/2.0\r\n\r\n"), "400"));
    NX_CHECK(contains(raw("GET /api/health\r\n\r\n"), "400"));
    NX_CHECK(contains(raw(std::string(13000, 'x')), "400"));
}

template <class Request> void test_http_server_methods(const Request& request) {
    // Every non-GET method is rejected without reaching a route.
    for (const std::string_view method : {"POST", "PUT", "DELETE", "PATCH"}) {
        NX_CHECK(contains(request(method, "/api/tree"), "405"));
    }
}

template <class Request, class Raw>
void test_http_server_query_escape(const Fixture& fixture, const Request& request, const Raw& raw) {
    // Invalid query and escape values fall back to defaults or a 404.
    NX_CHECK(contains(
        request("GET", "/api/streams/" + stream_id(fixture, "app") + "/recent?limit=abc"), "200"));
    NX_CHECK(contains(
        request("GET", "/api/streams/" + stream_id(fixture, "app") + "/recent?limit=999999"),
        "200"));
    NX_CHECK(contains(request("GET", "/api/streams/%zz/recent"), "404"));
    NX_CHECK(contains(request("GET", "/api/streams/unknown/recent"), "404"));
    // Percent-decoding is exercised by encoding every separator manually.
    std::string encoded_path;
    for (const char c : stream_id(fixture, "app")) {
        if (c == ':') {
            encoded_path += "%3A";
        } else {
            encoded_path.push_back(c);
        }
    }
    NX_CHECK(contains(request("GET", "/api/streams/" + encoded_path + "/recent"), "200"));
    NX_CHECK(contains(raw("GET /api/events?stream=unknown HTTP/1.1\r\n"
                          "Host: localhost\r\n\r\n"),
                      "404"));
    NX_CHECK(contains(raw("GET /api/events?stream=" + stream_id(fixture, "app") +
                          "&since=not-a-number HTTP/1.1\r\n"
                          "Host: localhost\r\n\r\n"),
                      "event: snapshot_end"));
}

void test_http_server_keep_alive(std::uint16_t port) {
    // HTTP/1.1 keep-alive serves two requests on one connection.
    {
        const int keep_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in keep_address{};
        keep_address.sin_family = AF_INET;
        keep_address.sin_port = htons(port);
        static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &keep_address.sin_addr));
        NX_CHECK(::connect(keep_fd, reinterpret_cast<sockaddr*>(&keep_address),
                           sizeof(keep_address)) == 0);
        timeval keep_timeout{};
        keep_timeout.tv_sec = 2;
        static_cast<void>(
            ::setsockopt(keep_fd, SOL_SOCKET, SO_RCVTIMEO, &keep_timeout, sizeof(keep_timeout)));
        const std::string first_request = "GET /api/health HTTP/1.1\r\nHost: localhost\r\n"
                                          "Connection: keep-alive\r\n\r\n";
        static_cast<void>(::send(keep_fd, first_request.data(), first_request.size(), 0));
        std::string buffer;
        auto read_one = [&](std::string& response) -> bool {
            for (;;) {
                const std::size_t header_end = buffer.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    const std::string header = buffer.substr(0, header_end + 4);
                    const std::size_t length_pos = header.find("Content-Length: ");
                    if (length_pos != std::string::npos) {
                        std::size_t length = 0;
                        std::size_t index = length_pos + 16;
                        while (index < header.size() && header[index] >= '0' &&
                               header[index] <= '9') {
                            length = length * 10U + static_cast<std::size_t>(header[index] - '0');
                            ++index;
                        }
                        if (buffer.size() >= header_end + 4 + length) {
                            response = buffer.substr(0, header_end + 4 + length);
                            buffer.erase(0, header_end + 4 + length);
                            return true;
                        }
                    }
                }
                char chunk[4096];
                const ssize_t count = ::recv(keep_fd, chunk, sizeof(chunk), 0);
                if (count <= 0) return false;
                buffer.append(chunk, static_cast<std::size_t>(count));
            }
        };
        std::string first_response;
        NX_CHECK(read_one(first_response));
        NX_CHECK(contains(first_response, "HTTP/1.1 200 OK"));
        const std::string second_request = "GET /api/health HTTP/1.1\r\nHost: localhost\r\n"
                                           "Connection: close\r\n\r\n";
        static_cast<void>(::send(keep_fd, second_request.data(), second_request.size(), 0));
        std::string second_response;
        NX_CHECK(read_one(second_response));
        NX_CHECK(contains(second_response, "HTTP/1.1 200 OK"));
        static_cast<void>(::close(keep_fd));
    }
}

void test_http_server_sse_live(const Fixture& fixture, std::uint16_t port) {
    // SSE snapshot headers and boundary over a raw socket.
    const int sse_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr));
    NX_CHECK(::connect(sse_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    timeval timeout{};
    timeout.tv_usec = 100000;
    static_cast<void>(::setsockopt(sse_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
    const std::string sse_request = "GET /api/events?stream=" + stream_id(fixture, "app") +
                                    "&since=0 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    static_cast<void>(::send(sse_fd, sse_request.data(), sse_request.size(), 0));
    std::string sse;
    char sse_buffer[4096];
    NX_CHECK(wait_until(
        [&] {
            const ssize_t count = ::recv(sse_fd, sse_buffer, sizeof(sse_buffer), 0);
            if (count > 0) sse.append(sse_buffer, static_cast<std::size_t>(count));
            return contains(sse, "event: snapshot_end");
        },
        3000));
    NX_CHECK(contains(sse, "Content-Type: text/event-stream"));
    NX_CHECK(contains(sse, "Transfer-Encoding: chunked"));
    NX_CHECK(contains(sse, "event: snapshot_begin"));
    NX_CHECK(contains(sse, "event: record"));

    // A new line is delivered live after the snapshot boundary.
    write_text(fixture.attempt_dir + "/app.jsonl", "{\"a\":1}\n{\"a\":2}\n{\"a\":3}\n");
    NX_CHECK(wait_until(
        [&] {
            const ssize_t count = ::recv(sse_fd, sse_buffer, sizeof(sse_buffer), 0);
            if (count > 0) sse.append(sse_buffer, static_cast<std::size_t>(count));
            return contains(sse, "\\\"a\\\":3");
        },
        3000));
    static_cast<void>(::close(sse_fd));
}

void test_http_server_tree_topic(const Fixture& fixture, std::uint16_t port) {
    // Tree topic emits the current snapshot.
    {
        const int tree_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in tree_address{};
        tree_address.sin_family = AF_INET;
        tree_address.sin_port = htons(port);
        static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &tree_address.sin_addr));
        NX_CHECK(::connect(tree_fd, reinterpret_cast<sockaddr*>(&tree_address),
                           sizeof(tree_address)) == 0);
        timeval tree_timeout{};
        tree_timeout.tv_sec = 2;
        static_cast<void>(
            ::setsockopt(tree_fd, SOL_SOCKET, SO_RCVTIMEO, &tree_timeout, sizeof(tree_timeout)));
        const std::string tree_request =
            "GET /api/events?tree=1 HTTP/1.1\r\nHost: localhost\r\n\r\n";
        static_cast<void>(::send(tree_fd, tree_request.data(), tree_request.size(), 0));
        std::string tree_text;
        char tree_buffer[4096];
        NX_CHECK(wait_until(
            [&] {
                const ssize_t count = ::recv(tree_fd, tree_buffer, sizeof(tree_buffer), 0);
                if (count > 0) {
                    tree_text.append(tree_buffer, static_cast<std::size_t>(count));
                }
                return contains(tree_text, "event: tree");
            },
            3000));
        NX_CHECK(contains(tree_text, "\"suite_id\":\"hil\""));
        // A tree change is pushed to the live tree topic.
        make_dirs(fixture.artifacts + "/hil/alpha/1/attempt-0009");
        NX_CHECK(wait_until(
            [&] {
                const ssize_t count = ::recv(tree_fd, tree_buffer, sizeof(tree_buffer), 0);
                if (count > 0) {
                    tree_text.append(tree_buffer, static_cast<std::size_t>(count));
                }
                const std::size_t first = tree_text.find("event: tree");
                return first != std::string::npos &&
                       tree_text.find("event: tree", first + 1) != std::string::npos;
            },
            5000));
        static_cast<void>(::close(tree_fd));
    }
}

void test_http_server_all_stream_sse(std::uint16_t port) {
    // All-stream SSE includes records from discovered streams.
    {
        const int all_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in all_address{};
        all_address.sin_family = AF_INET;
        all_address.sin_port = htons(port);
        static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &all_address.sin_addr));
        NX_CHECK(
            ::connect(all_fd, reinterpret_cast<sockaddr*>(&all_address), sizeof(all_address)) == 0);
        timeval all_timeout{};
        all_timeout.tv_sec = 2;
        static_cast<void>(
            ::setsockopt(all_fd, SOL_SOCKET, SO_RCVTIMEO, &all_timeout, sizeof(all_timeout)));
        const std::string all_request = "GET /api/events HTTP/1.1\r\nHost: localhost\r\n\r\n";
        static_cast<void>(::send(all_fd, all_request.data(), all_request.size(), 0));
        std::string all_text;
        char all_buffer[4096];
        NX_CHECK(wait_until(
            [&] {
                const ssize_t count = ::recv(all_fd, all_buffer, sizeof(all_buffer), 0);
                if (count > 0) {
                    all_text.append(all_buffer, static_cast<std::size_t>(count));
                }
                return contains(all_text, "event: snapshot_end");
            },
            3000));
        NX_CHECK(contains(all_text, "event: record"));
        static_cast<void>(::close(all_fd));
    }
}

void test_http_server_cursor_resume(const Fixture& fixture, std::uint16_t port) {
    // Last-Event-ID resumes a stream cursor.
    {
        const int cursor_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in cursor_address{};
        cursor_address.sin_family = AF_INET;
        cursor_address.sin_port = htons(port);
        static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &cursor_address.sin_addr));
        NX_CHECK(::connect(cursor_fd, reinterpret_cast<sockaddr*>(&cursor_address),
                           sizeof(cursor_address)) == 0);
        timeval cursor_timeout{};
        cursor_timeout.tv_sec = 2;
        static_cast<void>(::setsockopt(cursor_fd, SOL_SOCKET, SO_RCVTIMEO, &cursor_timeout,
                                       sizeof(cursor_timeout)));
        const std::string cursor_request =
            "GET /api/events?stream=" + stream_id(fixture, "app") +
            " HTTP/1.1\r\nHost: localhost\r\nLast-Event-ID: " + stream_id(fixture, "app") +
            ":1\r\n\r\n";
        static_cast<void>(::send(cursor_fd, cursor_request.data(), cursor_request.size(), 0));
        std::string cursor_text;
        char cursor_buffer[4096];
        NX_CHECK(wait_until(
            [&] {
                const ssize_t count = ::recv(cursor_fd, cursor_buffer, sizeof(cursor_buffer), 0);
                if (count > 0) {
                    cursor_text.append(cursor_buffer, static_cast<std::size_t>(count));
                }
                return contains(cursor_text, "event: snapshot_end");
            },
            3000));
        NX_CHECK(!contains(cursor_text, "\"sequence\":1"));
        NX_CHECK(contains(cursor_text, "\"sequence\":2"));
        static_cast<void>(::close(cursor_fd));
    }
}

std::string test_http_server_sse_with_header(const Fixture& fixture, std::uint16_t port,
                                             std::string_view header) {
    {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        NX_CHECK(fd >= 0);
        sockaddr_in sse_address{};
        sse_address.sin_family = AF_INET;
        sse_address.sin_port = htons(port);
        static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &sse_address.sin_addr));
        NX_CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&sse_address), sizeof(sse_address)) ==
                 0);
        timeval sse_timeout{};
        sse_timeout.tv_sec = 2;
        static_cast<void>(
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &sse_timeout, sizeof(sse_timeout)));
        const std::string payload = "GET /api/events?stream=" + stream_id(fixture, "app") +
                                    " HTTP/1.1\r\nHost: localhost\r\n" + std::string(header) +
                                    "\r\n";
        static_cast<void>(::send(fd, payload.data(), payload.size(), 0));
        std::string response;
        char buffer[4096];
        NX_CHECK(wait_until(
            [&] {
                const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
                if (count > 0) {
                    response.append(buffer, static_cast<std::size_t>(count));
                }
                return contains(response, "event: snapshot_end");
            },
            3000));
        static_cast<void>(::close(fd));
        return response;
    }
}

template <class Raw, class Sse>
void test_http_server_header_case(const Fixture& fixture, const Raw& raw,
                                  const Sse& sse_with_header) {
    const std::vector<std::string> cursor_headers{
        "last-event-id: " + stream_id(fixture, "app") + ":2\r\n",
        "LAST-EVENT-ID: " + stream_id(fixture, "app") + ":2\r\n",
        "Last-Event-ID:    " + stream_id(fixture, "app") + ":2   \r\n"};
    for (const std::string& header : cursor_headers) {
        const std::string resumed = sse_with_header(header);
        NX_CHECK(!contains(resumed, "\"sequence\":1"));
        NX_CHECK(contains(resumed, "\"sequence\":3"));
    }
    const std::string invalid_cursor = sse_with_header("Last-Event-ID: not-a-sequence\r\n");
    NX_CHECK(contains(invalid_cursor, "\"sequence\":1"));
    const std::string similar_header =
        sse_with_header("X-Last-Event-ID: " + stream_id(fixture, "app") + ":2\r\n");
    NX_CHECK(contains(similar_header, "\"sequence\":1"));

    // Connection header matching is case-insensitive as well.
    const std::string connection_close = raw("GET /api/health HTTP/1.1\r\nHost: localhost\r\n"
                                             "Connection: CLOSE\r\n\r\n");
    NX_CHECK(contains(connection_close, "200"));
}

template <class Request>
void test_http_server_failure_report(const Fixture& fixture, DiagnosticCapture& capture,
                                     const Request& request) {
    // A disappearing run root is reported as a failed refresh while the
    // listener keeps serving.
    remove_tree(fixture.root);
    NX_CHECK(wait_until(
        [&] { return diagnostic_contains(capture, "event=discovery_rescan_failed"); }, 4000));
    NX_CHECK(contains(request("GET", "/api/health"), "200"));
}

void test_http_server_shutdown(std::atomic<bool>& stop, hub::HttpServer& server,
                               std::thread& thread) {
    stop.store(true);
    server.stop();
    thread.join();
}

void run_hub_http_server_suites() {
    run_suite("http_start_errors", test_http_start_errors);
    run_suite("http_v6", test_http_server_ipv6_listen);
    run_suite("http_sse_gaps", test_http_sse_gaps);
    run_suite("http_fuzz_corpus", test_http_fuzz_corpus);
    run_suite("http_server", test_http_server);
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    run_suite("http_fault_injection", test_http_fault_injection);
#endif
}
