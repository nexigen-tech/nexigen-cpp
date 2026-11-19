// nexigen::chrono — daemon RT and memory-lock bootstrap implementation.
#include <nexigen/chrono/daemon_runtime.hpp>

#if defined(__linux__)
#include <sys/mman.h>
#endif

namespace nexigen::chrono {

core::Status apply_daemon_runtime(const DaemonRuntimeConfig& config,
                                  DaemonRuntimeReport& report) noexcept {
    report = DaemonRuntimeReport{};
    const core::Status thread_status = core::apply_thread_spec_self(config.thread, report.thread);
    if (!thread_status) {
        return thread_status;
    }
    if (!config.lock_memory) {
        return {};
    }
#if defined(__linux__)
    report.memory_locked = ::mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
#endif
    return {};
}

ServiceFlags runtime_service_flags(SyncMode policy, const DaemonRuntimeReport& report) noexcept {
    ServiceFlags flags{};
    const std::uint32_t scheduler_mask = core::ThreadApplyReport::scheduler_mask;
    if ((report.thread.requested() & scheduler_mask) != 0U &&
        (report.thread.failed() & scheduler_mask) == 0U) {
        flags.set(ServiceFlag::RtSchedApplied);
    }
    if (report.memory_locked) {
        flags.set(ServiceFlag::MlockApplied);
    }
    if (policy == SyncMode::PtpSynced) {
        flags.set(ServiceFlag::Ptp4lRequired);
        flags.set(ServiceFlag::Phc2sysRequired);
    }
    return flags;
}

} // namespace nexigen::chrono
