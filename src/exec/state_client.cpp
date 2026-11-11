// nexigen::exec — StateClient receiver implementation (change add-sm;
// add-em-orchestration adds Function Groups + SafeState priority).
// Built only when NEXIGEN_ENABLE_COM=ON (nexigen_state_client target).
#include <nexigen/exec/state_client.hpp>

namespace nexigen::exec {

com::Result<StateClient> StateClient::create(com::Node& node, const com::ServiceName& name,
                                             FunctionGroupRegistry& registry,
                                             ExecutionManager& manager,
                                             const app::RunIdentity& identity) noexcept {
    auto server = com::Server<StateCommand, StateAck>::create(node, name);
    if (!server) {
        return std::unexpected(server.error());
    }
    const std::string emergency_name = name.to_string() + "Emergency";
    const auto emergency_service = com::ServiceName::create(emergency_name.c_str());
    if (!emergency_service) {
        return std::unexpected(emergency_service.error());
    }
    auto emergency_server = com::Server<StateCommand, StateAck>::create(node, *emergency_service);
    if (!emergency_server) {
        return std::unexpected(emergency_server.error());
    }
    return StateClient{name,      std::move(*server), std::move(*emergency_server),
                       &registry, &manager,           identity};
}

StateClient::StateClient(com::ServiceName name, com::Server<StateCommand, StateAck> server,
                         com::Server<StateCommand, StateAck> emergency_server,
                         FunctionGroupRegistry* registry, ExecutionManager* manager,
                         app::RunIdentity identity) noexcept
    : service_name_(std::move(name)), server_(std::move(server)),
      emergency_server_(std::move(emergency_server)), registry_(registry), manager_(manager),
      identity_(std::move(identity)) {}

com::Result<bool> StateClient::serve_once() noexcept {
    // SafeState priority (REQ_EM_007): the emergency channel is drained first.
    auto emergency = serve_from(emergency_server_);
    if (!emergency) {
        return emergency;
    }
    if (*emergency) {
        return true;
    }
    return serve_from(server_);
}

com::Result<bool> StateClient::serve_from(com::Server<StateCommand, StateAck>& server) noexcept {
    auto received = server.receive();
    if (!received) {
        return std::unexpected(received.error());
    }
    if (!received->has_value()) {
        return false;
    }
    const StateCommand& command = (*received)->payload();
    const std::uint32_t accepted = execute(command) ? 1U : 0U;
    last_command_ = command.to_string();
    auto sent = (*received)->send_copy(StateAck{accepted});
    if (!sent) {
        return std::unexpected(sent.error());
    }
    return true;
}

bool StateClient::execute(const StateCommand& command) noexcept {
    const std::string_view group = command.function_group;
    const std::size_t index = registry_->find_group(group);
    if (index == static_cast<std::size_t>(-1)) {
        return false; // unknown group -> rejection ack
    }
    const bool stopping = FunctionGroupRegistry::is_off_target(command.target_state);
    const std::vector<std::string>& app_names = registry_->bound_apps(index);
    for (const std::string& app_name : app_names) {
        const std::string_view command_binary = registry_->command_of(app_name);
        const std::string binary = command_binary.empty() ? app_name : std::string(command_binary);
        const core::Status step =
            stopping ? manager_->stop(instance_dir_for(app_name))
                     : manager_->start(identity_, app_name, std::vector<std::string>{binary});
        if (!step) {
            return false;
        }
    }
    registry_->set_state(index, command.target_state);
    return true;
}

std::string StateClient::instance_dir_for(std::string_view app_name) const {
    return identity_.run_dir + "/" + std::string(app_name) + "/" + identity_.instance_id;
}

std::string StateClient::to_string() const {
    return std::format("StateClient(service={}, last_command={})", service_name_.to_string(),
                       last_command_);
}

} // namespace nexigen::exec
