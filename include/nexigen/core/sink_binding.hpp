// nexigen::core — shared type-erasure binding for sink consumers (change
// unify-core-concepts; spec core/sink-concepts "Shared sink binding").
//
// Providers keep the sink type out of their own type: the binding stores plain
// function pointers, which is free on the cold path. A null `on_event` or
// `flush` means "no sink" and is a successful no-op, preserving the unbound
// provider contract.
#pragma once

#include <cstddef>

#include <nexigen/core/result.hpp>

namespace nexigen::core {

/// @brief Borrowed sink handle with function-pointer dispatch.
///
/// The pointed-to sink MUST outlive the binding; the binding never owns it.
/// `start` and `stop` are optional, for lifecycle sinks.
///
/// @tparam Event Record type delivered to the sink.
/// @note Spec `core/sink-concepts`, requirement "Shared sink binding".
template <class Event> struct SinkBinding {
    void* self{nullptr}; ///< Borrowed sink instance.
    /// @brief Deliver one event; null means "no sink".
    Status (*on_event)(void*, const Event&) noexcept {nullptr};
    /// @brief Flush pending output; null means "nothing to flush".
    Status (*flush)(void*) noexcept {nullptr};
    /// @brief Begin delivery; null skips the step.
    Status (*start)(void*) noexcept {nullptr};
    /// @brief Stop delivery; null skips the step.
    void (*stop)(void*) noexcept {nullptr};
};

} // namespace nexigen::core
