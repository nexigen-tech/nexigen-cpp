// nexigen::exec — supervisor start-handshake record
// (change refactor-execution_manager-structure; spec execution_manager/process-supervision).
#pragma once

#include <cstdint>

namespace nexigen::exec::detail {

/// Handshake message written by a supervisor before it starts reaping.
/// `pid` is the launched child (or the suite supervisor); a non-zero `error`
/// carries a `core::ErrorCode` when startup failed.
struct StartReport {
    std::int32_t pid{0};
    std::int32_t error{0};
};

} // namespace nexigen::exec::detail
