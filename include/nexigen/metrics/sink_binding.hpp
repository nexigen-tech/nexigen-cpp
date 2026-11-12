// nexigen::metrics — type-erased sink binding (design D6; spec
// metrics/metric-provider).
#pragma once

#include <cstdint>

#include <nexigen/core/result.hpp>
#include <nexigen/metrics/metric_batch_snapshot.hpp>

namespace nexigen::metrics::detail {

/// @brief Type-erased sink binding: plain function pointers, no virtual dispatch.
///
/// The provider stores one of these instead of a concrete sink type, so the
/// harvest loop can deliver batches without templates or a vtable. `self`
/// points at the caller-owned sink object and must stay valid for the whole
/// binding lifetime; the provider never owns it.
///
/// @note Spec `metrics/metric-sinks`, requirement "Metric sink concept".
/// @note Spec `metrics/metric-provider`, requirement "Provider control plane".
struct SinkBinding {
    void* self{nullptr}; ///< Bound sink object (non-owning).
    /// @brief Deliver one batch snapshot to the bound sink.
    core::Status (*on_batch)(void*, const MetricBatchSnapshot&) noexcept {nullptr};
    core::Status (*flush)(void*) noexcept {nullptr}; ///< Flush pending output.
};

} // namespace nexigen::metrics::detail
