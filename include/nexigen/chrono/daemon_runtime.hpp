// nexigen::chrono — daemon RT and memory-lock bootstrap.
//
// `nexigen-chronod` must apply the deployment's `core::ThreadSpec` before it
// publishes anything: a `Required` attribute failure stops startup and no block
// is created, while a best-effort result is reported as a service flag. The
// same bootstrap attempts `mlockall` best effort.
//
// Keeping this in `chrono` (rather than in the tool's `main`) makes both
// outcomes testable without starting the daemon.
#pragma once

#include <cstdint>

#include <nexigen/chrono/service_flags.hpp>
#include <nexigen/chrono/sync_mode.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>

namespace nexigen::chrono {

/// @brief Inputs of the daemon runtime bootstrap.
struct DaemonRuntimeConfig {
    core::ThreadSpec thread{}; ///< Thread attributes to apply to the caller.
    bool lock_memory{true};    ///< Whether to attempt `mlockall`.
};

/// @brief Observed result of the daemon runtime bootstrap.
struct DaemonRuntimeReport {
    core::ThreadApplyReport thread{}; ///< Per-attribute thread report.
    bool memory_locked{false};        ///< Whether `mlockall` succeeded.
};

/// @brief Apply the thread spec and, best effort, lock the daemon's memory.
///
/// A required attribute failure returns the thread error and no memory lock is
/// attempted; a best-effort failure is visible in the report and the daemon
/// continues with `memory_locked == false`.
///
/// @param config Thread specification and memory-lock request.
/// @param report Destination report; overwritten on every call.
/// @return Success, `InvalidArgument` for an invalid spec, or the attribute
///         failure when the spec is `Required`.
/// @note Cold path only (daemon startup); never called from a publish loop.
[[nodiscard]] core::Status apply_daemon_runtime(const DaemonRuntimeConfig& config,
                                                DaemonRuntimeReport& report) noexcept;

/// @brief Build the runtime service flags from the observed thread report.
///
/// `rt_sched_applied` is set only when a scheduler was requested and the
/// scheduler attribute actually applied; `mlock_applied` only when the memory
/// lock succeeded. A `PtpSynced` policy also advertises the external services
/// the deployment expects.
///
/// @param policy Deployment synchronization policy.
/// @param report Observed runtime report.
/// @return Flags for the publisher.
[[nodiscard]] ServiceFlags runtime_service_flags(SyncMode policy,
                                                 const DaemonRuntimeReport& report) noexcept;

} // namespace nexigen::chrono
