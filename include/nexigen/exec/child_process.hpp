// nexigen::exec — child application lifecycle wrapper
// (change extract-child-process-class; spec execution_manager/app-suite).
//
// COLD PATH ONLY. A `ChildProcess` owns the runtime state of one suite
// application and delegates the actual fork/exec/stop work to `ExecutionManager`, so
// the suite supervisor no longer reaches for `ExecutionManager` directly. Single
// threaded by design: one instance is owned by one supervisor thread.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/app_spec.hpp>
#include <nexigen/exec/app_state.hpp>
#include <nexigen/exec/start_options.hpp>

namespace nexigen::exec {

/// Runtime state plus lifecycle actions for one child application.
///
/// Value type: the supervisor owns a `ChildProcess` per application and
/// mutates the public fields directly (mirroring the former `RuntimeApp`
/// struct). The only behavior it adds is `launch`/`stop`, which wrap the
/// existing `ExecutionManager` so child lifecycle handling lives in one place.
class ChildProcess {
public:
    explicit ChildProcess(AppSpec spec) noexcept;

    /// Start (or restart) the application: delegate to `ExecutionManager::start`, then
    /// read `resource.json` for the child/supervisor PIDs. On failure marks
    /// `state = Failed` and `launch_failed = true`. The caller owns the attempt
    /// counter (it feeds the launch plan).
    [[nodiscard]] core::Status launch(const app::RunIdentity& identity,
                                      const std::vector<std::string>& command,
                                      const StartOptions& options) noexcept;

    /// Stop the application by delegating to `ExecutionManager::stop`. A no-op returning
    /// success when nothing is running.
    [[nodiscard]] core::Status stop(std::uint64_t grace_s = 5) noexcept;

    /// Whether the child is currently running or scheduled to restart.
    [[nodiscard]] bool is_running() const noexcept;

    /// Diagnostic string representation.
    [[nodiscard]] std::string to_string() const;

    // --- Runtime state (public value-type fields) ---

    AppSpec spec{};                          ///< Manifest entry for this app.
    std::string instance_dir{};              ///< Control directory.
    std::string artifact_dir{};              ///< Stream directory for this attempt.
    AppState state{AppState::Unknown};       ///< Lifecycle state.
    std::uint32_t attempt{0};                ///< 1-based attempt counter.
    std::uint32_t restarts_used{0};          ///< Restarts consumed.
    std::uint64_t next_restart_ns{0};        ///< 0 = no retry scheduled.
    std::int32_t child_pid{0};               ///< Child process group leader.
    std::int32_t supervisor_pid{0};          ///< Instance supervisor pid.
    bool exit_consumed{false};               ///< exit.json already handled.
    bool launch_failed{false};               ///< Last launch attempt failed.
    bool dependencies_pending{false};        ///< Waiting for dependencies to report Running.
    std::uint64_t dependency_deadline_ns{0}; ///< Deadline for the wait; 0 = no deadline.
};

[[nodiscard]] inline std::string to_string(const ChildProcess& child) {
    return child.to_string();
}

void to_json(nlohmann::json& j, const ChildProcess& child);
void from_json(const nlohmann::json& j, ChildProcess& child) noexcept;

} // namespace nexigen::exec

namespace std {

template <> struct formatter<nexigen::exec::ChildProcess> : formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::exec::ChildProcess& child, Ctx& ctx) const {
        const std::string text = child.to_string();
        return formatter<std::string_view>::format(text, ctx);
    }
};

} // namespace std
