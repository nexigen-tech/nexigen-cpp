// nexigen::chrono — the sink contract (change add-chrono-clock-service;
// spec chrono/chrono-sink).
//
// A sink receives UTC-offset snapshots and can be flushed on demand. Both
// operations are noexcept and allocation-free: the sink is a cold-path
// consumer (background worker thread or an explicit snapshot_now caller),
// but it still must never take a lock or grow a container on the worker's
// runtime loop (mandate 4).
#pragma once

#include <concepts>

#include <nexigen/chrono/snapshot.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono::concepts {

/// @brief A chrono sink: snapshot delivery plus flush, no virtual dispatch required.
///
/// Satisfied by `NullSink`, `ConsoleSink`, `JsonFileSink`, and any
/// `CompositeSink` over them. Both operations return `core::Status`, must be
/// noexcept, and must be allocation-free on the worker's runtime loop: a sink
/// is a cold-path consumer, but it may not take a lock or grow a container
/// while delivering.
///
/// @tparam T Candidate sink type; it must expose `on_snapshot(const Snapshot&)`
///         and `flush()`, each returning `core::Status`.
/// @note Spec `chrono/chrono-sink`, requirement "Chrono sink concept";
///       project mandate 4 (no allocation on the runtime loop).
template <class T>
concept Sink = requires(T& sink, const Snapshot& snapshot) {
    { sink.on_snapshot(snapshot) } -> std::same_as<core::Status>;
    { sink.flush() } -> std::same_as<core::Status>;
};

} // namespace nexigen::chrono::concepts
