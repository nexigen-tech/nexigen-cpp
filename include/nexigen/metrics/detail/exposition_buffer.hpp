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

struct alignas(64) ExpositionBuffer {
    static constexpr std::size_t capacity = 1U << 20; // 1 MiB per buffer

    char buffers[2][capacity]{};
    std::size_t lengths[2]{0, 0};
    std::atomic<int> published{-1};

    /// Render every aggregated record into the inactive buffer and publish it.
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

    /// Current published exposition (empty until the first render).
    [[nodiscard]] std::string_view current() const noexcept {
        const int index = published.load(std::memory_order_acquire);
        if (index < 0) {
            return {};
        }
        return std::string_view(buffers[index], lengths[index]);
    }
};

} // namespace nexigen::metrics::detail
