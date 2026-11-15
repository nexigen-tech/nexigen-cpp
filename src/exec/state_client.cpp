// nexigen::exec — StateClient receiver implementation (change add-sm;
// add-em-orchestration adds Function Groups + SafeState priority).
// Built only when NEXIGEN_ENABLE_COM=ON (nexigen_state_client target).
#include <nexigen/exec/state_client.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include <nexigen/exec/detail/sys_helpers.hpp>
#include <nexigen/exec/execution_manager_logging.hpp>
#include <nexigen/exec/exit_metadata.hpp>
#include <nexigen/exec/instance_file_io.hpp>
#include <nexigen/exec/machine_function_group.hpp>

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

[[nodiscard]] bool configs_equal(const StateDependentStartupConfig* left,
                                 const StateDependentStartupConfig* right) noexcept {
    if (left == right) {
        return true;
    }
    if (left == nullptr || right == nullptr) {
        return false;
    }
    return left->args == right->args && left->env == right->env &&
           left->startup_timeout_ms == right->startup_timeout_ms &&
           left->termination_timeout_ms == right->termination_timeout_ms &&
           left->restart_attempts == right->restart_attempts &&
           left->restart_delay_s == right->restart_delay_s;
}

void collect_termination_order(const FunctionGroupRegistry& registry,
                               const std::vector<std::string>& bound, std::string_view state,
                               std::string_view app, std::vector<std::string>& visited,
                               std::vector<std::string>& order) {
    if (std::find(visited.begin(), visited.end(), app) != visited.end()) {
        return;
    }
    visited.emplace_back(app);
    if (const StateDependentStartupConfig* config = registry.config_of(app, state);
        config != nullptr) {
        for (const ExecutionDependency& dependency : config->dependencies) {
            if (std::find(bound.begin(), bound.end(), dependency.process) != bound.end()) {
                collect_termination_order(registry, bound, state, dependency.process, visited,
                                          order);
            }
        }
    }
    order.emplace_back(app);
}

[[nodiscard]] std::uint64_t max_startup_timeout_ms(const FunctionGroupRegistry& registry,
                                                   const std::vector<std::string>& apps,
                                                   std::string_view state) noexcept {
    std::uint64_t max_ms = 0;
    for (const std::string& app : apps) {
        if (const StateDependentStartupConfig* config = registry.config_of(app, state);
            config != nullptr && config->startup_timeout_ms > max_ms) {
            max_ms = config->startup_timeout_ms;
        }
    }
    return max_ms == 0U ? 5000U : max_ms;
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
        tracker->register_process(app, registry.type_of(app), registry.reporting_of(app));
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
      state_channels_(std::move(state_channels)) {
    if (registry_ != nullptr) {
        transitions_.resize(registry_->group_count());
        last_request_ids_.assign(registry_->group_count(), 0U);
    }
}

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
    const StateResult result = execute(command);
    const bool accepted = result == StateResult::Success || result == StateResult::InProgress;
    last_command_ = command.to_string();
    auto sent = (*received)->send_copy(StateAck{accepted ? 1U : 0U, result});
    if (!sent) {
        return std::unexpected(sent.error());
    }
    return true;
}

StateResult StateClient::stop_app(std::size_t index, std::string_view app_name) noexcept {
    const std::string current(registry_->group_state(index));
    std::uint64_t grace_s = 5;
    if (const StateDependentStartupConfig* config = registry_->config_of(app_name, current);
        config != nullptr) {
        grace_s = (config->termination_timeout_ms + 999U) / 1000U;
    }
    pending_exits_.erase(std::remove(pending_exits_.begin(), pending_exits_.end(), app_name),
                         pending_exits_.end());
    if (tracker_ != nullptr) {
        tracker_->on_stop(app_name);
    }
    const core::Status step = stop_handler_ ? stop_handler_(app_name, current)
                                            : manager_->stop(instance_dir_for(app_name), grace_s);
    if (!step) {
        return StateResult::OperationFailed;
    }
    if (tracker_ != nullptr) {
        tracker_->on_reaped(app_name);
    }
    return StateResult::Success;
}

