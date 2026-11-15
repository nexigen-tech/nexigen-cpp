// nexigen::sm — EM MachineFG lifecycle client
// (change machinefg-as-em-function-group).
#include <nexigen/sm/machine_lifecycle.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#include <nexigen/com/service_name.hpp>
#include <nexigen/exec/machine_function_group.hpp>

namespace nexigen::sm {

namespace {

constexpr std::string_view command_service_name = "Nexigen/EM/StateCommand";
constexpr std::string_view query_service_name = "Nexigen/EM/InitialMachineStateTransitionResult";
constexpr auto retry_interval = std::chrono::milliseconds(10);

[[nodiscard]] std::chrono::milliseconds
remaining_budget(std::chrono::steady_clock::time_point deadline) noexcept {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return std::chrono::milliseconds(1);
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    return remaining.count() == 0 ? std::chrono::milliseconds(1) : remaining;
}

} // namespace

MachineLifecycleClient::MachineLifecycleClient(
    std::string app_name, std::optional<exec::ApplicationStateReporter> reporter,
    com::Client<std::monostate, exec::InitialMachineStateTransitionResultResponse> query,
    com::Client<exec::StateCommand, exec::StateAck> command) noexcept
    : app_name_(std::move(app_name)), reporter_(std::move(reporter)), query_(std::move(query)),
      command_(std::move(command)) {}

com::Result<MachineLifecycleClient> MachineLifecycleClient::create(com::Node& node,
                                                                   std::string app_name) noexcept {
    auto reporter = exec::ApplicationStateReporter::create(node, app_name);
    if (!reporter) {
        return std::unexpected(reporter.error());
    }
    const auto query_name = com::ServiceName::create(query_service_name.data());
    if (!query_name) {
        return std::unexpected(query_name.error());
    }
    auto query =
        com::Client<std::monostate, exec::InitialMachineStateTransitionResultResponse>::create(
            node, *query_name);
    if (!query) {
        return std::unexpected(query.error());
    }
    const auto command_name = com::ServiceName::create(command_service_name.data());
    if (!command_name) {
        return std::unexpected(command_name.error());
    }
    auto command = com::Client<exec::StateCommand, exec::StateAck>::create(node, *command_name);
    if (!command) {
        return std::unexpected(command.error());
    }
    return MachineLifecycleClient{std::move(app_name), std::move(*reporter), std::move(*query),
                                  std::move(*command)};
}

com::Status MachineLifecycleClient::report_ready() noexcept {
    if (!reporter_) {
        return std::unexpected(com::Error::ServiceNotFound);
    }
    return reporter_->report(exec::AppState::Running);
}

com::Result<exec::StateResult>
MachineLifecycleClient::initial_transition_result(std::chrono::milliseconds timeout) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        const auto response = query_.call(std::monostate{}, remaining_budget(deadline));
        if (!response) {
            return std::unexpected(response.error());
        }
        if (response->result != exec::StateResult::InProgress) {
            return response->result;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::unexpected(com::Error::Timeout);
        }
        std::this_thread::sleep_for(retry_interval);
    }
}

com::Result<exec::StateResult>
MachineLifecycleClient::request_state(std::string_view target,
                                      std::chrono::milliseconds timeout) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    exec::StateCommand command{};
    std::snprintf(command.function_group, sizeof(command.function_group), "%s",
                  std::string(exec::k_machine_function_group_name).c_str());
    std::snprintf(command.target_state, sizeof(command.target_state), "%.*s",
                  static_cast<int>(target.size()), target.data());
    std::snprintf(command.requester_id, sizeof(command.requester_id), "%s",
                  app_name_.empty() ? "sm" : app_name_.c_str());
    for (;;) {
        const auto ack = command_.call(command, remaining_budget(deadline));
        if (!ack) {
            return std::unexpected(ack.error());
        }
        if (ack->result == exec::StateResult::Success ||
            ack->result == exec::StateResult::InProgress) {
            if (ack->result == exec::StateResult::Success) {
                return ack->result;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return std::unexpected(com::Error::Timeout);
            }
            std::this_thread::sleep_for(retry_interval);
            continue;
        }
        return ack->result;
    }
}

} // namespace nexigen::sm
