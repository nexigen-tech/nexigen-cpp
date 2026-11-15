// nexigen::sm — lightweight declarative state machine
// (change p3-lightweight-state-machine).
#include <nexigen/sm/lightweight_state_machine.hpp>

#include <algorithm>
#include <format>
#include <utility>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::sm {

namespace {

void read_index_array(const nlohmann::json& object, const char* key,
                      std::vector<std::uint32_t>& out) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return;
    }
    std::vector<std::uint32_t> parsed;
    for (const auto& item : *it) {
        if (item.is_number_unsigned()) {
            parsed.push_back(item.get<std::uint32_t>());
        }
    }
    out = std::move(parsed);
}

void read_string_array(const nlohmann::json& object, const char* key,
                       std::vector<std::string>& out) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return;
    }
    std::vector<std::string> parsed;
    for (const auto& item : *it) {
        if (item.is_string()) {
            parsed.push_back(item.get<std::string>());
        }
    }
    out = std::move(parsed);
}

[[nodiscard]] bool states_contain(const std::vector<std::string>& states,
                                  std::string_view name) noexcept {
    return std::find(states.begin(), states.end(), name) != states.end();
}

} // namespace

std::string LsmAction::to_string() const {
    return std::format("LsmAction(type={}, function_group={}, target_state={})",
                       nexigen::sm::to_string(type), function_group, target_state);
}

std::string LsmTransition::to_string() const {
    return std::format("LsmTransition(from={}, to={}, actions={})", from, to, actions.size());
}

std::string LsmErrorRecovery::to_string() const {
    return std::format("LsmErrorRecovery(from={}, fallback_state={}, actions={})", from,
                       fallback_state, actions.size());
}

std::string LightweightStateMachineConfig::to_string() const {
    return std::format("LightweightStateMachineConfig(name={}, states={}, transitions={}, "
                       "actions={}, recovery={})",
                       name, states.size(), transitions.size(), actions.size(),
                       error_recovery.size());
}

[[nodiscard]] bool valid_action_indices(const std::vector<LsmAction>& actions,
                                        const std::vector<std::uint32_t>& indices) noexcept {
    return std::all_of(indices.begin(), indices.end(),
                       [&actions](std::uint32_t index) { return index < actions.size(); });
}

[[nodiscard]] bool has_duplicate_states(const std::vector<std::string>& states) noexcept {
    for (std::size_t i = 0; i < states.size(); ++i) {
        for (std::size_t j = i + 1; j < states.size(); ++j) {
            if (states[i] == states[j]) {
                return true;
            }
        }
    }
    return false;
}

