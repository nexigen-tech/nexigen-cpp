// nexigen::metrics — minimal Prometheus HTTP responder for the MetricsDaemon
// (change add-metrics-daemon; spec metrics/metrics-daemon "Daemon serves GET
// /metrics over HTTP"; design D5).
//
// One blocking poll/accept loop on its own thread; `GET <endpoint>` returns the
// current exposition snapshot, every other path returns 404. Request-line
// parsing reuses the pure hub::detail helpers. No SSE, no discovery coupling.
#pragma once

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nexigen/hub/detail/http_parse.hpp>
#include <nexigen/metrics/detail/exposition_buffer.hpp>

namespace nexigen::metrics::detail {

class MetricHttpServer {
public:
    MetricHttpServer() = default;

    MetricHttpServer(const MetricHttpServer&) = delete;
    MetricHttpServer& operator=(const MetricHttpServer&) = delete;

    ~MetricHttpServer() { stop_listen(); }

    /// Bind and listen; port 0 asks the kernel for an ephemeral port.
    [[nodiscard]] bool start(std::string_view bind_address, std::uint16_t port,
                             std::string endpoint) noexcept {
        endpoint_ = std::move(endpoint);
        const std::string host(bind_address);
        const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            return false;
        }
        int one = 1;
        static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
            ::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            ::listen(fd, 16) != 0) {
            static_cast<void>(::close(fd));
            return false;
        }
        listen_fd_ = fd;
        return true;
    }

    /// The actual bound port (useful when start() was asked for port 0).
    [[nodiscard]] std::uint16_t port() const noexcept {
        if (listen_fd_ < 0) {
            return 0;
        }
        sockaddr_in address{};
        socklen_t length = sizeof(address);
        if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            return 0;
        }
        return ntohs(address.sin_port);
    }

    /// Serve requests until `stop` is set. Runs on a dedicated thread.
    void serve(std::atomic<bool>& stop, const ExpositionBuffer& exposition) noexcept {
        while (!stop.load(std::memory_order_relaxed) && listen_fd_ >= 0) {
            pollfd descriptor{listen_fd_, POLLIN, 0};
            const int ready = ::poll(&descriptor, 1, 200);
            if (ready <= 0) {
                continue;
            }
            sockaddr_in peer{};
            socklen_t peer_length = sizeof(peer);
            const int client =
                ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_length);
            if (client < 0) {
                continue;
            }
            handle_connection(client, exposition);
            static_cast<void>(::close(client));
        }
    }

    void stop_listen() noexcept {
        if (listen_fd_ >= 0) {
            static_cast<void>(::close(listen_fd_));
            listen_fd_ = -1;
        }
    }

private:
    static void send_all(int fd, const char* data, std::size_t size) noexcept {
        std::size_t offset = 0;
        while (offset < size) {
            const ssize_t written = ::send(fd, data + offset, size - offset, MSG_NOSIGNAL);
            if (written > 0) {
                offset += static_cast<std::size_t>(written);
            } else if (errno != EINTR) {
                break;
            }
        }
    }

    void handle_connection(int client, const ExpositionBuffer& exposition) noexcept {
        timeval timeout{2, 0};
        static_cast<void>(::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));

        char request[8192];
        std::size_t used = 0;
        while (used + 1U < sizeof(request)) {
            const ssize_t received = ::recv(client, request + used, sizeof(request) - 1U - used, 0);
            if (received <= 0) {
                break;
            }
            used += static_cast<std::size_t>(received);
            request[used] = '\0';
            if (std::string_view(request, used).find("\r\n\r\n") != std::string_view::npos) {
                break;
            }
        }

        hub::detail::RequestLine line{};
        const auto error = hub::detail::parse_request_line(std::string_view(request, used), line);
        const bool matched = error == hub::detail::RequestLineError::None && line.path == endpoint_;
        if (!matched) {
            static constexpr char not_found[] =
                "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all(client, not_found, sizeof(not_found) - 1U);
            return;
        }

        const std::string body(exposition.current());
        char header[256];
        const int header_size = std::snprintf(
            header, sizeof(header),
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n",
            body.size());
        if (header_size > 0) {
            send_all(client, header, static_cast<std::size_t>(header_size));
        }
        send_all(client, body.data(), body.size());
    }

    int listen_fd_{-1};
    std::string endpoint_{};
};

} // namespace nexigen::metrics::detail
