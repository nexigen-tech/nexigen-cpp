// nexigen::metrics — testable HTTP front end for the metrics daemon
// (change apply-thread-specs-platform-wide; spec metrics/metrics-daemon
// "MetricsDaemon HTTP thread is configurable").
//
// The serving worker owns the HTTP thread, applies the effective ThreadSpec,
// and bridges the daemon's stop request to the server's atomic stop flag.
#pragma once

#include <atomic>
#include <cstdint>
#include <stop_token>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/worker_thread.hpp>
#include <nexigen/metrics/detail/exposition_buffer.hpp>
#include <nexigen/metrics/detail/metric_http_server.hpp>
#include <nexigen/metrics/metric_daemon_config.hpp>

namespace nexigen::metrics::detail {

/// @brief HTTP scrape front end: the server, its stop flag and its serving worker.
///
/// Owns the listener through `MetricHttpServer` and runs `serve` on a
/// `core::WorkerThread`, so the effective `ThreadSpec` (name, affinity,
/// scheduling) is applied before any request is accepted.
///
/// @note Spec `metrics/metrics-daemon`, requirement "MetricsDaemon HTTP thread
///       is configurable" and requirement "Daemon serves GET /metrics over
///       HTTP".
struct HttpFront {
    /// Listener and request loop; started before the worker.
    MetricHttpServer server{};
    /// Stop request bridged from the worker's stop token to the accept loop.
    std::atomic<bool> stop{false};

    /// @brief Thread body: bridge the stop token, then serve until stopped.
    ///
    /// Holds the borrowed server, stop flag and exposition snapshot for the
    /// lifetime of the worker.
    struct Body {
        /// Server to run; borrowed for the worker's lifetime.
        MetricHttpServer* server{nullptr};
        /// Stop flag the server polls; borrowed for the worker's lifetime.
        std::atomic<bool>* stop{nullptr};
        /// Published exposition the server answers from; borrowed.
        const ExpositionBuffer* exposition{nullptr};

        /// @brief Serve HTTP requests until the worker is asked to stop.
        ///
        /// Installs a `std::stop_callback` that sets `stop`, so a stop request
        /// wakes the accept loop, then calls `server->serve`.
        ///
        /// @param token Stop token from the worker; cancellation sets `stop`.
        void run(std::stop_token token) noexcept {
            std::stop_callback request_stop{
                token, [this] { stop->store(true, std::memory_order_relaxed); }};
            server->serve(*stop, *exposition);
        }
    };

    /// Serving thread; started by `start_http` with the effective spec.
    core::WorkerThread<Body> worker{};
};

/// @brief Bind the listener and start the serving worker with the effective spec.
///
/// Uses `cfg.http_thread`, falling back to `cfg.bg_cpu_affinity`, so the HTTP
/// thread is configurable like the other daemon workers. A disabled HTTP front
/// is a successful no-op.
///
/// @param front Front end whose server and worker are started.
/// @param cfg Daemon configuration carrying bind address, port, endpoint, the
///        enable flag and the thread specs.
/// @param exposition Published exposition the server answers from; borrowed and
///        must outlive the worker.
/// @return `true` on success or when HTTP is disabled; `false` when the listener
///         cannot bind or the worker cannot start (the listener is then closed).
/// @note Spec `metrics/metrics-daemon`, requirement "MetricsDaemon HTTP thread
///       is configurable".
[[nodiscard]] inline bool start_http(HttpFront& front, const MetricDaemonConfig& cfg,
                                     const ExpositionBuffer& exposition) {
    if (!cfg.enable_http) {
        return true;
    }
    if (!front.server.start(cfg.bind_address, cfg.port, cfg.endpoint)) {
        return false;
    }
    const core::ThreadSpec spec = core::effective_thread_spec(cfg.http_thread, cfg.bg_cpu_affinity);
    const core::Status started =
        front.worker.start(spec, HttpFront::Body{&front.server, &front.stop, &exposition});
    if (!started) {
        front.server.stop_listen();
        return false;
    }
    return true;
}

/// @brief Stop and join the serving worker before closing the listener.
///
/// @param front Front end to shut down; safe to call when it was never started.
/// @note Spec `metrics/metrics-daemon`, requirement "Daemon serves GET /metrics
///       over HTTP".
inline void stop_http(HttpFront& front) noexcept {
    front.worker.stop();
    front.stop.store(true, std::memory_order_relaxed);
    front.server.stop_listen();
}

} // namespace nexigen::metrics::detail
