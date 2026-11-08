// nexigen::chrono — type-erased sink binding (change add-chrono-clock-service;
// spec chrono/chrono-provider). Plain function pointers, no virtual dispatch,
// no RTTI: the provider binds a ChronoSink through these two thunks.
#pragma once

#include <nexigen/chrono/snapshot.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono::detail {

/// Type-erased sink binding: plain function pointers, no virtual dispatch.
struct SinkBinding {
    void* self{nullptr};
    core::Status (*on_snapshot)(void*, const Snapshot&) noexcept {nullptr};
    core::Status (*flush)(void*) noexcept {nullptr};
};

} // namespace nexigen::chrono::detail
