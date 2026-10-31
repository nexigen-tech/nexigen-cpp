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

#include <nexigen/chrono/chrono_snapshot.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono::concepts {

/// A chrono sink: snapshot delivery + flush, no virtual dispatch required.
template <class T>
concept ChronoSink = requires(T& sink, const Chrono_snapshot& snapshot) {
    { sink.on_snapshot(snapshot) } -> std::same_as<core::Status>;
    { sink.flush() } -> std::same_as<core::Status>;
};

} // namespace nexigen::chrono::concepts
