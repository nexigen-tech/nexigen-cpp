// nexigen::hub — minimal HTTP/1.1 + SSE server
// (change nexigen-hub; spec hub/http-api).
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/detail/fault_injection.hpp>
#include <nexigen/hub/detail/http_parse.hpp>
#include <nexigen/hub/discovery.hpp>
#include <nexigen/hub/http_server.hpp>
#include <nexigen/hub/gap_marker.hpp>
#include <nexigen/hub/hub_tree.hpp>
#include <nexigen/hub/stream_record.hpp>
#include <nexigen/hub/streaming.hpp>
#include <nexigen/hub/web_assets.hpp>
#include <nexigen/runner/detail/sys_helpers.hpp>

namespace nexigen::hub {

namespace {

constexpr std::size_t max_request_line_bytes = std::size_t{4} * 1024U;
constexpr std::size_t max_header_bytes = std::size_t{8} * 1024U;
constexpr std::size_t max_request_bytes = max_request_line_bytes + max_header_bytes + 4U;
constexpr std::size_t max_client_output_bytes = std::size_t{1} * 1024U * 1024U;
constexpr std::size_t max_clients = 64;
constexpr std::uint64_t heartbeat_interval_ns = 15ULL * 1'000'000'000ULL;
constexpr std::uint64_t refresh_interval_ns = 1ULL * 1'000'000'000ULL;

using nexigen::runner::detail::monotonic_now_ns;

[[nodiscard]] std::string json_text(const nlohmann::json& document) {
    return document.dump();
}

[[nodiscard]] bool starts_with(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] std::string sse_record(const StreamRecord& record) {
    nlohmann::json document;
    to_json(document, record);
    return "event: record\nid: " + record.stream_id + ":" + std::to_string(record.sequence) +
           "\ndata: " + json_text(document) + "\n\n";
}

[[nodiscard]] std::string sse_gap(const GapMarker& gap) {
    nlohmann::json document;
    to_json(document, gap);
    return "event: gap\ndata: " + json_text(document) + "\n\n";
}

// SSE helpers
[[nodiscard]] std::string sse_headers() noexcept {
    return "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
           "Cache-Control: no-cache\r\nTransfer-Encoding: chunked\r\n"
           "X-Accel-Buffering: no\r\n\r\n";
}

[[nodiscard]] std::string sse_tree_event(const nlohmann::json& document) noexcept {
    return "event: tree\ndata: " + json_text(document) + "\n\n";
}

} // namespace

struct HttpServer::Impl {
    struct Client {
        int fd{-1};
        std::string input{};
        std::string output{};
        bool close_after_write{false};
        bool sse{false};
        bool chunked{false};
        bool tree_topic{false};
        std::uint64_t subscriber{0};
        std::uint64_t last_tree_revision{0};
        std::uint64_t last_heartbeat_ns{0};
    };

    Discovery& discovery;
    StreamManager& streaming;
    int listen_fd{-1};
    int epoll_fd{-1};
    int event_fd{-1};
    std::uint16_t port{0};
    /// Bind target prepared before socket creation (IPv4 or IPv6-variant per
    /// the parsed family; analysis #13).
    union BindAddress {
        sockaddr_in in{};
        sockaddr_in6 in6;
    };
    BindAddress bind_address_{};
    std::unordered_map<int, Client> clients{};
    std::uint64_t last_refresh_ns{0};
    DiagnosticSink diagnostic_sink{nullptr};
    void* diagnostic_context{nullptr};

    void set_diagnostic_sink(DiagnosticSink sink, void* context) noexcept {
        diagnostic_sink = sink;
        diagnostic_context = context;
        streaming.set_diagnostic_sink(sink, context);
    }

    void report(std::string_view message) const noexcept {
        if (diagnostic_sink != nullptr) {
            diagnostic_sink(diagnostic_context, message);
        }
    }

    /// Append one SSE body chunk in HTTP/1.1 chunked-transfer framing
    /// (`<size hex>\r\n<data>\r\n`), so browsers can stream the event body.
    void append_sse(Client& client, std::string_view data) noexcept {
        if (data.empty()) {
            return;
        }
        char size_line[32];
        const int size_len = std::snprintf(size_line, sizeof(size_line), "%zx\r\n", data.size());
        client.output.append(size_line, static_cast<std::size_t>(size_len));
        client.output.append(data.data(), data.size());
        client.output.append("\r\n", 2);
    }

