// nexigen::exec — COM-free control-host boundary for the suite supervisor
// (change unify-exec-suite-control-path).
//
// `nexigen_exec` defines only this interface and the factory slot. The concrete
// COM-enabled host lives in `nexigen_exec_control`; the executable installs its
// factory before forking a suite supervisor, so the child inherits it.
#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/app_state.hpp>
#include <nexigen/exec/process_state.hpp>
#include <nexigen/exec/state_result.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec {

/// @brief Callback that starts/stops one application through the suite host.
///
/// The first argument is the application name, the second the Function Group
/// State that selects the startup configuration. It returns success when the
/// supervised process was launched or stopped.
using ProcessHandler = std::function<core::Status(std::string_view app, std::string_view state)>;

/// @brief Readiness/transition surface shared by the suite supervisor and the
///        COM host.
///
/// The suite supervisor keeps ownership of `ChildProcess`, attempt artifacts,
/// and the restart policy; the control host owns the `StateClient`, the
/// per-application readiness listeners, the process table, and the transition
/// engine. Start/stop handlers are the only boundary between them.
class ControlHost {
public:
    virtual ~ControlHost() = default;

    /// @brief Drain readiness events and state commands.
    virtual void poll() noexcept = 0;

    /// @brief Request one Function Group state.
    ///
    /// @param group Function Group name.
    /// @param state Target state name.
    /// @return Transition result; `InProgress` means retry later.
    [[nodiscard]] virtual StateResult request_state(std::string_view group,
                                                    std::string_view state) noexcept = 0;

    /// @brief Current state of one Function Group (`Undefined` after failure).
    [[nodiscard]] virtual std::string_view group_state(std::string_view group) const noexcept = 0;

    /// @brief Whether a transition is currently running for one group.
    [[nodiscard]] virtual bool transition_active(std::string_view group) const noexcept = 0;

    /// @brief Reset every group to its first declared state and drop pending
    ///        transitions (used before a whole-suite restart).
    virtual void reset_groups() noexcept = 0;

    /// @brief Record the EM-owned `MachineFG` `Off` state at suite shutdown.
    virtual void shutdown_machine() noexcept = 0;

    /// @brief Install the suite process-start callback.
    virtual void set_start_handler(ProcessHandler handler) = 0;

    /// @brief Install the suite process-stop callback.
    virtual void set_stop_handler(ProcessHandler handler) = 0;

    /// @brief Whether an application has reported readiness.
    [[nodiscard]] virtual bool ready(std::string_view app) const noexcept = 0;

    /// @brief Current process state of one application.
    [[nodiscard]] virtual ProcessState process_state(std::string_view app) const noexcept = 0;

    /// @brief Current application state of one application.
    [[nodiscard]] virtual AppState app_state(std::string_view app) const noexcept = 0;

    /// @brief Notify the host that the supervisor launched/restarted a process.
    virtual void on_launched(std::string_view app) noexcept = 0;

    /// @brief Notify the host that the supervisor observed an exit.
    virtual void on_exited(std::string_view app, int exit_code) noexcept = 0;
};

/// @brief Factory installed by the COM-enabled executable.
using ControlHostFactory = std::unique_ptr<ControlHost> (*)(const SuiteManifest&,
                                                            const app::RunIdentity&);

/// @brief Install or clear the process-wide factory.
///
/// @param factory Factory function, or `nullptr` for the gate-OFF fallback.
void set_control_host_factory(ControlHostFactory factory) noexcept;

/// @brief Current process-wide factory, or `nullptr`.
[[nodiscard]] ControlHostFactory control_host_factory() noexcept;

/// @brief Install the COM control factory (defined only in
///        `nexigen_exec_control`).
void install_control_host_factory() noexcept;

} // namespace nexigen::exec