core::Status LightweightStateMachineConfig::validate() const noexcept {
    if (name.empty() || states.empty() || !states_contain(states, initial_state) ||
        has_duplicate_states(states)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    for (const LsmTransition& transition : transitions) {
        if (!states_contain(states, transition.from) || !states_contain(states, transition.to) ||
            !valid_action_indices(actions, transition.actions)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
    }
    for (const LsmErrorRecovery& rule : error_recovery) {
        if (!states_contain(states, rule.from) || !states_contain(states, rule.fallback_state) ||
            !valid_action_indices(actions, rule.actions)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
    }
    return {};
}

LightweightStateMachine::LightweightStateMachine(LightweightStateMachineConfig config,
                                                 LsmActionSink& sink) noexcept
    : config_(std::move(config)), sink_(&sink), current_(config_.initial_state) {}

core::Result<LightweightStateMachine>
LightweightStateMachine::create(LightweightStateMachineConfig config,
                                LsmActionSink& sink) noexcept {
    if (const core::Status valid = config.validate(); !valid) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return LightweightStateMachine(std::move(config), sink);
}

core::Status LightweightStateMachine::run_actions(const std::vector<std::uint32_t>& indices,
                                                  std::string_view requester_id) {
    for (const std::uint32_t index : indices) {
        const LsmAction& action = config_.actions[index];
        switch (action.type) {
            case LsmActionType::None: break;
            case LsmActionType::RequestFunctionGroupState:
                if (const core::Status status = sink_->request_state(
                        action.function_group, action.target_state, requester_id);
                    !status) {
                    return status;
                }
                break;
            case LsmActionType::PublishState:
                if (const core::Status status =
                        sink_->publish_state(action.function_group, action.target_state);
                    !status) {
                    return status;
                }
                break;
        }
    }
    return {};
}

void LightweightStateMachine::recover(std::string_view failed_target,
                                      std::string_view requester_id) {
    for (const LsmErrorRecovery& rule : config_.error_recovery) {
        if (rule.from == failed_target) {
            static_cast<void>(run_actions(rule.actions, requester_id));
            current_ = rule.fallback_state;
            return;
        }
    }
    for (const LsmErrorRecovery& rule : config_.error_recovery) {
        if (rule.from == current_) {
            static_cast<void>(run_actions(rule.actions, requester_id));
            current_ = rule.fallback_state;
            return;
        }
    }
}

core::Result<TransitionResult> LightweightStateMachine::request(std::string_view target,
                                                                std::string_view requester_id) {
    const auto transition =
        std::find_if(config_.transitions.begin(), config_.transitions.end(),
                     [&](const LsmTransition& candidate) {
                         return candidate.from == current_ && candidate.to == target;
                     });
    if (transition == config_.transitions.end()) {
        return TransitionResult::InvalidGraph;
    }
    if (const core::Status actions = run_actions(transition->actions, requester_id); !actions) {
        recover(target, requester_id);
        return std::unexpected(actions.error());
    }
    current_ = std::string(target);
    return TransitionResult::Success;
}

std::string LightweightStateMachine::to_string() const {
    return std::format("LightweightStateMachine(name={}, current={}, states={})", config_.name,
                       current_, config_.states.size());
}

void to_json(nlohmann::json& j, const LsmAction& value) {
    j = nlohmann::json{{"type", nexigen::sm::to_string(value.type)},
                       {"function_group", value.function_group},
                       {"target_state", value.target_state}};
}

void from_json(const nlohmann::json& j, LsmAction& value) noexcept {
    std::string type;
    if (core::detail::read_string(j, "type", type)) {
        LsmActionType parsed = value.type;
        if (from_string(type, parsed)) {
            value.type = parsed;
        }
    }
    static_cast<void>(core::detail::read_string(j, "function_group", value.function_group));
    static_cast<void>(core::detail::read_string(j, "target_state", value.target_state));
}

void to_json(nlohmann::json& j, const LsmTransition& value) {
    j = nlohmann::json{{"from", value.from}, {"to", value.to}, {"actions", value.actions}};
}

void from_json(const nlohmann::json& j, LsmTransition& value) noexcept {
    static_cast<void>(core::detail::read_string(j, "from", value.from));
    static_cast<void>(core::detail::read_string(j, "to", value.to));
    read_index_array(j, "actions", value.actions);
}

void to_json(nlohmann::json& j, const LsmErrorRecovery& value) {
    j = nlohmann::json{
        {"from", value.from}, {"fallback_state", value.fallback_state}, {"actions", value.actions}};
}

void from_json(const nlohmann::json& j, LsmErrorRecovery& value) noexcept {
    static_cast<void>(core::detail::read_string(j, "from", value.from));
    static_cast<void>(core::detail::read_string(j, "fallback_state", value.fallback_state));
    read_index_array(j, "actions", value.actions);
}

void to_json(nlohmann::json& j, const LightweightStateMachineConfig& value) {
    nlohmann::json actions = nlohmann::json::array();
    for (const LsmAction& action : value.actions) {
        nlohmann::json entry;
        to_json(entry, action);
        actions.push_back(std::move(entry));
    }
    nlohmann::json transitions = nlohmann::json::array();
    for (const LsmTransition& transition : value.transitions) {
        nlohmann::json entry;
        to_json(entry, transition);
        transitions.push_back(std::move(entry));
    }
    nlohmann::json recovery = nlohmann::json::array();
    for (const LsmErrorRecovery& rule : value.error_recovery) {
        nlohmann::json entry;
        to_json(entry, rule);
        recovery.push_back(std::move(entry));
    }
    j = nlohmann::json{{"name", value.name},
                       {"states", value.states},
                       {"initial_state", value.initial_state},
                       {"actions", std::move(actions)},
                       {"transitions", std::move(transitions)},
                       {"error_recovery", std::move(recovery)}};
}

void from_json(const nlohmann::json& j, LightweightStateMachineConfig& value) noexcept {
    static_cast<void>(core::detail::read_string(j, "name", value.name));
    read_string_array(j, "states", value.states);
    static_cast<void>(core::detail::read_string(j, "initial_state", value.initial_state));
    if (const auto it = j.find("actions"); it != j.end() && it->is_array()) {
        std::vector<LsmAction> parsed;
        for (const auto& entry : *it) {
            LsmAction action;
            from_json(entry, action);
            parsed.push_back(std::move(action));
        }
        value.actions = std::move(parsed);
    }
    if (const auto it = j.find("transitions"); it != j.end() && it->is_array()) {
        std::vector<LsmTransition> parsed;
        for (const auto& entry : *it) {
            LsmTransition transition;
            from_json(entry, transition);
            parsed.push_back(std::move(transition));
        }
        value.transitions = std::move(parsed);
    }
    if (const auto it = j.find("error_recovery"); it != j.end() && it->is_array()) {
        std::vector<LsmErrorRecovery> parsed;
        for (const auto& entry : *it) {
            LsmErrorRecovery rule;
            from_json(entry, rule);
            parsed.push_back(std::move(rule));
        }
        value.error_recovery = std::move(parsed);
    }
}

} // namespace nexigen::sm