StateResult StateClient::stop_group(std::size_t index) noexcept {
    const std::vector<std::string>& bound = registry_->bound_apps(index);
    const std::string current(registry_->group_state(index));
    std::vector<std::string> visited;
    std::vector<std::string> order;
    for (const std::string& app_name : bound) {
        collect_termination_order(*registry_, bound, current, app_name, visited, order);
    }
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if (const StateResult step = stop_app(index, *it); step != StateResult::Success) {
            return step;
        }
    }
    return StateResult::Success;
}

bool StateClient::should_skip_start(std::string_view app_name) const noexcept {
    if (tracker_ == nullptr) {
        return false;
    }
    if (tracker_->ready(app_name) || tracker_->state(app_name) == AppState::Exited ||
        tracker_->state(app_name) == AppState::Failed) {
        return true;
    }
    return std::find(pending_exits_.begin(), pending_exits_.end(), app_name) !=
           pending_exits_.end();
}

StateResult StateClient::start_app(std::size_t index, std::string_view app_name,
                                   std::string_view state) noexcept {
    const StateDependentStartupConfig* config = registry_->config_of(app_name, state);
    if (tracker_ != nullptr) {
        tracker_->register_process(app_name, registry_->type_of(app_name),
                                   registry_->reporting_of(app_name), config, 1);
    }
    if (should_skip_start(app_name)) {
        return StateResult::Success;
    }
    if (tracker_ != nullptr) {
        tracker_->on_launch(app_name, config, 1);
    }
    core::Status step{};
    if (start_handler_) {
        step = start_handler_(app_name, state);
    } else {
        std::vector<std::string> command = registry_->command_vector(app_name, state);
        if (command.empty()) {
            command.emplace_back(app_name);
        }
        app::RunIdentity start_identity = identity_;
        start_identity.instance_dir = instance_dir_for(app_name);
        step = manager_->start(start_identity, app_name, command);
    }
    if (!step) {
        pending_exits_.erase(std::remove(pending_exits_.begin(), pending_exits_.end(), app_name),
                             pending_exits_.end());
        return StateResult::OperationFailed;
    }
    if (tracker_ != nullptr &&
        std::find(pending_exits_.begin(), pending_exits_.end(), app_name) == pending_exits_.end()) {
        pending_exits_.push_back(std::string(app_name));
    }
    (void)index;
    return StateResult::Success;
}

StateResult StateClient::start_group(std::size_t index, std::string_view state) noexcept {
    for (const std::string& app_name : registry_->apps_in_state(index, state)) {
        if (const StateResult step = start_app(index, app_name, state);
            step != StateResult::Success) {
            return step;
        }
    }
    return StateResult::Success;
}

void StateClient::launch_ready_apps(std::size_t index) noexcept {
    GroupTransition& transition = transitions_[index];
    for (auto it = transition.pending_start.begin(); it != transition.pending_start.end();) {
        bool ready = true;
        if (tracker_ != nullptr) {
            const StateDependentStartupConfig* config =
                registry_->config_of(*it, transition.requested);
            if (config != nullptr) {
                for (const ExecutionDependency& dependency : config->dependencies) {
                    if (tracker_->process_state(dependency.process) != dependency.state) {
                        ready = false;
                        break;
                    }
                }
            }
        }
        if (!ready) {
            ++it;
            continue;
        }
        if (start_app(index, *it, transition.requested) != StateResult::Success) {
            return;
        }
        it = transition.pending_start.erase(it);
    }
}

StateResult StateClient::stop_unneeded(std::size_t index, std::string_view current,
                                       std::string_view target) noexcept {
    std::vector<std::string> current_apps = registry_->apps_in_state(index, current);
    if (current == "Undefined" || current.empty()) {
        current_apps = registry_->bound_apps(index);
    }
    const std::vector<std::string>& target_apps = registry_->apps_in_state(index, target);
    for (const std::string& app_name : current_apps) {
        const bool needed =
            std::find(target_apps.begin(), target_apps.end(), app_name) != target_apps.end();
        const bool changed = !configs_equal(registry_->config_of(app_name, current),
                                            registry_->config_of(app_name, target));
        if ((!needed || changed) && stop_app(index, app_name) != StateResult::Success) {
            return StateResult::OperationFailed;
        }
    }
    return StateResult::Success;
}

