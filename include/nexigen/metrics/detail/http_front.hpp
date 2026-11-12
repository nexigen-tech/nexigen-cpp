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

/// HTTP scrape front end: the server, its stop flag and its serving worker.
struct HttpFront {
    MetricHttpServer server{};
    std::atomic<bool> stop{false};

    struct Body {
        MetricHttpServer* server{nullptr};
        std::atomic<bool>* stop{nullptr};
        const ExpositionBuffer* exposition{nullptr};

        void run(std::stop_token token) noexcept {
            std::stop_callback request_stop{
                token, [this] { stop->store(true, std::memory_order_relaxed); }};
            server->serve(*stop, *exposition);
        }
    };

    core::WorkerThread<Body> worker{};
};

/// Bind the listener and start the serving worker with the effective spec
/// (`http_thread` or the `bg_cpu_affinity` fallback).
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

/// Stop and join the serving worker before closing the listener.
inline void stop_http(HttpFront& front) noexcept {
    front.worker.stop();
    front.stop.store(true, std::memory_order_relaxed);
    front.server.stop_listen();
}

} // namespace nexigen::metrics::detail
