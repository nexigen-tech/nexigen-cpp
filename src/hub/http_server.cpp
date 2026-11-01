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
#include <nexigen/hub/discovery.hpp>
#include <nexigen/hub/http_server.hpp>
#include <nexigen/hub/model.hpp>
#include <nexigen/hub/streaming.hpp>
#include <nexigen/hub/web_assets.hpp>

namespace nexigen::hub {

namespace {

constexpr std::size_t max_request_line_bytes = 4U * 1024U;
constexpr std::size_t max_header_bytes = 8U * 1024U;
constexpr std::size_t max_url_bytes = 64U * 1024U;
constexpr std::size_t max_request_bytes =
    max_request_line_bytes + max_header_bytes + 4U;
constexpr std::size_t max_client_output_bytes = 1U * 1024U * 1024U;
constexpr std::size_t max_clients = 64;
constexpr std::uint64_t heartbeat_interval_ns = 15ULL * 1'000'000'000ULL;
constexpr std::uint64_t refresh_interval_ns = 1ULL * 1'000'000'000ULL;

[[nodiscard]] std::uint64_t monotonic_now_ns() noexcept {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())
            .count());
}


[[nodiscard]] std::string url_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '%' && index + 2 < text.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(text[index + 1]);
            const int lo = hex(text[index + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                index += 2;
                continue;
            }
        }
        out.push_back(text[index] == '+' ? ' ' : text[index]);
    }
    return out;
}

[[nodiscard]] std::string json_text(const nlohmann::json& document) {
    return document.dump();
}

[[nodiscard]] bool starts_with(std::string_view text,
                               std::string_view prefix) noexcept {
    return text.size() >= prefix.size() &&
           text.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] std::string sse_record(const Stream_record& record) {
    nlohmann::json document;
    to_json(document, record);
    return "event: record\nid: " + record.stream_id + ":" +
           std::to_string(record.sequence) + "\ndata: " +
           json_text(document) + "\n\n";
}

[[nodiscard]] std::string sse_gap(const Gap_marker& gap) {
    nlohmann::json document;
    to_json(document, gap);
    return "event: gap\ndata: " + json_text(document) + "\n\n";
}

} // namespace

struct Http_server::Impl {
    struct Client {
        int fd{-1};
        std::string input{};
        std::string output{};
        bool close_after_write{false};
        bool sse{false};
        bool tree_topic{false};
        std::uint64_t subscriber{0};
        std::uint64_t last_tree_revision{0};
        std::uint64_t last_heartbeat_ns{0};
    };

    Discovery& discovery;
    Stream_manager& streaming;
    int listen_fd{-1};
    int epoll_fd{-1};
    int event_fd{-1};
    std::uint16_t port{0};
    std::unordered_map<int, Client> clients{};
    std::uint64_t last_refresh_ns{0};

    Impl(Discovery& discovery_ref, Stream_manager& streaming_ref) noexcept
        : discovery(discovery_ref), streaming(streaming_ref) {}

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

    [[nodiscard]] bool parse_listen(std::string_view listen,
                                    std::string& host,
                                    std::uint16_t& out_port) const {
        if (listen.empty()) {
            return false;
        }
        const std::size_t colon = listen.rfind(':');
        if (colon == std::string_view::npos || colon == 0 ||
            colon + 1 >= listen.size()) {
            return false;
        }
        host = std::string(listen.substr(0, colon));
        if (host == "localhost") {
            host = "127.0.0.1";
        }
        std::uint64_t port_value = 0;
        for (std::size_t index = colon + 1; index < listen.size(); ++index) {
            const char c = listen[index];
            if (c < '0' || c > '9') {
                return false;
            }
            port_value = port_value * std::uint64_t{10} +
                         static_cast<std::uint64_t>(c - '0');
            if (port_value > 65535ULL) {
                return false;
            }
        }
        out_port = static_cast<std::uint16_t>(port_value);
        return true;
    }

    [[nodiscard]] core::Status start(std::string_view listen) noexcept {
        std::string host;
        std::uint16_t requested_port = 0;
        if (!parse_listen(listen, host, requested_port)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(requested_port);
        if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                             0);
        if (listen_fd < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        int enabled = 1;
        static_cast<void>(::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                                        &enabled, sizeof(enabled)));
        if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&address),
                   sizeof(address)) != 0 ||
            ::listen(listen_fd, 128) != 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        sockaddr_in bound{};
        socklen_t bound_length = sizeof(bound);
        if (::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound),
                          &bound_length) == 0) {
            port = ntohs(bound.sin_port);
        }

        epoll_fd = ::epoll_create1(EPOLL_CLOEXEC);
        event_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (epoll_fd < 0 || event_fd < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
        epoll_event listen_event{};
        listen_event.events = EPOLLIN;
        listen_event.data.fd = listen_fd;
        static_cast<void>(::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd,
                                      &listen_event));
        epoll_event event_event{};
        event_event.events = EPOLLIN;
        event_event.data.fd = event_fd;
        static_cast<void>(::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd,
                                      &event_event));
