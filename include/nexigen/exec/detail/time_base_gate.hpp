// nexigen::exec — time-base gate primitives (change add-time-base-service;
// spec exec/process-supervision "Time-base loss retry gate").
//
// The suite supervisor asks two questions: is the published base Ready right
// now, and must a retry be held because the application stopped for
// `time_base_lost` while the daemon or the base is not usable. Both live here
// so they can be exercised without launching a suite.
#pragma once

#include <string_view>

namespace nexigen::exec::detail {

/// @brief Whether the published time base reports `Ready`.
///
/// @param segment POSIX shared-memory name; empty is never ready.
/// @return `true` when a block exists and its status is `Ready`.
/// @note Cold path only: attaches and unmaps the segment per call.
[[nodiscard]] bool time_base_ready(std::string_view segment) noexcept;

/// @brief Whether an abnormal exit must be held out of the retry loop.
///
/// A hold applies only to the `time_base_lost` exit code while the daemon is not
/// running or the published base is not `Ready`. The hold does not consume the
/// application's restart budget.
///
/// @param segment POSIX shared-memory name of the base.
/// @param daemon_running Whether the manifest's daemon process is alive.
/// @param exit_code Child exit code.
/// @return `true` when the retry must be held.
/// @note Cold path only.
[[nodiscard]] bool hold_retry_for_time_base(std::string_view segment, bool daemon_running,
                                            int exit_code) noexcept;

} // namespace nexigen::exec::detail
