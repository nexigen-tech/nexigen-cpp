// nexigen::exec — supervisor start-handshake record
// (change refactor-execution_manager-structure; spec execution_manager/process-supervision).
#pragma once

#include <cstdint>

namespace nexigen::exec::detail {

/// @brief Handshake message written by a supervisor before it starts reaping.
///
/// The supervisor writes exactly one record through the start-report pipe after
/// the child's `exec` handshake succeeds (pid positive, error zero); on any
/// setup or `exec` failure it writes a non-zero `core::ErrorCode` and exits
/// self-terminally. The launcher reads the record before publishing a start
/// success, and reaps the supervisor when the record is missing or invalid.
/// Plain bytes, no owning members.
///
/// @note Spec `exec/process-supervision`, requirement "Setup handshake
///       completes only after a successful exec".
/// @note Spec `exec/process-supervision`, requirement "A failed start reaps the
///       supervisor child".
struct StartReport {
    std::int32_t pid{0};   ///< Launched child (or suite supervisor) pid; 0 = failure.
    std::int32_t error{0}; ///< 0 on success, otherwise a `core::ErrorCode` value.
};

} // namespace nexigen::exec::detail