#pragma GCC diagnostic pop
        if (discovery.inotify_fd() >= 0) {
            epoll_event inotify_event{};
            inotify_event.events = EPOLLIN;
            inotify_event.data.fd = discovery.inotify_fd();
            static_cast<void>(::epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
                                          discovery.inotify_fd(),
                                          &inotify_event));
        }
        streaming.sync(discovery.tree());
        last_refresh_ns = monotonic_now_ns();
        return {};
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
            const int fd = ::accept4(listen_fd, nullptr, nullptr,
                                    SOCK_NONBLOCK | SOCK_CLOEXEC);
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
        static_cast<void>(::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client.fd,
                                      &event));
#pragma GCC diagnostic pop
    }

    [[nodiscard]] bool flush_client(Client& client) noexcept {
        while (!client.output.empty()) {
            const ssize_t sent = ::send(client.fd, client.output.data(),
                                        client.output.size(), MSG_NOSIGNAL);
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

    void respond(Client& client, int status, std::string_view reason,
                 std::string_view content_type, std::string_view body,
                 bool keep_alive) noexcept {
        const std::string head = std::format(
            "HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\n"
            "Cache-Control: no-store\r\nConnection: {}\r\n\r\n",
            status, reason, content_type, body.size(),
            keep_alive ? "keep-alive" : "close");
        client.output += head;
        client.output.append(body);
        if (!keep_alive) {
            client.close_after_write = true;
        }
    }

    void respond_json(Client& client, int status, std::string_view reason,
                      const nlohmann::json& document, bool keep_alive) noexcept {
        respond(client, status, reason, "application/json", json_text(document),
                keep_alive);
    }

    [[nodiscard]] static std::string query_value(std::string_view query,
                                                 std::string_view key) {
        std::size_t position = 0;
        while (position < query.size()) {
            const std::size_t ampersand = query.find('&', position);
            const std::size_t end =
                ampersand == std::string_view::npos ? query.size() : ampersand;
            const std::string_view pair = query.substr(position, end - position);
            const std::size_t equals = pair.find('=');
            const std::string_view name = pair.substr(0, equals);
            if (name == key) {
                return equals == std::string_view::npos
                           ? std::string{}
                           : url_decode(pair.substr(equals + 1));
            }
            if (ampersand == std::string_view::npos) {
                break;
            }
            position = ampersand + 1;
        }
        return {};
    }

    void start_sse(Client& client, std::string_view path,
                   std::string_view last_event_id) noexcept {
        const std::size_t query_start = path.find('?');
        const std::string_view route = path.substr(0, query_start);
        const std::string_view query =
            query_start == std::string_view::npos
                ? std::string_view{}
                : path.substr(query_start + 1);
        if (route != "/api/events") {
            respond_json(client, 404, "Not Found", {{"error", "not found"}},
                         false);
            return;
        }
        const std::string tree_topic = query_value(query, "tree");
        const std::string stream = query_value(query, "stream");
        if (tree_topic != "1" && !stream.empty() &&
            find_stream(discovery.tree(), stream) == nullptr) {
            respond_json(client, 404, "Not Found", {{"error", "unknown stream"}},
                         false);
            return;
        }

        client.sse = true;
        client.close_after_write = false;
        client.output +=
            "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\nConnection: keep-alive\r\n"
            "X-Accel-Buffering: no\r\n\r\n";
        client.last_heartbeat_ns = monotonic_now_ns();

        if (tree_topic == "1") {
            client.tree_topic = true;
            client.last_tree_revision = discovery.revision();
            nlohmann::json document;
            to_json(document, discovery.tree());
            client.output += "event: tree\ndata: " + json_text(document) + "\n\n";
            return;
        }

        std::string since_text = query_value(query, "since");
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
        std::vector<Stream_record> snapshot;
        std::optional<Gap_marker> gap;
        client.subscriber = streaming.subscribe(stream, since, snapshot, gap);
        client.output += "event: snapshot_begin\ndata: {}\n\n";
        for (const Stream_record& record : snapshot) {
            client.output += sse_record(record);
        }
        if (gap.has_value()) {
            client.output += sse_gap(*gap);
        }
        client.output += "event: snapshot_end\ndata: {}\n\n";
    }

    void handle_request(Client& client, std::string_view request,
                        bool keep_alive) noexcept {
        const std::size_t first_line_end = request.find("\r\n");
        const std::string_view first_line = request.substr(0, first_line_end);
        const std::size_t first_space = first_line.find(' ');
        const std::size_t second_space =
            first_space == std::string_view::npos
                ? std::string_view::npos
                : first_line.find(' ', first_space + 1);
        if (first_space == std::string_view::npos ||
            second_space == std::string_view::npos) {
            respond_json(client, 400, "Bad Request", {{"error", "bad request"}},
                         false);
            return;
        }
        const std::string_view method = first_line.substr(0, first_space);
        const std::string_view path =
            first_line.substr(first_space + 1, second_space - first_space - 1);
        const std::string_view version = first_line.substr(second_space + 1);
        if (path.size() > max_url_bytes) {
            respond_json(client, 400, "Bad Request",
                         {{"error", "URL too large"}}, false);
            return;
        }
        if (version != "HTTP/1.1" && version != "HTTP/1.0") {
            respond_json(client, 400, "Bad Request", {{"error", "bad version"}},
                         false);
            return;
        }
        if (method != "GET") {
            respond_json(client, 405, "Method Not Allowed",
                         {{"error", "read only"}}, false);
            return;
        }

        const std::size_t query_start = path.find('?');
        const std::string_view route = path.substr(0, query_start);
        if (route == "/api/health") {
            respond_json(client, 200, "OK",
                         {{"status", "ok"}, {"version", "nexigen-hub"}},
                         keep_alive);
            return;
        }
        if (route == "/api/tree") {
            nlohmann::json document;
            to_json(document, discovery.tree());
            respond_json(client, 200, "OK", document, keep_alive);
            return;
        }
        if (route == "/api/streams") {
            nlohmann::json document = nlohmann::json::array();
            for (const Hub_stream& stream : all_streams(discovery.tree())) {
                nlohmann::json entry;
                to_json(entry, stream);
                document.push_back(std::move(entry));
            }
            respond_json(client, 200, "OK", document, keep_alive);
            return;
        }
        constexpr std::string_view recent_prefix = "/api/streams/";
        const std::string_view recent_suffix = "/recent";
        if (starts_with(route, recent_prefix) &&
            route.size() > recent_prefix.size() + recent_suffix.size() &&
            route.substr(route.size() - recent_suffix.size()) == recent_suffix) {
            const std::string id =
                url_decode(route.substr(recent_prefix.size(),
                                        route.size() - recent_prefix.size() -
                                            recent_suffix.size()));
            if (find_stream(discovery.tree(), id) == nullptr) {
                respond_json(client, 404, "Not Found",
                             {{"error", "unknown stream"}}, false);
                return;
            }
            std::size_t limit = streaming.limits().ring_records;
            if (query_start != std::string_view::npos) {
                const std::string limit_text =
                    query_value(path.substr(query_start + 1), "limit");
                if (!limit_text.empty()) {
                    std::size_t parsed = 0;
                    for (const char c : limit_text) {
                        if (c < '0' || c > '9') {
                            parsed = 0;
                            break;
                        }
                        parsed = parsed * 10U +
                                 static_cast<std::size_t>(c - '0');
                    }
                    if (parsed != 0) {
                        limit = std::min(parsed,
                                         streaming.limits().ring_records);
                    }
                }
            }
            std::vector<Stream_record> records;
            std::optional<Gap_marker> gap;
            streaming.recent(id, limit, records, gap);
            nlohmann::json document;
            to_json(document, *find_stream(discovery.tree(), id));
            nlohmann::json array = nlohmann::json::array();
            for (const Stream_record& record : records) {
                nlohmann::json entry;
                to_json(entry, record);
                array.push_back(std::move(entry));
            }
            document["records"] = std::move(array);
            if (gap.has_value()) {
                to_json(document["gap"], *gap);
            }
            respond_json(client, 200, "OK", document, keep_alive);
            return;
        }
        if (route == "/api/events") {
            std::string last_event_id;
            const std::size_t last_pos = request.find("Last-Event-ID: ");
            const std::size_t last_pos_lower = request.find("last-event-id: ");
            const std::size_t position =
                last_pos != std::string_view::npos ? last_pos : last_pos_lower;
            if (position != std::string_view::npos) {
                const std::size_t value_start = position + 15U;
                const std::size_t value_end = request.find("\r\n", value_start);
                if (value_end != std::string_view::npos) {
                    last_event_id = std::string(
                        request.substr(value_start, value_end - value_start));
                }
            }
            start_sse(client, path, last_event_id);
            return;
        }
        if (route == "/" || route == "/index.html") {
            respond(client, 200, "OK", "text/html; charset=utf-8", index_html(),
                    keep_alive);
            return;
        }
        if (route == "/app.js") {
            respond(client, 200, "OK", "text/javascript; charset=utf-8",
                    app_js(), keep_alive);
            return;
        }
        if (route == "/style.css") {
            respond(client, 200, "OK", "text/css; charset=utf-8", style_css(),
                    keep_alive);
            return;
        }
        respond_json(client, 404, "Not Found", {{"error", "not found"}}, false);
    }

    void read_client(int fd) noexcept {
        const auto found = clients.find(fd);
        if (found == clients.end()) {
            return;
        }
        Client& client = found->second;
        std::array<char, 4096> buffer{};
        for (;;) {
            const ssize_t count = ::recv(fd, buffer.data(), buffer.size(), 0);
            if (count > 0) {
                client.input.append(buffer.data(), static_cast<std::size_t>(count));
                if (client.input.size() > max_request_bytes) {
                    respond_json(client, 400, "Bad Request",
                                 {{"error", "request too large"}}, false);
                    break;
                }
                continue;
            }
            if (count == 0) {
                close_client(fd);
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        while (!client.sse && !client.close_after_write) {
            const std::size_t header_end = client.input.find("\r\n\r\n");
            if (header_end == std::string::npos) {
                break;
            }
            const std::size_t first_line_end = client.input.find("\r\n");
            if (first_line_end == std::string::npos ||
                first_line_end > max_request_line_bytes) {
                respond_json(client, 400, "Bad Request",
                             {{"error", "request line too large"}}, false);
                break;
            }
            const std::size_t header_bytes =
                header_end + 4U - (first_line_end + 2U);
            if (header_bytes > max_header_bytes) {
                respond_json(client, 400, "Bad Request",
                             {{"error", "headers too large"}}, false);
                break;
            }
            const std::string request = client.input.substr(0, header_end + 4);
            client.input.erase(0, header_end + 4);
            const bool keep_alive =
                request.find("Connection: close") == std::string::npos &&
                request.find("connection: close") == std::string::npos;
            handle_request(client, request, keep_alive);
            if (client.sse) {
                break;
            }
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
                    client.output +=
                        "event: tree\ndata: " + json_text(document) + "\n\n";
                }
            } else if (client.subscriber != 0) {
                std::vector<Stream_record> records;
                std::vector<Gap_marker> gaps;
                streaming.take(client.subscriber, records, gaps);
                for (const Gap_marker& gap : gaps) {
                    client.output += sse_gap(gap);
                }
                for (const Stream_record& record : records) {
                    client.output += sse_record(record);
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

    void timers() noexcept {
        const std::uint64_t now = monotonic_now_ns();
        if (now - last_refresh_ns >= refresh_interval_ns) {
            last_refresh_ns = now;
            static_cast<void>(discovery.rescan());
            streaming.sync(discovery.tree());
        }
        if (discovery.dirty()) {
            discovery.clear_dirty();
            static_cast<void>(discovery.rescan());
            streaming.sync(discovery.tree());
        }
        for (auto& [fd, client] : clients) {
            static_cast<void>(fd);
            if (client.sse &&
                now - client.last_heartbeat_ns >= heartbeat_interval_ns) {
                client.last_heartbeat_ns = now;
                client.output += ": heartbeat\n\n";
            }
        }
    }

    void run(std::atomic<bool>* stop_flag) noexcept {
        std::array<epoll_event, 64> events{};
        while (stop_flag == nullptr || !*stop_flag) {
            const int count = ::epoll_wait(epoll_fd, events.data(),
                                           static_cast<int>(events.size()), 500);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            for (int index = 0; index < count; ++index) {
                const int fd = events[static_cast<std::size_t>(index)].data.fd;
                if (fd == listen_fd) {
                    accept_clients();
                } else if (fd == event_fd) {
                    std::uint64_t value = 0;
                    const ssize_t read_count =
                        ::read(event_fd, &value, sizeof(value));
                    static_cast<void>(read_count);
                    return;
                } else if (fd == discovery.inotify_fd()) {
                    discovery.handle_inotify();
                } else {
                    if ((events[static_cast<std::size_t>(index)].events &
                     (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U) {
                        close_client(fd);
                    } else if ((events[static_cast<std::size_t>(index)].events & EPOLLIN) != 0U) {
                        read_client(fd);
                    } else if ((events[static_cast<std::size_t>(index)].events & EPOLLOUT) != 0U) {
                        write_client(fd);
                    }
                }
            }
            timers();
            streaming.poll();
            dispatch_live();
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
    }
};

Http_server::Http_server(Discovery& discovery, Stream_manager& streaming)
    : impl_(std::make_unique<Impl>(discovery, streaming)) {}

Http_server::~Http_server() = default;

core::Status Http_server::start(std::string_view listen) noexcept {
    return impl_->start(listen);
}

void Http_server::run(std::atomic<bool>* stop_flag) noexcept {
    impl_->run(stop_flag);
}

void Http_server::stop() noexcept {
    impl_->stop();
}

std::uint16_t Http_server::port() const noexcept {
    return impl_->port;
}

std::string Http_server::to_string() const {
    return std::format("Http_server(port={}, clients={})", impl_->port,
                       impl_->clients.size());
}

} // namespace nexigen::hub
