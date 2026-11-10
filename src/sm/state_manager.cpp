// nexigen::sm — central arbitration engine implementation (change add-sm).
#include <nexigen/sm/state_manager.hpp>

#include <cstdio>
#include <utility>

namespace nexigen::sm {

StateManager::StateManager(SmManifest manifest) : manifest_(std::move(manifest)) {
    groups_.reserve(manifest_.function_groups.size());
    for (std::size_t i = 0; i < manifest_.function_groups.size(); ++i) {
        GroupRuntime runtime;
        runtime.state_index = 0U;  // first declared state
        groups_.push_back(std::move(runtime));
    }
}

std::size_t StateManager::group_count() const noexcept {
    return groups_.size();
}

std::string_view StateManager::group_name(std::size_t index) const noexcept {
    return manifest_.function_groups[index].name;
}

std::string_view StateManager::state(std::size_t index) const noexcept {
    const FunctionGroup& group = manifest_.function_groups[index];
    const std::size_t state_index = groups_[index].state_index;
    return state_index < group.states.size() ? std::string_view(group.states[state_index])
                                             : std::string_view{};
}

std::size_t StateManager::find_state(std::size_t index, std::string_view name) const noexcept {
    const FunctionGroup& group = manifest_.function_groups[index];
    for (std::size_t i = 0; i < group.states.size(); ++i) {
        if (group.states[i] == name) {
            return i;
        }
    }
    return group.states.size();  // not found
}

bool StateManager::is_allowed(std::size_t index, std::string_view from,
                              std::string_view to) const noexcept {
    for (const AllowedTransition& transition :
         manifest_.function_groups[index].allowed_transitions) {
        if (transition.from == from && transition.to == to) {
            return true;
        }
    }
    return false;
}

std::size_t StateManager::find_group_index(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < manifest_.function_groups.size(); ++i) {
        if (manifest_.function_groups[i].name == name) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

bool StateManager::has_state(std::size_t index, std::string_view name) const noexcept {
    return find_state(index, name) < manifest_.function_groups[index].states.size();
}

std::uint64_t StateManager::preparation_timeout_ms(std::size_t index) const noexcept {
    return manifest_.function_groups[index].preparation_timeout_ms;
}

std::uint64_t StateManager::transition_max_time_ms(std::size_t index) const noexcept {
    return manifest_.function_groups[index].transition_max_time_ms;
}

TransitionResult StateManager::request_transition(std::size_t index, std::string_view target,
                                                 std::string_view requester_id) noexcept {
    GroupRuntime& group = groups_[index];
    const std::string_view current = state(index);

    // 1. Transition graph validation (REQ_SM_003).
    if (current == target || !is_allowed(index, current, target)) {
        return TransitionResult::InvalidGraph;
    }
    const std::size_t target_index = find_state(index, target);
    if (target_index >= manifest_.function_groups[index].states.size()) {
        return TransitionResult::InvalidGraph;
    }

    // 2. Arbitration by requester priority (REQ_SM_004).
    const std::int32_t priority = manifest_.priority_of(requester_id);
    if (priority < group.holder_priority) {
        return TransitionResult::RejectedByPriority;
    }

    group.state_index = target_index;
    group.holder_priority = priority;
    std::snprintf(group.holder_requester, sizeof(group.holder_requester), "%s",
                  requester_id.data());
    return TransitionResult::Success;
}

bool StateManager::advance_startup() noexcept {
    if (machine_ != MachineState::Startup) {
        return false;
    }
    machine_ = MachineState::Verify;
    return true;
}

bool StateManager::confirm_startup_step() noexcept {
    if (machine_ != MachineState::Verify) {
        return false;
    }
    machine_ = MachineState::NormalRunning;
    return true;
}

void StateManager::begin_shutdown() noexcept {
    for (std::size_t i = 0; i < manifest_.function_groups.size(); ++i) {
        if (manifest_.function_groups[i].application_group) {
            force_state(i, "Off");
        }
    }
    machine_ = MachineState::Shutdown;
}

void StateManager::force_state(std::size_t index, std::string_view target) noexcept {
    const std::size_t target_index = find_state(index, target);
    if (target_index < manifest_.function_groups[index].states.size()) {
        groups_[index].state_index = target_index;
    }
}

std::string StateManager::to_string() const {
    std::string out = std::format("StateManager(machine={}, groups=[",
                                  nexigen::sm::to_string(machine_));
    for (std::size_t i = 0; i < groups_.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += std::format("{}={}", manifest_.function_groups[i].name, state(i));
    }
    out += "])";
    return out;
}

} // namespace nexigen::sm
