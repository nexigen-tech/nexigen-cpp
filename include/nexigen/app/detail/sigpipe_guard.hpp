// nexigen::app::detail — process-wide SIGPIPE disposition guard (change
// add-app-runtime-facade; spec app/runtime "Graceful signals and exit codes").
//
// A console application writing to a closed terminal or pipe must not be killed
// by SIGPIPE before its SIGHUP/SIGTERM drain path runs. The guard saves the
// current disposition, installs SIG_IGN for the App lifetime, and restores it
// after telemetry quiescence.
#pragma once

#include <csignal>
#include <cstring>

namespace nexigen::app::detail {

/// @brief Save/ignore/restore the process-wide SIGPIPE disposition.
///
/// The guard is cold path only and non-copyable. `restore()` is idempotent;
/// the destructor restores if the owner did not.
///
/// @note Spec `app/runtime`, requirement "Graceful signals and exit codes".
class SigpipeGuard {
public:
    /// @brief Install `SIG_IGN` for SIGPIPE.
    SigpipeGuard() noexcept {
        struct sigaction ignored{};
        ignored.sa_handler = SIG_IGN;
        sigemptyset(&ignored.sa_mask);
        ignored.sa_flags = 0;
        if (::sigaction(SIGPIPE, &ignored, &previous_) == 0) {
            armed_ = true;
        }
    }

    /// @brief Restore the previous disposition.
    ~SigpipeGuard() { restore(); }

    /// @brief Deleted: the guard owns one process-global disposition slot.
    SigpipeGuard(const SigpipeGuard&) = delete;
    /// @brief Deleted: copying would double-restore the disposition.
    SigpipeGuard& operator=(const SigpipeGuard&) = delete;
    /// @brief Deleted: the saved `sigaction` must stay at a stable address.
    SigpipeGuard(SigpipeGuard&&) = delete;
    /// @brief Deleted: the saved `sigaction` must stay at a stable address.
    SigpipeGuard& operator=(SigpipeGuard&&) = delete;

    /// @brief Whether SIGPIPE was successfully ignored.
    /// @return `true` when the constructor installed `SIG_IGN` and the previous
    ///         disposition is saved for `restore()`.
    [[nodiscard]] bool armed() const noexcept { return armed_; }

    /// @brief Restore the previous disposition; safe to call repeatedly.
    void restore() noexcept {
        if (!armed_) {
            return;
        }
        static_cast<void>(::sigaction(SIGPIPE, &previous_, nullptr));
        armed_ = false;
    }

private:
    struct sigaction previous_{};
    bool armed_{false};
};

} // namespace nexigen::app::detail
