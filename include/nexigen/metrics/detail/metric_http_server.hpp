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

/// @brief Minimal Prometheus HTTP responder used by the metrics daemon.
///
/// One blocking `poll`/`accept` loop on its own thread: a `GET` to the
/// configured endpoint returns the current exposition snapshot with status 200
/// and the Prometheus text content type; every other path returns 404.
/// Request-line parsing reuses the pure `hub::detail` helpers. No SSE and no
/// discovery coupling. Owns its listening socket and closes it on destruction.
///
/// @note Spec `metrics/metrics-daemon`, requirement "Daemon serves GET /metrics
///       over HTTP".
class MetricHttpServer {
public:
    /// @brief Construct an unbound server.
    MetricHttpServer() = default;

    /// @brief Copy construction is deleted: the server owns a listening socket.
    MetricHttpServer(const MetricHttpServer&) = delete;
    /// @brief Copy assignment is deleted: the server owns a listening socket.
    MetricHttpServer& operator=(const MetricHttpServer&) = delete;

    /// @brief Close the listening socket.
    ~MetricHttpServer() { stop_listen(); }

    /// @brief Bind and listen; port 0 asks the kernel for an ephemeral port.
    ///
    /// Creates an IPv4 stream socket with `SO_REUSEADDR`, binds it to the parsed
    /// address and listens with a backlog of 16. All failures close the socket
    /// and leave the server unbound.
    ///
    /// @param bind_address Dotted-quad IPv4 address accepted by `inet_pton`.
    /// @param port TCP port, or 0 for an ephemeral port (see `port()`).
    /// @param endpoint Request path answered with exposition, e.g. `"/metrics"`.
    /// @return `true` when bound and listening; `false` on invalid address, bind
    ///         failure, listen failure or socket creation failure.
    /// @note Spec `metrics/metrics-daemon`, requirement "Daemon serves GET
    ///       /metrics over HTTP".
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

    /// @brief Read the actual bound TCP port.
    ///
    /// @return The port from `getsockname`, 0 when unbound or on failure. Useful
    ///         when `start()` was asked for port 0.
    /// @note Spec `metrics/metrics-daemon`, requirement "Daemon serves GET
    ///       /metrics over HTTP".
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

    /// @brief Serve requests until `stop` is set.
    ///
    /// Polls the listening socket with a 200 ms timeout, accepts one connection
    /// at a time, answers it and closes it. Runs on a dedicated thread driven by
    /// `HttpFront`.
    ///
    /// @param stop Stop flag; a `true` value ends the loop without draining.
    /// @param exposition Published snapshot served for the configured endpoint.
    /// @warning Blocking call: it does not return until `stop` is set or the
    ///          listener is closed.
    /// @note Spec `metrics/metrics-daemon`, requirement "Daemon serves GET
    ///       /metrics over HTTP".
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

    /// @brief Close the listening socket and mark the server unbound.
    ///
    /// Idempotent; a subsequent `serve` loop exits immediately.
    /// @note Spec `metrics/metrics-daemon`, requirement "Daemon serves GET
    ///       /metrics over HTTP".
    void stop_listen() noexcept {
        if (listen_fd_ >= 0) {
            static_cast<void>(::close(listen_fd_));
            listen_fd_ = -1;
        }
    }

private:
    /// @brief Write a whole buffer with `send`, retrying partial writes and `EINTR`.
    ///
    /// @param fd Connected client socket.
    /// @param data Bytes to send.
    /// @param size Number of bytes in `data`.
    /// @note Uses `MSG_NOSIGNAL` so a peer reset cannot raise `SIGPIPE`; aborts
    ///       quietly on any other error.
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

    /// @brief Read one request, answer it, and leave the socket for the caller to close.
    ///
    /// Applies a 2 s receive timeout, reads until the header terminator or the
    /// 8 KiB request buffer is full, and parses the request line. A `GET` whose
    /// path equals the configured endpoint gets the exposition with status 200
    /// and `text/plain; version=0.0.4`; anything else gets 404.
    ///
    /// @param client Connected client socket; borrowed, closed by the caller.
    /// @param exposition Snapshot whose `current()` text forms the 200 body.
    /// @warning Single-connection, single-threaded: it blocks for the receive
    ///          timeout if the peer sends nothing.
    /// @note Spec `metrics/metrics-daemon`, requirement "Daemon serves GET
    ///       /metrics over HTTP".
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
