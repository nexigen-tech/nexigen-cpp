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

/// @brief Foreground HTTP/1.1 and SSE server over the discovered tree.
///
/// One epoll control loop multiplexes the listening socket, an eventfd wakeup,
/// inotify, client reads/writes, timers, and stream polling; no application
/// work ever runs here. The server serves the read-only JSON API, the SSE
/// channels, and the embedded web UI, and it rejects every non-`GET` method so
/// no supervised process can be started, stopped, or modified.
///
/// Request processing is bounded: the current limits are a 4 KiB request line,
/// 8 KiB of headers, 64 concurrent clients, and 1 MiB of buffered output per
/// client. Oversized or malformed requests receive a bounded `400`, unknown
/// paths `404`, and unsupported methods `405`. SSE replay first drains the
/// bounded ring from the requested cursor, then emits a snapshot boundary and
/// live records; a client that stops draining is closed without blocking other
/// clients or tailing.
///
/// All serving is cold path. The server owns its listener, clients, and buffer
/// state through a pimpl and holds non-owning references to the discovery and
/// streaming objects, which must outlive it.
///
/// @note Spec `hub/http-api`, requirement "The API is observation-only".
/// @note Spec `hub/http-api`, requirement "HTTP serving is cold path only".
class HttpServer {
public:
    /// @brief Construct the server around the control-plane objects.
    ///
    /// No socket is opened until `start()` is called.
    ///
    /// @param discovery Discovery observer queried for tree snapshots and
    ///        rescanned on timers and inotify events.
    /// @param streaming Stream manager queried for recent records and SSE
    ///        subscriptions.
    /// @param sink Optional diagnostic callback; `nullptr` keeps diagnostics
    ///        silent.
    /// @param context Opaque pointer passed back to `sink`.
    /// @warning `discovery` and `streaming` must outlive the server.
    HttpServer(Discovery& discovery, StreamManager& streaming, DiagnosticSink sink = nullptr,
               void* context = nullptr);

    /// @brief Close the listener, clients, and wakeup descriptors.
    ~HttpServer();

    /// @brief Deleted: the server owns a pimpl and OS descriptors.
    HttpServer(const HttpServer&) = delete;

    /// @brief Deleted: the server owns a pimpl and OS descriptors.
    HttpServer& operator=(const HttpServer&) = delete;

    /// @brief Install a callback for control-plane failures.
    ///
    /// The sink receives preformatted messages for discovery rescan failures,
    /// stream reconciliation failures, and stream I/O failures. Passing
    /// `nullptr` (the default) keeps diagnostics silent.
    ///
    /// @param sink Callback that must not throw and should be cheap.
    /// @param context Opaque pointer handed to `sink` on every call.
    /// @note Spec `hub/http-api`, requirement "Control-plane refresh failures
    ///       are reported".
    void set_diagnostic_sink(DiagnosticSink sink, void* context) noexcept;

    /// @brief Bind the listening socket and start serving.
    ///
    /// Parses `host:port` (IPv4 literal, `localhost`, or bracketed IPv6), binds
    /// with the family-specific options, creates the epoll/eventfd set, and
    /// performs the first discovery and stream sync. Port `0` asks the kernel
    /// for an ephemeral port, which tests read back through `port()`.
    ///
    /// @param listen Listen address in `host:port` form.
    /// @return Empty `core::Status` on success, `InvalidArgument` for a
    ///         malformed address, or `SystemError` when binding, epoll setup,
    ///         or the initial sync fails.
    /// @note Spec `hub/http-api`, requirement "HTTP listener is loopback by
    ///       default".
    /// @note Spec `hub/http-api`, scenario "Invalid address fails startup".
    [[nodiscard]] core::Status start(std::string_view listen) noexcept;

    /// @brief Run the blocking epoll loop until stopped.
    ///
    /// Each iteration collects events, dispatches them, runs timers and the
    /// stream poll, delivers live records, and drains client output. The stop
    /// flag is polled between events.
    ///
    /// @param stop_flag Optional external stop flag; `nullptr` runs until
    ///        `stop()` is called.
    /// @note Spec `hub/http-api`, requirement "The server run loop uses an
    ///       explicit state stepper".
    void run(std::atomic<bool>* stop_flag) noexcept;

    /// @brief Wake the loop and request shutdown.
    ///
    /// Signals the eventfd; safe to call from the control thread while `run()`
    /// executes on another thread.
    void stop() noexcept;

    /// @brief The bound local port.
    ///
    /// @return Port resolved after `start()` (the kernel-chosen one for port
    ///         `0`), or `0` before a successful bind.
    [[nodiscard]] std::uint16_t port() const noexcept;

    /// @brief Render the server state as a diagnostic string.
    ///
    /// @return Text naming the bound port and client count; allocates.
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Opaque listener, client, timer, and buffer state.
    struct Impl;
    std::unique_ptr<Impl> impl_; ///< Owned implementation; null after destruction only.
};

} // namespace nexigen::hub
