// nexigen::logging — the consumer-sink contract (change unify-core-concepts;
// spec logging/multi-producer "Consumer sink contract").
//
// The asynchronous worker used to duck-type its sink. It now constrains the
// type with the shared lifecycle concept, using the record type the producer
// carries.
#pragma once

#include <nexigen/core/concepts/sink_concept.hpp>

namespace nexigen::logging::detail {

/// @brief Consumer sink accepted by `AsyncWorker`.
///
/// A model type satisfies `core::concepts::LifecycleSink` over the producer's
/// record type: `start`, `on_event`, `flush`, and `stop`.
///
/// @tparam S Sink type under test.
/// @tparam Record Record type delivered by the worker.
/// @note Spec `logging/multi-producer`, requirement "Consumer sink contract".
template <class S, class Record>
concept RecordSink = core::concepts::LifecycleSink<S, Record>;

} // namespace nexigen::logging::detail