    Impl(Discovery& discovery_ref, StreamManager& streaming_ref, DiagnosticSink sink,
         void* context) noexcept
        : discovery(discovery_ref), streaming(streaming_ref) {
        set_diagnostic_sink(sink, context);
    }

    ~Impl() {
        for (auto& [fd, client] : clients) {
            if (client.subscriber != 0) {
                streaming.unsubscribe(client.subscriber);
            }
            static_cast<void>(::close(fd));
        }
        if (event_fd >= 0) {
            static_cast<void>(::close(event_fd));
        }
        if (listen_fd >= 0) {
            static_cast<void>(::close(listen_fd));
        }
        if (epoll_fd >= 0) {
            static_cast<void>(::close(epoll_fd));
        }
    }

    [[nodiscard]] bool parse_listen(std::string_view listen, std::string& host,
                                    std::uint16_t& out_port, bool& out_ipv6) const {
        out_ipv6 = false;
        if (listen.empty()) {
            return false;
        }
        // RFC 3986 bracket form: `[<addr>]:<port>` (required for full v6
        // literals with more than one colon; analysis #13).
        if (listen.front() == '[') {
            const std::size_t closing = listen.find(']');
            if (closing == std::string_view::npos || closing + 2 >= listen.size() ||
                listen[closing + 1] != ':') {
                return false;
            }
            host = std::string(listen.substr(1, closing - 1));
            out_ipv6 = true;
            return parse_port(listen, closing + 2, out_port);
        }
        // Bare form: a single colon separates host and port for IPv4 names,
        // and a ≤1-colon IPv6 literal is unambiguous (e.g. `::1:8080` style
        // `addr:port` with one colon in the address part — multi-colon
        // literals must use brackets). Keep the port digits-only rule.
        const std::size_t colon = listen.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            return false;
        }
        if (listen.find(':', colon + 1) != std::string_view::npos) {
            return false; // ambiguous multi-colon without brackets
        }
        host = std::string(listen.substr(0, colon));
        if (host == "localhost") {
            host = "127.0.0.1";
        }
        out_ipv6 = host.find(':') != std::string::npos;
        return parse_port(listen, colon + 1, out_port);
    }

    [[nodiscard]] bool parse_port(std::string_view listen, std::size_t begin,
                                  std::uint16_t& out_port) const {
        if (begin >= listen.size()) {
            return false;
        }
        std::uint64_t port_value = 0;
        for (std::size_t index = begin; index < listen.size(); ++index) {
            const char c = listen[index];
            if (c < '0' || c > '9') {
                return false;
            }
            port_value = port_value * std::uint64_t{10} + static_cast<std::uint64_t>(c - '0');
            if (port_value > 65535ULL) {
                return false;
            }
        }
        out_port = static_cast<std::uint16_t>(port_value);
        return true;
    }

