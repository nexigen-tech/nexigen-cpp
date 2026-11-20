// nexigen::metrics — the sink contract (design D13; spec metrics/metric-sinks;
// unified onto core concepts by change unify-core-concepts).
//
// A sink receives harvested batches and can be flushed on demand. Both
// operations are noexcept and allocation-free: the sink is a cold-path
// consumer (background harvester thread or an explicit collect_now caller),
// but it still must never take a lock or grow a container on the worker's
// runtime loop (mandate 4).
#pragma once

#include <nexigen/core/concepts/sink_concept.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>

namespace nexigen::metrics::concepts {

/// @brief Structural contract for a metrics batch consumer.
///
/// A model type satisfies `core::concepts::Sink<T, MetricBatchSnapshot>`:
/// `on_event(const MetricBatchSnapshot&) noexcept -> core::Status` and
/// `flush() noexcept -> core::Status`. No virtual functions are required.
///
/// @tparam T Sink type under test.
/// @note Spec `metrics/metric-sinks`, requirement "Metric sink concept".
/// @note Spec `core/sink-concepts`, requirement "Sink delivery contract".
template <class T>
concept MetricSink = core::concepts::Sink<T, MetricBatchSnapshot>;

} // namespace nexigen::metrics::concepts
