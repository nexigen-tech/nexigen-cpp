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

/// @brief Runtime state plus lifecycle actions for one child application.
///
/// Value type: the supervisor owns a `ChildProcess` per application and
/// mutates the public fields directly (mirroring the former `RuntimeApp`
/// struct). The only behavior it adds is `launch`/`stop`, which wrap the
/// existing `ExecutionManager` so child lifecycle handling lives in one place.
/// Single-threaded: one instance belongs to one supervisor thread; all methods
/// and field reads happen on the cold supervision path.
///
/// @note Spec `exec/app-suite`, requirement "Suite supervisor loop uses an
///       explicit application state machine".
/// @note Spec `exec/app-suite`, requirement "Restarts follow abnormal
///       termination only".
class ChildProcess {
public:
    /// @brief Construct the runtime record for one manifest entry.
    ///
    /// @param spec Manifest entry copied into `ChildProcess::spec`.
    explicit ChildProcess(AppSpec spec) noexcept;

    /// @brief Start (or restart) the application.
    ///
    /// Delegates to `ExecutionManager::start`, then reads `resource.json` for
    /// the child and supervisor pids. On failure marks `state = Failed` and
    /// `launch_failed = true`; on success records the pids and clears
    /// `launch_failed`. The caller owns the attempt counter (it feeds the
    /// launch plan).
    ///
    /// @param identity Run identity for the attempt.
    /// @param command Executable and arguments (already resolved by the launch
    ///        plan).
    /// @param options Stream root, settings sources, log level, environment,
    ///        and CPU set.
    /// @return Success, or the `core::Status` from `ExecutionManager::start`.
    /// @note Spec `exec/app-suite`, requirement "Each attempt writes to a new
    ///       artifact directory".
    /// @note Spec `exec/app-suite`, scenario "A per-application launch failure
    ///       does not fail start".
    [[nodiscard]] core::Status launch(const app::RunIdentity& identity,
                                      const std::vector<std::string>& command,
                                      const StartOptions& options) noexcept;

    /// @brief Stop the application.
    ///
    /// Delegates to `ExecutionManager::stop`. A no-op returning success when
    /// nothing is running (for example when `exit.json` already exists).
    ///
    /// @param grace_s Seconds between SIGTERM and SIGKILL.
    /// @return Success, or the `core::Status` from `ExecutionManager::stop`.
    /// @note Spec `exec/process-supervision`, requirement "Stop delivers SIGTERM
    ///       then SIGKILL on timeout".
    /// @note Spec `exec/app-suite`, scenario "Operator stop does not restart".
    [[nodiscard]] core::Status stop(std::uint64_t grace_s = 5) noexcept;

    /// @brief Whether the child is currently running or scheduled to restart.
    ///
    /// @return `true` for `Running` and `Restarting`, `false` for terminal and
    ///         unknown states.
    [[nodiscard]] bool is_running() const noexcept;

    /// @brief Diagnostic string representation.
    ///
    /// @return Human-readable text naming the application, state, and attempt.
    /// @note Project mandate 9.
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

/// @brief Free helper (mandate 9): mirrors the member.
///
/// @param child Child record to render.
/// @return The same text as `child.to_string()`.
[[nodiscard]] inline std::string to_string(const ChildProcess& child) {
    return child.to_string();
}

/// @brief Serialize a child record into its JSON representation.
///
/// @param j Destination JSON value.
/// @param child Record to serialize.
/// @note Project mandate 10.
void to_json(nlohmann::json& j, const ChildProcess& child);

/// @brief Parse a child record from JSON without throwing.
///
/// Hostile or wrong-typed input leaves `child` unchanged.
///
/// @param j Source JSON value.
/// @param child Target record.
/// @note Project mandate 10.
void from_json(const nlohmann::json& j, ChildProcess& child) noexcept;

} // namespace nexigen::exec

namespace std {

/// @brief The child record formats as its diagnostic string (mandate 11).
template <> struct formatter<nexigen::exec::ChildProcess> : formatter<std::string_view> {
    /// @brief Format the child record through its `to_string()` member.
    ///
    /// @tparam Ctx `std::formatter` context type.
    /// @param child Record to format.
    /// @param ctx Format context receiving the text.
    /// @return The context's output iterator.
    template <class Ctx> auto format(const nexigen::exec::ChildProcess& child, Ctx& ctx) const {
        const std::string text = child.to_string();
        return formatter<std::string_view>::format(text, ctx);
    }
};

} // namespace std