    [[nodiscard]] core::Status bind_listen(const std::string& host, std::uint16_t requested_port,
                                           bool ipv6) noexcept {
        if (ipv6) {
            const bool dual_stack = host == "::";
            const std::uint32_t v6_only = dual_stack ? 0U : 1U;
            if (::inet_pton(AF_INET6, host.c_str(), &bind_address_.in6.sin6_addr) != 1) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            bind_address_.in6.sin6_family = AF_INET6;
            bind_address_.in6.sin6_port = htons(requested_port);

            if (core::fault_injection::fault(core::fault_injection::FaultPoint::Hub_socket)) {
                return std::unexpected(core::ErrorCode::SystemError);
            }
            listen_fd = ::socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (listen_fd < 0) {
                return std::unexpected(core::ErrorCode::SystemError);
            }

            int enabled = 1;
            static_cast<void>(
                ::setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6_only, sizeof(v6_only)));
            static_cast<void>(
                ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)));
            if (core::fault_injection::fault(core::fault_injection::FaultPoint::Hub_bind) ||
                ::bind(listen_fd, reinterpret_cast<sockaddr*>(&bind_address_.in6),
                       sizeof(bind_address_.in6)) != 0 ||
                ::listen(listen_fd, 128) != 0) {
                static_cast<void>(::close(listen_fd));
                listen_fd = -1;
                return std::unexpected(core::ErrorCode::SystemError);
            }
            return {};
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(requested_port);
        if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (core::fault_injection::fault(core::fault_injection::FaultPoint::Hub_socket)) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (listen_fd < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        int enabled = 1;
        static_cast<void>(
            ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)));
        if (core::fault_injection::fault(core::fault_injection::FaultPoint::Hub_bind) ||
            ::bind(listen_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            ::listen(listen_fd, 128) != 0) {
            static_cast<void>(::close(listen_fd));
            listen_fd = -1;
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    void capture_bound_port() noexcept {
        sockaddr_storage bound{};
        socklen_t bound_length = sizeof(bound);
        if (::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &bound_length) == 0) {
            if (bound.ss_family == AF_INET6) {
                port = ntohs(reinterpret_cast<sockaddr_in6&>(bound).sin6_port);
            } else if (bound.ss_family == AF_INET) {
                port = ntohs(reinterpret_cast<sockaddr_in&>(bound).sin_port);
            }
        }
    }

    [[nodiscard]] core::Status create_epoll_set() noexcept {
        epoll_fd = core::fault_injection::fault(core::fault_injection::FaultPoint::Hub_epoll)
                       ? -1
                       : ::epoll_create1(EPOLL_CLOEXEC);
        event_fd = core::fault_injection::fault(core::fault_injection::FaultPoint::Hub_eventfd)
                       ? -1
                       : ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (epoll_fd >= 0 && event_fd >= 0) {
            return {};
        }
        static_cast<void>(::close(listen_fd));
        listen_fd = -1;
        if (epoll_fd >= 0) {
            static_cast<void>(::close(epoll_fd));
            epoll_fd = -1;
        }
        if (event_fd >= 0) {
            static_cast<void>(::close(event_fd));
            event_fd = -1;
        }
        return std::unexpected(core::ErrorCode::SystemError);
    }

    void register_poll_sources() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
        epoll_event listen_event{};
        listen_event.events = EPOLLIN;
        listen_event.data.fd = listen_fd;
        static_cast<void>(::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &listen_event));
        epoll_event event_event{};
        event_event.events = EPOLLIN;
        event_event.data.fd = event_fd;
        static_cast<void>(::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &event_event));
        if (discovery.inotify_fd() >= 0) {
            epoll_event inotify_event{};
            inotify_event.events = EPOLLIN;
            inotify_event.data.fd = discovery.inotify_fd();
            static_cast<void>(
                ::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, discovery.inotify_fd(), &inotify_event));
        }
