// nexigen::metrics — double-buffered Prometheus exposition snapshot (change
// add-metrics-daemon; design D5).
//
// The drain thread renders into the buffer that is NOT currently published,
// then swaps the published index with release semantics. The HTTP thread reads
// the published index with acquire and never touches the buffer being written.
// The object is ~2 MiB from two 1 MiB buffers: allocate it on the heap.
#pragma once

#include <atomic>
#include <cstddef>
#include <string_view>

#include <nexigen/metrics/detail/metric_aggregator.hpp>
#include <nexigen/metrics/detail/prometheus_render.hpp>

namespace nexigen::metrics::detail {

/// @brief Double-buffered Prometheus exposition snapshot shared by two threads.
///
/// The drain thread renders into the buffer that is not published and then
/// swaps the published index with release semantics; the HTTP thread reads the
/// index with acquire and never touches the buffer being written. The object is
/// roughly 2 MiB (two 1 MiB buffers), so allocate it on the heap.
///
/// @note Spec `metrics/metrics-daemon`, requirement "Daemon renders Prometheus
///       text exposition" and requirement "Daemon serves GET /metrics over
///       HTTP".
struct alignas(64) ExpositionBuffer {
    /// Bytes available in each of the two exposition buffers (1 MiB).
    static constexpr std::size_t capacity = 1U << 20; // 1 MiB per buffer

    /// The two render buffers; only the unpublished one is written.
    char buffers[2][capacity]{};
    /// Rendered byte length of each buffer, valid once published.
    std::size_t lengths[2]{0, 0};
    /// Index of the readable buffer, or `-1` before the first render.
    std::atomic<int> published{-1};

    /// @brief Render every aggregated record into the inactive buffer and publish it.
    ///
    /// Chooses the buffer opposite the currently published one, renders each
    /// frame with `render_prometheus`, stores the resulting length and publishes
    /// the index with release ordering. A record whose lines do not fit is
    /// skipped by the renderer.
    ///
    /// @param aggregator Single-writer aggregation table to drain; it must not be
    ///        mutated concurrently.
    /// @warning Runs only on the drain thread; concurrent calls would write the
    ///          same inactive buffer.
    /// @note Spec `metrics/metrics-daemon`, requirement "Daemon renders
    ///       Prometheus text exposition".
    void render(const MetricAggregator& aggregator) noexcept {
        const int last = published.load(std::memory_order_relaxed);
        const int next = (last < 0) ? 0 : (1 - last);
        std::size_t used = 0;
        aggregator.for_each([&](const MetricFrame& frame) {
            used = render_prometheus(frame, buffers[next], used, capacity);
        });
        lengths[next] = used;
        published.store(next, std::memory_order_release);
    }

    /// @brief Read the currently published exposition text.
    ///
    /// @return View over the published buffer, empty before the first render.
    ///         The view stays valid until the drain thread publishes the next
    ///         snapshot, so the HTTP thread must finish with it before the next
    ///         render completes.
    /// @note Spec `metrics/metrics-daemon`, requirement "Daemon serves GET
    ///       /metrics over HTTP".
    [[nodiscard]] std::string_view current() const noexcept {
        const int index = published.load(std::memory_order_acquire);
        if (index < 0) {
            return {};
        }
        return std::string_view(buffers[index], lengths[index]);
    }
};

} // namespace nexigen::metrics::detail
