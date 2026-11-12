// nexigen::chrono — type-erased sink binding (change add-chrono-clock-service;
// spec chrono/chrono-provider). Plain function pointers, no virtual dispatch,
// no RTTI: the provider binds a Sink through these two thunks.
#pragma once

#include <nexigen/chrono/snapshot.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono::detail {

/// @brief Type-erased sink binding built from plain function pointers.
///
/// The provider stores a `void*` self handle plus one thunk per sink operation,
/// so binding a `concepts::Sink` costs no vtable, no RTTI, and no allocation.
/// The handle is a borrowed pointer: the bound sink must outlive the provider.
/// A null `on_snapshot`/`flush` means "no sink" and the provider treats the
/// operation as a successful no-op.
///
/// @note Spec `chrono/provider`, requirement "Provider construction and sink
///       binding".
struct SinkBinding {
    void* self{nullptr}; ///< Opaque sink instance passed to both thunks.
    core::Status (*on_snapshot)(void*, const Snapshot&) noexcept {nullptr}; ///< Delivery thunk.
    core::Status (*flush)(void*) noexcept {nullptr};                        ///< Flush thunk.
};

} // namespace nexigen::chrono::detail
