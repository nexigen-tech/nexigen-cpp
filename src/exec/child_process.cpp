// nexigen::exec — child application lifecycle wrapper
// (change extract-child-process-class; spec execution_manager/app-suite).
//
// COLD PATH ONLY. launch/stop delegate to the existing `ExecutionManager`; the state
// fields here mirror the former `RuntimeApp` struct in suite_supervisor.cpp.
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/detail/json_field.hpp>

#include <nexigen/exec/child_process.hpp>
#include <nexigen/exec/instance_layout.hpp>
#include <nexigen/exec/instance_file_io.hpp>
#include <nexigen/exec/resource_metadata.hpp>
#include <nexigen/exec/execution_manager.hpp>

namespace nexigen::exec {

ChildProcess::ChildProcess(AppSpec app_spec) noexcept : spec(std::move(app_spec)) {}

core::Status ChildProcess::launch(const app::RunIdentity& identity,
                                  const std::vector<std::string>& command,
                                  const StartOptions& options) noexcept {
    ExecutionManager execution_manager;
    const core::Status started =
        execution_manager.start(identity, spec.app, command, spec.idle_timeout_s, options);
    if (!started) {
        state = AppState::Failed;
        child_pid = 0;
        supervisor_pid = 0;
        launch_failed = true;
        exit_consumed = false;
        return started;
    }

    ResourceMetadata resource{};
    if (read_json_into(instance_file_path(instance_dir, "resource.json"), resource)) {
        child_pid = resource.pid;
        supervisor_pid = resource.ppid;
    }
    state = AppState::Running;
    exit_consumed = false;
    launch_failed = false;
    next_restart_ns = 0;
    return {};
}

core::Status ChildProcess::stop(std::uint64_t grace_s) noexcept {
    if (state != AppState::Running && state != AppState::Restarting &&
        child_pid <= 0 && supervisor_pid <= 0) {
        return {};
    }
    ExecutionManager execution_manager;
    return execution_manager.stop(instance_dir, grace_s);
}

bool ChildProcess::is_running() const noexcept {
    return state == AppState::Running || state == AppState::Restarting;
}

std::string ChildProcess::to_string() const {
    return std::format(
        "ChildProcess(app={}, instance={}, pid={}, supervisor_pid={}, state={}, "
        "attempt={}, restarts_used={})",
        spec.app, spec.instance, child_pid, supervisor_pid,
        nexigen::exec::to_string(state), attempt, restarts_used);
}

void to_json(nlohmann::json& j, const ChildProcess& child) {
    j = nlohmann::json{
        {"app", child.spec.app},
        {"instance", child.spec.instance},
        {"pid", child.child_pid},
        {"supervisor_pid", child.supervisor_pid},
        {"state", to_string(child.state)},
        {"attempt", child.attempt},
        {"restarts_used", child.restarts_used},
    };
}

void from_json(const nlohmann::json& j, ChildProcess& child) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_string(j, "app", child.spec.app);
    core::detail::read_string(j, "instance", child.spec.instance);
    core::detail::read_signed(j, "pid", child.child_pid);
    core::detail::read_signed(j, "supervisor_pid", child.supervisor_pid);
    // Non-throwing enum parse; a miss leaves the current state unchanged.
    std::string state;
    if (core::detail::read_string(j, "state", state)) {
        static_cast<void>(from_string(std::string_view(state), child.state));
    }
    core::detail::read_unsigned(j, "attempt", child.attempt);
    core::detail::read_unsigned(j, "restarts_used", child.restarts_used);
}

} // namespace nexigen::exec
