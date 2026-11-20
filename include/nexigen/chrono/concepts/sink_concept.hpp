// nexigen::chrono — the sink contract (change add-chrono-clock-service;
// spec chrono/chrono-sink; unified onto core concepts by change
// unify-core-concepts).
//
// A sink receives UTC-offset snapshots and can be flushed on demand. Both
// operations are noexcept and allocation-free: the sink is a cold-path
// consumer (background worker thread or an explicit snapshot_now caller),
// but it still must never take a lock or grow a container on the worker's
// runtime loop (mandate 4).
#pragma once

#include <nexigen/chrono/snapshot.hpp>
#include <nexigen/core/concepts/sink_concept.hpp>

namespace nexigen::chrono::concepts {

/// @brief A chrono sink: snapshot delivery plus flush, no virtual dispatch required.
///
/// Satisfied by `NullSink`, `ConsoleSink`, `JsonFileSink`, and any
/// `CompositeSink` over them. The shape is the shared
/// `core::concepts::Sink<T, Snapshot>`.
///
/// @tparam T Candidate sink type; it must expose `on_event(const Snapshot&)`
///         and `flush()`, each returning `core::Status`.
/// @note Spec `chrono/chrono-sink`, requirement "Chrono sink concept";
///       spec `core/sink-concepts`, requirement "Sink delivery contract".
template <class T>
concept Sink = core::concepts::Sink<T, Snapshot>;

} // namespace nexigen::chrono::concepts