StateResult StateClient::begin_transition(std::size_t index, std::string_view target,
                                          std::uint64_t request_id) noexcept {
    GroupTransition& transition = transitions_[index];
    if (transition.active) {
        // A newer request supersedes the in-flight one; an older retry is stale.
        if (request_id != 0U && transition.request_id != 0U && request_id < transition.request_id) {
            return StateResult::OperationCanceled;
        }
        if (transition.requested == target) {
            if (request_id != 0U) {
                transition.request_id = request_id;
            }
            return advance_transition(index);
        }
        if (request_id != 0U && transition.request_id != 0U &&
            request_id == transition.request_id) {
            return StateResult::OperationCanceled;
        }
        log_execution_manager(core::LogLevel::Info,
                              std::format("event=transition_canceled group={} from={} to={}",
                                          registry_->group_name(index), transition.requested,
                                          target));
        const std::string current = transition.requested;
        transition = GroupTransition{}; // A newer request replaces the old one.
        return start_transition(index, target, request_id, current);
    }
    if (request_id != 0U && request_id < last_request_ids_[index]) {
        return StateResult::OperationCanceled;
    }
    return start_transition(index, target, request_id, registry_->group_state(index));
}

StateResult StateClient::start_transition(std::size_t index, std::string_view target,
                                          std::uint64_t request_id,
                                          std::string_view current) noexcept {
    GroupTransition& transition = transitions_[index];
    if (current == target) {
        if (request_id != 0U) {
            last_request_ids_[index] = request_id;
        }
        return StateResult::Success;
    }
    if (!registry_->has_state(index, target)) {
        return StateResult::InvalidTransition;
    }
    if (request_id != 0U) {
        last_request_ids_[index] = request_id;
    }
    if (FunctionGroupRegistry::is_off_target(target)) {
        if (const StateResult stopped = stop_group(index); stopped != StateResult::Success) {
            return stopped;
        }
        registry_->set_state(index, target);
        return StateResult::Success;
    }
    const std::vector<std::string>& target_apps = registry_->apps_in_state(index, target);
    if (const StateResult stopped = stop_unneeded(index, current, target);
        stopped != StateResult::Success) {
        return stopped;
    }
    transition.active = true;
    transition.requested = std::string(target);
    transition.target_apps = target_apps;
    transition.pending_start = target_apps;
    transition.attempts = 1;
    transition.request_id = request_id;
    transition.deadline_ns = detail::monotonic_now_ns() +
                             max_startup_timeout_ms(*registry_, target_apps, target) * 1'000'000ULL;
    launch_ready_apps(index);
    if (tracker_ != nullptr &&
        (!transition.pending_start.empty() || !tracker_->run_confirmed(target_apps))) {
        return StateResult::InProgress;
    }
    registry_->set_state(index, target);
    transition = GroupTransition{};
    return StateResult::Success;
}

StateResult StateClient::advance_transition(std::size_t index) noexcept {
    GroupTransition& transition = transitions_[index];
    if (!transition.active) {
        return StateResult::Success;
    }
    launch_ready_apps(index);
    if (tracker_ == nullptr || tracker_->run_confirmed(transition.target_apps)) {
        registry_->set_state(index, transition.requested);
        transition = GroupTransition{};
        return StateResult::Success;
    }
    for (const std::string& app_name : transition.target_apps) {
        if (tracker_->state(app_name) == AppState::Failed) {
            log_execution_manager(core::LogLevel::Error,
                                  std::format("event=transition_failed group={} state={} app={}",
                                              registry_->group_name(index), transition.requested,
                                              app_name));
            registry_->set_undefined(index);
            transition = GroupTransition{};
            return StateResult::UnexpectedTermination;
        }
    }
    const std::uint64_t now = detail::monotonic_now_ns();
    if (now < transition.deadline_ns) {
        return StateResult::InProgress;
    }
    std::uint32_t restart_budget = 0;
    for (const std::string& app_name : transition.target_apps) {
        if (const StateDependentStartupConfig* config =
                registry_->config_of(app_name, transition.requested);
            config != nullptr && config->restart_attempts > restart_budget) {
            restart_budget = config->restart_attempts;
        }
    }
    if (transition.attempts <= restart_budget) {
        log_execution_manager(core::LogLevel::Warning,
                              std::format("event=transition_timeout group={} state={} attempt={}",
                                          registry_->group_name(index), transition.requested,
                                          transition.attempts));
        for (const std::string& app_name : transition.target_apps) {
            if (!tracker_->ready(app_name) && tracker_->state(app_name) != AppState::Exited) {
                static_cast<void>(stop_app(index, app_name));
                static_cast<void>(start_app(index, app_name, transition.requested));
            }
        }
        ++transition.attempts;
        transition.deadline_ns =
            now + max_startup_timeout_ms(*registry_, transition.target_apps, transition.requested) *
                      1'000'000ULL;
        return StateResult::InProgress;
    }
    log_execution_manager(
        core::LogLevel::Error,
        std::format("event=transition_failed group={} state={} reason=startup_exhausted",
                    registry_->group_name(index), transition.requested));
    registry_->set_undefined(index);
    transition = GroupTransition{};
    return StateResult::OperationFailed;
}

