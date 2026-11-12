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

/// @brief Structural contract for a metrics batch consumer.
///
/// A model type accepts one harvested `MetricBatchSnapshot` through
/// `on_batch()` and finishes pending work through `flush()`; both return
/// `core::Status` and both are `noexcept`. No virtual functions are
/// required, so a sink can be consumed through templates or a plain function
/// pointer. Sinks only run on the cold path (the background harvester thread
/// or an explicit `collect_now()` caller), and neither operation may
/// allocate, grow a container, take a lock, or perform a syscall on the
/// runtime loop (mandate 4).
///
/// @tparam T Sink type under test. It must expose
///         `on_batch(const MetricBatchSnapshot&) noexcept -> core::Status`
///         and `flush() noexcept -> core::Status`; a missing member or a
///         non-`core::Status` return type fails the concept.
/// @note Spec `metrics/metric-sinks`, requirement "Metric sink concept".
/// @note Both callbacks must stay allocation-free and lock-free; a sink is
///       never allowed to grow a container or allocate on the caller's loop.
template <class T>
concept MetricSink = requires(T& sink, const MetricBatchSnapshot& batch) {
    { sink.on_batch(batch) } -> std::same_as<core::Status>;
    { sink.flush() } -> std::same_as<core::Status>;
};

} // namespace nexigen::metrics::concepts