#pragma GCC diagnostic pop
    }

    [[nodiscard]] core::Status initial_sync() noexcept {
        const core::Status synced = streaming.sync(discovery.tree());
        if (!synced) {
            report("event=hub_sync_failed phase=start");
        }
        last_refresh_ns = monotonic_now_ns();
        return {};
    }

    [[nodiscard]] core::Status start(std::string_view listen) noexcept {
        std::string host;
        std::uint16_t requested_port = 0;
        bool ipv6 = false;
        if (!parse_listen(listen, host, requested_port, ipv6)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        const core::Status bound = bind_listen(host, requested_port, ipv6);
        if (!bound) {
            return bound;
        }
        capture_bound_port();
        const core::Status created = create_epoll_set();
        if (!created) {
            return created;
        }
        register_poll_sources();
        return initial_sync();
    }

    void stop() noexcept {
        if (event_fd >= 0) {
            const std::uint64_t one = 1;
            const ssize_t written = ::write(event_fd, &one, sizeof(one));
            static_cast<void>(written);
        }
    }

    void accept_clients() noexcept {
        for (;;) {
            const int fd = ::accept4(listen_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (fd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (clients.size() >= max_clients) {
                static_cast<void>(::close(fd));
                continue;
            }
            Client client{};
            client.fd = fd;
            clients.emplace(fd, std::move(client));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
            epoll_event event{};
            event.events = EPOLLIN | EPOLLRDHUP;
            event.data.fd = fd;
            static_cast<void>(::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event));
#pragma GCC diagnostic pop
        }
    }

    void close_client(int fd) noexcept {
        const auto found = clients.find(fd);
        if (found == clients.end()) {
            return;
        }
        if (found->second.subscriber != 0) {
            streaming.unsubscribe(found->second.subscriber);
        }
        if (found->second.chunked) {
            static_cast<void>(::send(fd, "0\r\n\r\n", 5, MSG_NOSIGNAL));
        }
        static_cast<void>(::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr));
        static_cast<void>(::close(fd));
        clients.erase(found);
    }

    void update_interest(Client& client) noexcept {
        if (client.fd < 0) {
            return;
        }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP;
        if (!client.output.empty()) {
            event.events |= EPOLLOUT;
        }
        event.data.fd = client.fd;
        static_cast<void>(::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client.fd, &event));
#pragma GCC diagnostic pop
    }

    [[nodiscard]] bool flush_client(Client& client) noexcept {
        while (!client.output.empty()) {
            const ssize_t sent =
                ::send(client.fd, client.output.data(), client.output.size(), MSG_NOSIGNAL);
            if (sent > 0) {
                client.output.erase(0, static_cast<std::size_t>(sent));
                continue;
            }
            if (sent < 0 && (errno == EINTR)) {
                continue;
            }
            if (sent < 0 && errno == EAGAIN) {
                return true;
            }
            return false;
        }
        return true;
    }

    void respond(Client& client, int status, std::string_view reason, std::string_view content_type,
                 std::string_view body, bool keep_alive) noexcept {
        const std::string head = std::format(
            "HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\n"
            "Cache-Control: no-store\r\nConnection: {}\r\n\r\n",
            status, reason, content_type, body.size(), keep_alive ? "keep-alive" : "close");
        client.output += head;
        client.output.append(body);
        if (!keep_alive) {
            client.close_after_write = true;
        }
    }

    void respond_json(Client& client, int status, std::string_view reason,
                      const nlohmann::json& document, bool keep_alive) noexcept {
        respond(client, status, reason, "application/json", json_text(document), keep_alive);
    }

    void start_sse(Client& client, std::string_view path, std::string_view last_event_id) noexcept {
        const std::size_t query_start = path.find('?');
        const std::string_view query = query_start == std::string_view::npos
                                           ? std::string_view{}
                                           : path.substr(query_start + 1);
        const std::string tree_topic = detail::query_value(query, "tree");
        const std::string stream = detail::query_value(query, "stream");
        if (tree_topic != "1" && !stream.empty() &&
            find_stream(discovery.tree(), stream) == nullptr) {
            respond_json(client, 404, "Not Found", {{"error", "unknown stream"}}, false);
            return;
        }

        client.sse = true;
        client.chunked = true;
        client.close_after_write = false;
        client.output += sse_headers();
        client.last_heartbeat_ns = monotonic_now_ns();

        if (tree_topic == "1") {
            client.tree_topic = true;
            client.last_tree_revision = discovery.revision();
            nlohmann::json document;
            to_json(document, discovery.tree());
            append_sse(client, sse_tree_event(document));
            return;
        }

        std::string since_text = detail::query_value(query, "since");
        if (since_text.empty() && !last_event_id.empty()) {
            const std::size_t colon = last_event_id.rfind(':');
            if (colon != std::string_view::npos) {
                since_text = std::string(last_event_id.substr(colon + 1));
            }
        }
        std::uint64_t since = 0;
        if (!since_text.empty()) {
            for (const char c : since_text) {
                if (c < '0' || c > '9') {
                    since = 0;
                    break;
                }
                since = since * std::uint64_t{10} + static_cast<std::uint64_t>(c - '0');
            }
        }
        std::vector<StreamRecord> snapshot;
        std::optional<GapMarker> gap;
        client.subscriber = streaming.subscribe(stream, since, snapshot, gap);
        append_sse(client, "event: snapshot_begin\ndata: {}\n\n");
        for (const StreamRecord& record : snapshot) {
            append_sse(client, sse_record(record));
        }
        if (gap.has_value()) {
            append_sse(client, sse_gap(*gap));
        }
        append_sse(client, "event: snapshot_end\ndata: {}\n\n");
    }

    void handle_health(Client& client, bool keep_alive) noexcept {
        respond_json(client, 200, "OK", {{"status", "ok"}, {"version", "nexigen-hub"}}, keep_alive);
    }

    void handle_tree(Client& client, bool keep_alive) noexcept {
        nlohmann::json document;
        to_json(document, discovery.tree());
        respond_json(client, 200, "OK", document, keep_alive);
    }

    void handle_streams(Client& client, bool keep_alive) noexcept {
        nlohmann::json document = nlohmann::json::array();
        for (const HubStream& stream : all_streams(discovery.tree())) {
            nlohmann::json entry;
            to_json(entry, stream);
            document.push_back(std::move(entry));
        }
        respond_json(client, 200, "OK", document, keep_alive);
    }

    void handle_recent(Client& client, std::string_view route, std::string_view path,
                       std::size_t query_start, bool keep_alive) noexcept {
        constexpr std::string_view recent_prefix = "/api/streams/";
        constexpr std::string_view recent_suffix = "/recent";
        const std::string id = detail::url_decode(route.substr(
            recent_prefix.size(), route.size() - recent_prefix.size() - recent_suffix.size()));
        const HubStream* const stream = find_stream(discovery.tree(), id);
        if (stream == nullptr) {
            respond_json(client, 404, "Not Found", {{"error", "unknown stream"}}, false);
            return;
        }
        std::size_t limit = streaming.limits().ring_records;
        if (query_start != std::string_view::npos) {
            const std::string limit_text =
                detail::query_value(path.substr(query_start + 1), "limit");
            if (!limit_text.empty()) {
                std::size_t parsed = 0;
                for (const char c : limit_text) {
                    if (c < '0' || c > '9') {
                        parsed = 0;
                        break;
                    }
                    parsed = parsed * 10U + static_cast<std::size_t>(c - '0');
                }
                if (parsed != 0) {
                    limit = std::min(parsed, streaming.limits().ring_records);
                }
            }
        }
        std::vector<StreamRecord> records;
        std::optional<GapMarker> gap;
        streaming.recent(id, limit, records, gap);
        nlohmann::json document;
        to_json(document, *stream);
        nlohmann::json array = nlohmann::json::array();
        for (const StreamRecord& record : records) {
            nlohmann::json entry;
            to_json(entry, record);
            array.push_back(std::move(entry));
        }
        document["records"] = std::move(array);
        respond_json(client, 200, "OK", document, keep_alive);
    }

    void handle_events(Client& client, std::string_view request, std::string_view path) noexcept {
        const std::string last_event_id(detail::header_value(request, "Last-Event-ID"));
        start_sse(client, path, last_event_id);
    }

    [[nodiscard]] bool handle_assets(Client& client, std::string_view route,
                                     bool keep_alive) noexcept {
        if (route == "/" || route == "/index.html") {
            respond(client, 200, "OK", "text/html; charset=utf-8", index_html(), keep_alive);
            return true;
        }
        if (route == "/app.js") {
            respond(client, 200, "OK", "text/javascript; charset=utf-8", app_js(), keep_alive);
            return true;
        }
        if (route == "/style.css") {
            respond(client, 200, "OK", "text/css; charset=utf-8", style_css(), keep_alive);
            return true;
        }
        return false;
    }

    void handle_request(Client& client, std::string_view request, bool keep_alive) noexcept {
        detail::RequestLine parsed;
        switch (detail::parse_request_line(request, parsed)) {
            case detail::RequestLineError::BadRequest:
                respond_json(client, 400, "Bad Request", {{"error", "bad request"}}, false);
                return;
            case detail::RequestLineError::BadVersion:
                respond_json(client, 400, "Bad Request", {{"error", "bad version"}}, false);
                return;
            case detail::RequestLineError::MethodNotAllowed:
                respond_json(client, 405, "Method Not Allowed", {{"error", "read only"}}, false);
                return;
            case detail::RequestLineError::None: break;
        }
        const std::string_view path = parsed.path;

        const std::size_t query_start = path.find('?');
        const std::string_view route = path.substr(0, query_start);
        if (route == "/api/health") {
            handle_health(client, keep_alive);
            return;
        }
        if (route == "/api/tree") {
            handle_tree(client, keep_alive);
            return;
        }
        if (route == "/api/streams") {
            handle_streams(client, keep_alive);
            return;
        }
        constexpr std::string_view recent_prefix = "/api/streams/";
        constexpr std::string_view recent_suffix = "/recent";
        if (starts_with(route, recent_prefix) &&
            route.size() > recent_prefix.size() + recent_suffix.size() &&
            route.substr(route.size() - recent_suffix.size()) == recent_suffix) {
            handle_recent(client, route, path, query_start, keep_alive);
            return;
        }
        if (route == "/api/events") {
            handle_events(client, request, path);
            return;
        }
        if (handle_assets(client, route, keep_alive)) {
            return;
        }
        respond_json(client, 404, "Not Found", {{"error", "not found"}}, false);
    }

    void drain_input(int fd, Client& client, bool& peer_closed) noexcept {
        std::array<char, 4096> buffer{};
        for (;;) {
            const ssize_t count = ::recv(fd, buffer.data(), buffer.size(), 0);
            if (count > 0) {
                client.input.append(buffer.data(), static_cast<std::size_t>(count));
                if (client.input.size() > max_request_bytes) {
                    respond_json(client, 400, "Bad Request", {{"error", "request too large"}},
                                 false);
                    break;
                }
                continue;
            }
            if (count == 0) {
                peer_closed = true;
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            break;
        }
    }

    void process_requests(Client& client) noexcept {
        while (!client.sse && !client.close_after_write) {
            const std::size_t header_end = client.input.find("\r\n\r\n");
            if (header_end == std::string::npos) {
                break;
            }
            const std::size_t first_line_end = client.input.find("\r\n");
            if (first_line_end == std::string::npos || first_line_end > max_request_line_bytes) {
                respond_json(client, 400, "Bad Request", {{"error", "request line too large"}},
                             false);
                break;
            }
            const std::size_t header_bytes = header_end + 4U - (first_line_end + 2U);
            if (header_bytes > max_header_bytes) {
                respond_json(client, 400, "Bad Request", {{"error", "headers too large"}}, false);
                break;
            }
            const std::string request = client.input.substr(0, header_end + 4);
            client.input.erase(0, header_end + 4);
            const bool keep_alive =
                !detail::equals_ignore_case(detail::header_value(request, "Connection"), "close");
            handle_request(client, request, keep_alive);
            if (client.sse) {
                break;
            }
        }
    }

    void read_client(int fd) noexcept {
        const auto found = clients.find(fd);
        if (found == clients.end()) {
            return;
        }
        Client& client = found->second;
        bool peer_closed = false;
        drain_input(fd, client, peer_closed);
        process_requests(client);
        if (peer_closed) {
            client.close_after_write = true;
        }
        static_cast<void>(flush_client(client));
        if (client.close_after_write && client.output.empty()) {
            close_client(fd);
        }
    }

    void write_client(int fd) noexcept {
        const auto found = clients.find(fd);
        if (found == clients.end()) {
            return;
        }
        if (!flush_client(found->second)) {
            close_client(fd);
            return;
        }
        if (found->second.close_after_write && found->second.output.empty()) {
            close_client(fd);
        }
    }

    void dispatch_live() noexcept {
        for (auto it = clients.begin(); it != clients.end();) {
            Client& client = it->second;
            if (!client.sse) {
                ++it;
                continue;
            }
            if (client.tree_topic) {
                if (client.last_tree_revision != discovery.revision()) {
                    client.last_tree_revision = discovery.revision();
                    nlohmann::json document;
                    to_json(document, discovery.tree());
                    append_sse(client, "event: tree\ndata: " + json_text(document) + "\n\n");
                }
            } else if (client.subscriber != 0) {
                std::vector<StreamRecord> records;
                std::vector<GapMarker> gaps;
                streaming.take(client.subscriber, records, gaps);
                for (const GapMarker& gap : gaps) {
                    append_sse(client, sse_gap(gap));
                }
                for (const StreamRecord& record : records) {
                    append_sse(client, sse_record(record));
                }
                if (streaming.overflowed(client.subscriber)) {
                    const int fd = it->first;
                    ++it;
                    close_client(fd);
                    continue;
                }
            }
            if (client.output.size() > max_client_output_bytes) {
                const int fd = it->first;
                ++it;
                close_client(fd);
                continue;
            }
            ++it;
        }
    }

    void try_rescan() noexcept {
        const core::Status rescanned = discovery.rescan();
        if (!rescanned) {
            report(std::format("event=discovery_rescan_failed code={}",
                               core::to_string(rescanned.error())));
        }
        const core::Status synced = streaming.sync(discovery.tree());
        if (!synced) {
            report(std::format("event=hub_sync_failed code={}", core::to_string(synced.error())));
        }
    }

    void timers() noexcept {
        const std::uint64_t now = monotonic_now_ns();
        if (now - last_refresh_ns >= refresh_interval_ns) {
            last_refresh_ns = now;
            try_rescan();
        }
        if (discovery.dirty()) {
            discovery.clear_dirty();
            try_rescan();
        }
        for (auto& [fd, client] : clients) {
            static_cast<void>(fd);
            if (client.sse && now - client.last_heartbeat_ns >= heartbeat_interval_ns) {
                client.last_heartbeat_ns = now;
                append_sse(client, ": heartbeat\n\n");
            }
        }
    }

    [[nodiscard]] int wait_events(epoll_event* events, int max_events, int timeout_ms) noexcept {
        return ::epoll_wait(epoll_fd, events, max_events, timeout_ms);
    }

    [[nodiscard]] bool dispatch_event(const epoll_event& event) noexcept {
        const int fd = event.data.fd;
        if (fd == listen_fd) {
            accept_clients();
            return false;
        }
        if (fd == event_fd) {
            std::uint64_t value = 0;
            const ssize_t read_count = ::read(event_fd, &value, sizeof(value));
            static_cast<void>(read_count);
            return true;
        }
        if (fd == discovery.inotify_fd()) {
            discovery.handle_inotify();
            return false;
        }
        const std::uint32_t events_mask = event.events;
        // EPOLLIN and EPOLLOUT may arrive together; handle them independently
        // so a readable client also drains its pending output this tick. The
        // handlers no-op when the client was already removed, so the error
        // check is safe to run last.
        if ((events_mask & EPOLLIN) != 0U) {
            read_client(fd);
        }
        if ((events_mask & EPOLLOUT) != 0U) {
            write_client(fd);
        }
        if ((events_mask & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U) {
            close_client(fd);
        }
        return false;
    }

    void drain_client_outputs() noexcept {
        for (auto& [fd, client] : clients) {
            static_cast<void>(fd);
            if (!flush_client(client)) {
                close_client(fd);
                break;
            }
            if (client.close_after_write && client.output.empty()) {
                close_client(fd);
                break;
            }
            update_interest(client);
        }
    }

    void run(std::atomic<bool>* stop_flag) noexcept {
        std::array<epoll_event, 64> events{};
        bool running = true;
        while (running && (stop_flag == nullptr || !*stop_flag)) {
            const int count = wait_events(events.data(), static_cast<int>(events.size()), 500);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            for (int index = 0; index < count; ++index) {
                if (dispatch_event(events[static_cast<std::size_t>(index)])) {
                    running = false;
                    break;
                }
            }
            if (!running) {
                break;
            }
            timers();
            streaming.poll();
            dispatch_live();
            drain_client_outputs();
        }
    }
};

HttpServer::HttpServer(Discovery& discovery, StreamManager& streaming, DiagnosticSink sink,
                       void* context)
    : impl_(std::make_unique<Impl>(discovery, streaming, sink, context)) {}

HttpServer::~HttpServer() = default;

void HttpServer::set_diagnostic_sink(DiagnosticSink sink, void* context) noexcept {
    impl_->set_diagnostic_sink(sink, context);
}

core::Status HttpServer::start(std::string_view listen) noexcept {
    return impl_->start(listen);
}

void HttpServer::run(std::atomic<bool>* stop_flag) noexcept {
    impl_->run(stop_flag);
}

void HttpServer::stop() noexcept {
    impl_->stop();
}

std::uint16_t HttpServer::port() const noexcept {
    return impl_->port;
}

std::string HttpServer::to_string() const {
    return std::format("HttpServer(port={}, clients={})", impl_->port, impl_->clients.size());
}

} // namespace nexigen::hub
