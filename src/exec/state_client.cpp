// nexigen::exec — StateClient receiver implementation (change add-sm;
// add-em-orchestration adds Function Groups + SafeState priority).
// Built only when NEXIGEN_ENABLE_COM=ON (nexigen_state_client target).
#include <nexigen/exec/state_client.hpp>

#include <algorithm>
#include <cstdint>

#include <nexigen/exec/exit_metadata.hpp>
#include <nexigen/exec/instance_file_io.hpp>

namespace nexigen::exec {

namespace {

/// Unique application names bound to any Function Group, in manifest order.
[[nodiscard]] std::vector<std::string> bound_app_names(const FunctionGroupRegistry& registry) {
    std::vector<std::string> names;
    for (std::size_t group = 0; group < registry.group_count(); ++group) {
        for (const std::string& app : registry.bound_apps(group)) {
            if (std::find(names.begin(), names.end(), app) == names.end()) {
                names.push_back(app);
            }
        }
    }
    return names;
}

} // namespace

com::Result<StateClient> StateClient::create(com::Node& node, const com::ServiceName& name,
                                             FunctionGroupRegistry& registry,
                                             ExecutionManager& manager,
                                             const app::RunIdentity& identity,
                                             ExecutionStateTracker* tracker) noexcept {
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
    std::vector<StateChannel> channels;
    if (tracker == nullptr) {
        return StateClient{name,      std::move(*server), std::move(*emergency_server),
                           &registry, &manager,           identity,
                           tracker,   std::move(channels)};
    }
    for (const std::string& app : bound_app_names(registry)) {
        const std::string service = application_state_service(app);
        const auto state_name = com::ServiceName::create(service.c_str());
        if (!state_name) {
            return std::unexpected(state_name.error());
        }
        auto listener = com::EventListener::create(node, *state_name);
        if (!listener) {
            return std::unexpected(listener.error());
        }
        // Register after the listener exists so an early activation is queued
        // and drained against a known record, not dropped.
        tracker->register_process(app, registry.type_of(app));
        channels.push_back(StateChannel{app, std::move(*listener)});
    }
    return StateClient{name,      std::move(*server), std::move(*emergency_server),
                       &registry, &manager,           identity,
                       tracker,   std::move(channels)};
}

StateClient::StateClient(com::ServiceName name, com::Server<StateCommand, StateAck> server,
                         com::Server<StateCommand, StateAck> emergency_server,
                         FunctionGroupRegistry* registry, ExecutionManager* manager,
                         app::RunIdentity identity, ExecutionStateTracker* tracker,
                         std::vector<StateChannel> state_channels) noexcept
    : service_name_(std::move(name)), server_(std::move(server)),
      emergency_server_(std::move(emergency_server)), registry_(registry), manager_(manager),
      identity_(std::move(identity)), tracker_(tracker),
      state_channels_(std::move(state_channels)) {}

com::Result<bool> StateClient::serve_once() noexcept {
    // Readiness/completion first: the tracker must see the state channel
    // before a Run command is evaluated.
    const com::Status polled = poll_state_channels();
    if (!polled) {
        return std::unexpected(polled.error());
    }
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

bool StateClient::stop_group(std::size_t index) noexcept {
    for (const std::string& app_name : registry_->bound_apps(index)) {
        const core::Status step = manager_->stop(instance_dir_for(app_name));
        if (!step) {
            return false;
        }
        if (tracker_ != nullptr) {
            tracker_->on_stop(app_name);
        }
        pending_oneshots_.erase(
            std::remove(pending_oneshots_.begin(), pending_oneshots_.end(), app_name),
            pending_oneshots_.end());
    }
    return true;
}

bool StateClient::should_skip_start(std::string_view app_name) const noexcept {
    if (tracker_ == nullptr) {
        return false;
    }
    if (tracker_->ready(app_name) || tracker_->state(app_name) == AppState::Exited ||
        tracker_->state(app_name) == AppState::Failed) {
        return true;
    }
    return std::find(pending_oneshots_.begin(), pending_oneshots_.end(), app_name) !=
           pending_oneshots_.end();
}

bool StateClient::start_group(std::size_t index) noexcept {
    for (const std::string& app_name : registry_->bound_apps(index)) {
        if (tracker_ != nullptr) {
            tracker_->register_process(app_name, registry_->type_of(app_name));
        }
        if (should_skip_start(app_name)) {
            continue;
        }
        const std::string_view command_binary = registry_->command_of(app_name);
        const std::string binary = command_binary.empty() ? app_name : std::string(command_binary);
        if (tracker_ != nullptr) {
            tracker_->on_start(app_name);
        }
        const core::Status step =
            manager_->start(identity_, app_name, std::vector<std::string>{binary});
        if (!step) {
            pending_oneshots_.erase(
                std::remove(pending_oneshots_.begin(), pending_oneshots_.end(), app_name),
                pending_oneshots_.end());
            return false;
        }
        if (tracker_ != nullptr && registry_->type_of(app_name) == ProcessType::Oneshot &&
            std::find(pending_oneshots_.begin(), pending_oneshots_.end(), app_name) ==
                pending_oneshots_.end()) {
            pending_oneshots_.push_back(app_name);
        }
    }
    return true;
}

bool StateClient::execute(const StateCommand& command) noexcept {
    const std::size_t index = registry_->find_group(command.function_group);
    if (index == static_cast<std::size_t>(-1)) {
        return false; // unknown group -> rejection ack
    }
    const bool stopping = FunctionGroupRegistry::is_off_target(command.target_state);
    if (!(stopping ? stop_group(index) : start_group(index))) {
        return false;
    }
    // State-transition confirmation (REQ_EXEC_PROCESS_CONFIRM): a switch to
    // Run is confirmed only after every bound daemon reported readiness and
    // every bound oneshot exited 0.
    if (!stopping && tracker_ != nullptr && command.target_state == std::string_view("Run") &&
        !tracker_->run_confirmed(registry_->bound_apps(index))) {
        return false;
    }
    registry_->set_state(index, command.target_state);
    return true;
}

com::Status StateClient::poll_state_channels() noexcept {
    if (tracker_ == nullptr) {
        return {};
    }
    for (StateChannel& channel : state_channels_) {
        const auto drained = channel.listener.try_wait([this, &channel](com::EventId id) {
            const auto state = static_cast<AppState>(id.as_value());
            static_cast<void>(tracker_->report_execution_state(channel.app, state));
        });
        if (!drained) {
            return std::unexpected(drained.error());
        }
    }
    poll_oneshot_exits();
    return {};
}

void StateClient::poll_oneshot_exits() noexcept {
    if (tracker_ == nullptr || pending_oneshots_.empty()) {
        return;
    }
    for (auto it = pending_oneshots_.begin(); it != pending_oneshots_.end();) {
        const core::Result<nlohmann::json> document =
            read_json_file(instance_dir_for(*it) + "/exit.json");
        if (!document) {
            ++it;
            continue;
        }
        ExitMetadata exit{};
        from_json(*document, exit);
        const int exit_code = exit.exit_code.has_value() ? *exit.exit_code : 1;
        static_cast<void>(tracker_->on_exit(*it, exit_code));
        it = pending_oneshots_.erase(it);
    }
}

void StateClient::stop_all() noexcept {
    if (manager_ == nullptr || registry_ == nullptr) {
        return;
    }
    for (const std::string& app_name : bound_app_names(*registry_)) {
        static_cast<void>(manager_->stop(instance_dir_for(app_name)));
        if (tracker_ != nullptr) {
            tracker_->on_stop(app_name);
        }
    }
    pending_oneshots_.clear();
}

std::string StateClient::instance_dir_for(std::string_view app_name) const {
    return identity_.run_dir + "/" + std::string(app_name) + "/" + identity_.instance_id;
}

std::string StateClient::to_string() const {
    return std::format("StateClient(service={}, last_command={})", service_name_.to_string(),
                       last_command_);
}

} // namespace nexigen::exec