StateResult StateClient::execute(const StateCommand& command) noexcept {
    return execute_impl(command, true);
}

StateResult StateClient::execute_impl(const StateCommand& command, bool enforce_access) noexcept {
    const std::size_t index = registry_->find_group(command.function_group);
    if (index == static_cast<std::size_t>(-1)) {
        return StateResult::InvalidArgument;
    }
    if (enforce_access && !registry_->is_authorized(command.requester_id, command.function_group)) {
        return StateResult::AccessDenied;
    }
    // `MachineFG:Off` is EM-owned (spec exec/machine-function-group): an
    // external SetState never reaches the internal shutdown path.
    if (registry_->is_machine_group(index) &&
        std::string_view(command.target_state) == k_machine_state_off) {
        return StateResult::InvalidTransition;
    }
    if (!registry_->has_state(index, command.target_state)) {
        return StateResult::InvalidTransition;
    }
    return begin_transition(index, command.target_state, command.request_id);
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
    poll_process_exits();
    return {};
}

void StateClient::poll_process_exits() noexcept {
    if (tracker_ == nullptr || pending_exits_.empty()) {
        return;
    }
    for (auto it = pending_exits_.begin(); it != pending_exits_.end();) {
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
        it = pending_exits_.erase(it);
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
    pending_exits_.clear();
}

std::string StateClient::instance_dir_for(std::string_view app_name) const {
    std::string_view instance =
        registry_ == nullptr ? std::string_view{} : registry_->instance_of(app_name);
    if (instance.empty()) {
        instance = identity_.instance_id;
    }
    return identity_.run_dir + "/" + std::string(app_name) + "/" + std::string(instance);
}

StateResult StateClient::request_state(std::string_view group, std::string_view target) noexcept {
    StateCommand command{};
    std::snprintf(command.function_group, sizeof(command.function_group), "%.*s",
                  static_cast<int>(group.size()), group.data());
    std::snprintf(command.target_state, sizeof(command.target_state), "%.*s",
                  static_cast<int>(target.size()), target.data());
    return execute_impl(command, false);
}

std::string_view StateClient::group_state(std::string_view group) const noexcept {
    if (registry_ == nullptr) {
        return {};
    }
    const std::size_t index = registry_->find_group(group);
    if (index == static_cast<std::size_t>(-1) || index >= transitions_.size()) {
        return {};
    }
    return registry_->group_state(index);
}

bool StateClient::transition_active(std::string_view group) const noexcept {
    if (registry_ == nullptr) {
        return false;
    }
    const std::size_t index = registry_->find_group(group);
    return index != static_cast<std::size_t>(-1) && index < transitions_.size() &&
           transitions_[index].active;
}

void StateClient::reset_groups() noexcept {
    if (registry_ != nullptr) {
        for (const std::string& app_name : bound_app_names(*registry_)) {
            if (tracker_ != nullptr) {
                tracker_->on_stop(app_name);
            }
        }
        for (std::size_t index = 0; index < transitions_.size(); ++index) {
            transitions_[index] = GroupTransition{};
            registry_->reset_state(index);
        }
        std::fill(last_request_ids_.begin(), last_request_ids_.end(), 0U);
    }
    pending_exits_.clear();
}

void StateClient::shutdown_machine() noexcept {
    if (registry_ == nullptr) {
        return;
    }
    const std::size_t index = registry_->find_group(k_machine_function_group_name);
    if (index == static_cast<std::size_t>(-1)) {
        return;
    }
    if (index < transitions_.size()) {
        transitions_[index] = GroupTransition{};
    }
    registry_->set_state(index, k_machine_state_off);
}

std::string StateClient::to_string() const {
    return std::format("StateClient(service={}, last_command={})", service_name_.to_string(),
                       last_command_);
}

} // namespace nexigen::exec
