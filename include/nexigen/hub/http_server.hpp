// nexigen::hub — minimal HTTP/1.1 + SSE server
// (change nexigen-hub; spec hub/http-api).
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/hub/discovery.hpp>
#include <nexigen/hub/streaming.hpp>

namespace nexigen::hub {

/// Foreground HTTP/SSE server. One epoll control loop; no application work.
class HttpServer {
public:
    HttpServer(Discovery& discovery, StreamManager& streaming,
                DiagnosticSink sink = nullptr, void* context = nullptr);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /// Install a callback for rescan, stream reconciliation, and stream I/O
    /// failures. Passing nullptr (the default) keeps diagnostics silent.
    void set_diagnostic_sink(DiagnosticSink sink, void* context) noexcept;

    /// Bind and start listening. `--listen` accepts `host:port`; port 0 asks
    /// the kernel for an ephemeral port (used by tests).
    [[nodiscard]] core::Status start(std::string_view listen) noexcept;

    /// Blocking epoll loop. `stop_flag` is polled between events; pass nullptr
    /// to run until stop() is called.
    void run(std::atomic<bool>* stop_flag) noexcept;

    /// Wake the loop and request shutdown. Safe from a control thread.
    void stop() noexcept;

    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] std::string to_string() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nexigen::hub
