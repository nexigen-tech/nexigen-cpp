// nexigen::logging — thread affinity (CHANGE dedup-affinity-core: the
// implementation moved to nexigen::core::affinity; this header is a
// forward-only shim so every `detail::pin_self`/`detail::self_affinity`
// call site keeps resolving).
//
// Calling convention (from the core header, kept here for convenience):
// callers run these ON THE THREAD TO BE PINNED — the background worker
// pins itself as the first act inside its body, because std::jthread
// creation is asynchronous and pinning from the spawning thread could
// pin the wrong schedule window.
#pragma once

#include <nexigen/core/affinity.hpp>

namespace nexigen::logging::detail {

using nexigen::core::affinity::pin_self;
using nexigen::core::affinity::self_affinity;

} // namespace nexigen::logging::detail
