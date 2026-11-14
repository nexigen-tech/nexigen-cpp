// nexigen::exec — Unrecoverable State (change
// introduce-platformfg-process-model; spec exec/process-types).
//
// AUTOSAR Adaptive `Unrecoverable State`: when the Execution Manager hits a
// failure it cannot resolve, it stops acting, terminates every process it
// manages, and gives the project a handling hook (for example emergency
// power-off). Entering is one-way and idempotent.
#pragma once

#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::exec {

/// @brief One-way terminal state of the Execution Manager.
///
/// The manager installs a terminator that stops all managed processes and a
/// project-specific hook, then calls `enter()` on an unresolvable failure. Any
/// later `enter()` is a no-op, so the termination and the project hook run
/// exactly once. Cold path only: entering happens at most once per manager
/// lifetime.
///
/// @note Spec `exec/process-types`, requirement "Unrecoverable State stops all
///       activity".
class UnrecoverableState {
public:
    /// @brief Callable that terminates every managed process.
    using Terminator = std::function<void()>;

    /// @brief Project-specific handling invoked after termination.
    using ProjectHook = std::function<void()>;

    /// @brief Install the process terminator.
    ///
    /// @param terminator Callback that stops every managed process.
    void set_process_terminator(Terminator terminator) { terminator_ = std::move(terminator); }

    /// @brief Install the project-specific handling hook.
    ///
    /// @param hook Callback invoked after all processes are terminated.
    void set_project_hook(ProjectHook hook) { project_hook_ = std::move(hook); }

    /// @brief Enter the terminal state, terminating all processes and running
    ///        the project hook.
    ///
    /// The first call invokes the terminator and then the project hook; later
    /// calls do nothing, so the manager stops acting after the first failure.
    ///
    /// @note Spec `exec/process-types`, requirement "Unrecoverable State stops
    ///       all activity".
    void enter() {
        if (entered_) {
            return;
        }
        entered_ = true;
        if (terminator_) {
            terminator_();
        }
        if (project_hook_) {
            project_hook_();
        }
    }

    /// @brief Whether the manager has entered the terminal state.
    ///
    /// @return `true` after the first `enter()`.
    [[nodiscard]] bool entered() const noexcept { return entered_; }

    /// @brief Whether the manager may still act.
    ///
    /// @return `false` once the terminal state is entered.
    [[nodiscard]] bool active() const noexcept { return !entered_; }

    /// @brief Cold-path diagnostic (mandate 9).
    ///
    /// @return `UnrecoverableState(entered=<bool>)`.
    [[nodiscard]] std::string to_string() const {
        return std::format("UnrecoverableState(entered={})", entered_);
    }

private:
    Terminator terminator_{};    ///< Stops every managed process.
    ProjectHook project_hook_{}; ///< Project-specific handling.
    bool entered_{false};        ///< One-way latch.
};

/// @brief Free helper (mandate 9): mirrors the member.
///
/// @param state State to render.
/// @return The same text as `state.to_string()`.
[[nodiscard]] inline std::string to_string(const UnrecoverableState& state) {
    return state.to_string();
}

} // namespace nexigen::exec
