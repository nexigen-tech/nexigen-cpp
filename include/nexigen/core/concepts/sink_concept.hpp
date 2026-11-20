// nexigen::core — one sink delivery contract for every module that pushes
// records to a consumer (change unify-core-concepts; spec core/sink-concepts).
//
// The event type is supplied by the module, so core owns the shape while the
// domain type stays where it belongs. Sinks are cold-path consumers: both
// callbacks are noexcept and return core::Status.
#pragma once

#include <concepts>

#include <nexigen/core/result.hpp>

namespace nexigen::core::concepts {

/// @brief Structural contract for a consumer of one record type.
///
/// A model type accepts one event through `on_event()` and finishes pending
/// work through `flush()`. No virtual functions are required; modules may
/// consume a sink through templates or a function-pointer binding.
///
/// @tparam S Sink type under test.
/// @tparam Event Record type the sink consumes.
/// @note Spec `core/sink-concepts`, requirement "Sink delivery contract".
template <class S, class Event>
concept Sink = requires(S& sink, const Event& event) {
    { sink.on_event(event) } noexcept -> std::same_as<core::Status>;
    { sink.flush() } noexcept -> std::same_as<core::Status>;
};

/// @brief Sink contract for consumers that own setup and teardown.
///
/// Extends `Sink` with `start()` returning `core::Status` and a plain
/// `stop()`.
///
/// @tparam S Sink type under test.
/// @tparam Event Record type the sink consumes.
/// @note Spec `core/sink-concepts`, requirement "Lifecycle sink contract".
template <class S, class Event>
concept LifecycleSink = Sink<S, Event> && requires(S& sink) {
    { sink.start() } noexcept -> std::same_as<core::Status>;
    { sink.stop() } noexcept;
};

} // namespace nexigen::core::concepts
