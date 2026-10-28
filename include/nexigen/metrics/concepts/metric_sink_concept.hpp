// nexigen::metrics — the sink contract (design D13; spec metrics/metric-sinks).
//
// A sink receives harvested batches and can be flushed on demand. Both
// operations are noexcept and allocation-free: the sink is a cold-path
// consumer (background harvester thread or an explicit collect_now caller),
// but it still must never take a lock or grow a container on the worker's
// runtime loop (mandate 4).
#pragma once

#include <concepts>

#include <nexigen/core/result.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>

namespace nexigen::metrics::concepts {

/// A metrics sink: batch delivery + flush, no virtual dispatch required.
template <class T>
concept MetricSink = requires(T& sink, const MetricBatchSnapshot& batch) {
    { sink.on_batch(batch) } -> std::same_as<core::Status>;
    { sink.flush() } -> std::same_as<core::Status>;
};

} // namespace nexigen::metrics::concepts
