// nexigen::metrics — type-erased sink binding (design D6; spec
// metrics/metric-provider).
#pragma once

#include <cstdint>

#include <nexigen/core/result.hpp>
#include <nexigen/metrics/metric_batch_snapshot.hpp>

namespace nexigen::metrics::detail {

/// Type-erased sink binding: plain function pointers, no virtual dispatch.
struct SinkBinding {
    void* self{nullptr};
    core::Status (*on_batch)(void*, const MetricBatchSnapshot&) noexcept {nullptr};
    core::Status (*flush)(void*) noexcept {nullptr};
};

} // namespace nexigen::metrics::detail
