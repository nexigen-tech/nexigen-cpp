// nexigen::runner — supervisor start-handshake record
// (change refactor-runner-structure; spec runner/process-supervision).
#pragma once

#include <cstdint>

namespace nexigen::runner::detail {

/// Handshake message written by a supervisor before it starts reaping.
/// `pid` is the launched child (or the suite supervisor); a non-zero `error`
/// carries a `core::ErrorCode` when startup failed.
struct StartReport {
    std::int32_t pid{0};
    std::int32_t error{0};
};

} // namespace nexigen::runner::detail
