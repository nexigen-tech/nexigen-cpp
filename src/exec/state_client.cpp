// nexigen::exec — StateClient receiver implementation (change add-sm).
// Built only when NEXIGEN_ENABLE_COM=ON (nexigen_state_client target).
#include <nexigen/exec/state_client.hpp>

namespace nexigen::exec {

com::Result<StateClient> StateClient::create(com::Node& node, const com::ServiceName& name,
                                             std::vector<GroupMapping> mappings,
                                             ExecutionManager& manager,
                                             const app::RunIdentity& identity) noexcept {
    auto server = com::Server<StateCommand, StateAck>::create(node, name);
    if (!server) {
        return std::unexpected(server.error());
    }
    return StateClient{name, std::move(*server), std::move(mappings), &manager, identity};
}

StateClient::StateClient(com::ServiceName name, com::Server<StateCommand, StateAck> server,
                         std::vector<GroupMapping> mappings, ExecutionManager* manager,
                         app::RunIdentity identity) noexcept
    : service_name_(std::move(name)),
      server_(std::move(server)),
      mappings_(std::move(mappings)),
      manager_(manager),
      identity_(std::move(identity)) {}

com::Result<bool> StateClient::serve_once() noexcept {
    auto received = server_.receive();
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
    for (const GroupMapping& mapping : mappings_) {
        if (mapping.function_group != group) {
            continue;
        }
        const bool stopping = is_off_target(command.target_state);
        for (const std::string& app_name : mapping.app_names) {
            const std::string binary = mapping.binary.empty() ? app_name : mapping.binary;
            const core::Status step =
                stopping ? manager_->stop(instance_dir_for(app_name))
                         : manager_->start(identity_, app_name,
                                           std::vector<std::string>{binary});
            if (!step) {
                return false;
            }
        }
        return true;
    }
    return false;  // unknown group -> rejection ack
}

bool StateClient::is_off_target(std::string_view target) noexcept {
    return target == "Off" || target == "Stopped" || target == "Shutdown";
}

std::string StateClient::instance_dir_for(std::string_view app_name) const {
    return identity_.run_dir + "/" + std::string(app_name) + "/" + identity_.instance_id;
}

std::string StateClient::to_string() const {
    return std::format("StateClient(service={}, last_command={})", service_name_.to_string(),
                       last_command_);
}

} // namespace nexigen::exec
