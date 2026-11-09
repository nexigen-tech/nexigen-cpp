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
    const auto app = j.find("app");
    if (app != j.end() && app->is_string()) {
        child.spec.app = *app->get_ptr<const nlohmann::json::string_t*>();
    }
    const auto instance = j.find("instance");
    if (instance != j.end() && instance->is_string()) {
        child.spec.instance = *instance->get_ptr<const nlohmann::json::string_t*>();
    }
    const auto pid = j.find("pid");
    if (pid != j.end() && pid->is_number_integer()) {
        child.child_pid = static_cast<std::int32_t>(pid->get<std::int64_t>());
    }
    const auto supervisor_pid = j.find("supervisor_pid");
    if (supervisor_pid != j.end() && supervisor_pid->is_number_integer()) {
        child.supervisor_pid = static_cast<std::int32_t>(supervisor_pid->get<std::int64_t>());
    }
    const auto state = j.find("state");
    if (state != j.end() && state->is_string()) {
        const auto* const name = state->get_ptr<const nlohmann::json::string_t*>();
        if (name != nullptr) {
            static_cast<void>(from_string(std::string_view(*name), child.state));
        }
    }
    const auto attempt = j.find("attempt");
    if (attempt != j.end() && attempt->is_number_unsigned()) {
        child.attempt = static_cast<std::uint32_t>(attempt->get<std::uint64_t>());
    }
    const auto restarts_used = j.find("restarts_used");
    if (restarts_used != j.end() && restarts_used->is_number_unsigned()) {
        child.restarts_used = static_cast<std::uint32_t>(restarts_used->get<std::uint64_t>());
    }
}

} // namespace nexigen::exec
