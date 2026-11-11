// nexigen::phm — cold-path health-manifest serialization (change add-phm).
#include <nexigen/phm/phm_manifest.hpp>

#include <utility>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::phm {

namespace {

// Identity and supervision parameters of one supervised process. Every field is
// tolerant: a missing or wrong-typed value keeps the destination's default.
void parse_supervised_identity(const nlohmann::json& j, SupervisedProcess& process) noexcept {
    core::detail::read_string(j, "name", process.name);
    core::detail::read_string(j, "service_name", process.service_name);
    core::detail::read_unsigned(j, "alive_reference_cycle_ms", process.alive_reference_cycle_ms);
    core::detail::read_unsigned(j, "alive_min_margin_ms", process.alive_min_margin_ms);
    core::detail::read_unsigned(j, "alive_max_margin_ms", process.alive_max_margin_ms);
    core::detail::read_unsigned(j, "failed_supervision_cycles_tolerance",
                                process.failed_supervision_cycles_tolerance);
    core::detail::read_bool(j, "safety_critical", process.safety_critical);
}

// A present checkpoint array with a non-unsigned (or out-of-range) element
// rejects the whole process, leaving the array untouched.
[[nodiscard]] bool parse_checkpoints(const nlohmann::json& j,
                                     std::vector<std::uint32_t>& out) noexcept {
    return core::detail::collect_array_into<std::uint32_t>(
        j, "checkpoints", out, [](const nlohmann::json& item, std::uint32_t& id) noexcept {
            return core::detail::to_unsigned(item, id);
        });
}

// Checkpoint transitions are tolerant per element: from_json on a non-object
// element leaves the element's defaults in place.
void parse_transitions(const nlohmann::json& j, std::vector<CheckpointTransition>& out) noexcept {
    static_cast<void>(core::detail::collect_array_into<CheckpointTransition>(
        j, "transitions", out,
        [](const nlohmann::json& item, CheckpointTransition& transition) noexcept {
            from_json(item, transition);
            return true;
        }));
}

void parse_deadlines(const nlohmann::json& j, std::vector<DeadlinePair>& out) noexcept {
    static_cast<void>(core::detail::collect_array_into<DeadlinePair>(
        j, "deadlines", out, [](const nlohmann::json& item, DeadlinePair& pair) noexcept {
            from_json(item, pair);
            return true;
        }));
}

void parse_recovery_rules(const nlohmann::json& j, std::vector<RecoveryRule>& out) noexcept {
    static_cast<void>(core::detail::collect_array_into<RecoveryRule>(
        j, "recovery_rules", out, [](const nlohmann::json& item, RecoveryRule& rule) noexcept {
            from_json(item, rule);
            return true;
        }));
}

} // namespace

std::string CheckpointTransition::to_string() const {
    return std::format("CheckpointTransition(from={}, to={})", from_id, to_id);
}

std::string DeadlinePair::to_string() const {
    return std::format("DeadlinePair(source={}, target={}, min_ms={}, max_ms={})", source_id,
                       target_id, min_ms, max_ms);
}

std::string RecoveryRule::to_string() const {
    return std::format("RecoveryRule(action={}, function_group={})",
                       nexigen::phm::to_string(action), function_group);
}

std::string SupervisedProcess::to_string() const {
    return std::format("SupervisedProcess(name={}, service={}, checkpoints={}, transitions={}, "
                       "deadlines={}, rules={}, safety_critical={})",
                       name, service_name, checkpoints.size(), transitions.size(),
                       deadlines.size(), recovery_rules.size(), safety_critical);
}

std::string PhmManifest::to_string() const {
    std::string out = "PhmManifest(processes=[";
    for (std::size_t i = 0; i < processes.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += processes[i].name;
    }
    out += "])";
    return out;
}

void to_json(nlohmann::json& j, const CheckpointTransition& transition) {
    j = nlohmann::json{{"from_id", transition.from_id}, {"to_id", transition.to_id}};
}

void to_json(nlohmann::json& j, const DeadlinePair& pair) {
    j = nlohmann::json{{"source_id", pair.source_id},
                       {"target_id", pair.target_id},
                       {"min_ms", pair.min_ms},
                       {"max_ms", pair.max_ms}};
}

void to_json(nlohmann::json& j, const RecoveryRule& rule) {
    j = nlohmann::json{{"action", nexigen::phm::to_string(rule.action)},
                       {"function_group", rule.function_group}};
}

void to_json(nlohmann::json& j, const SupervisedProcess& process) {
    auto checkpoints = nlohmann::json::array();
    for (const std::uint32_t id : process.checkpoints) {
        checkpoints.push_back(id);
    }
    auto transitions = nlohmann::json::array();
    for (const CheckpointTransition& transition : process.transitions) {
        nlohmann::json t;
        to_json(t, transition);
        transitions.push_back(std::move(t));
    }
    auto deadlines = nlohmann::json::array();
    for (const DeadlinePair& pair : process.deadlines) {
        nlohmann::json p;
        to_json(p, pair);
        deadlines.push_back(std::move(p));
    }
    auto rules = nlohmann::json::array();
    for (const RecoveryRule& rule : process.recovery_rules) {
        nlohmann::json r;
        to_json(r, rule);
        rules.push_back(std::move(r));
    }
    j = nlohmann::json{{"name", process.name},
                       {"service_name", process.service_name},
                       {"alive_reference_cycle_ms", process.alive_reference_cycle_ms},
                       {"alive_min_margin_ms", process.alive_min_margin_ms},
                       {"alive_max_margin_ms", process.alive_max_margin_ms},
                       {"failed_supervision_cycles_tolerance",
                        process.failed_supervision_cycles_tolerance},
                       {"safety_critical", process.safety_critical},
                       {"checkpoints", std::move(checkpoints)},
                       {"transitions", std::move(transitions)},
                       {"deadlines", std::move(deadlines)},
                       {"recovery_rules", std::move(rules)}};
}

void to_json(nlohmann::json& j, const PhmManifest& manifest) {
    auto processes = nlohmann::json::array();
    for (const SupervisedProcess& process : manifest.processes) {
        nlohmann::json p;
        to_json(p, process);
        processes.push_back(std::move(p));
    }
    j = nlohmann::json{{"processes", std::move(processes)}};
}

void from_json(const nlohmann::json& j, CheckpointTransition& transition) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "from_id", transition.from_id);
    core::detail::read_unsigned(j, "to_id", transition.to_id);
}

void from_json(const nlohmann::json& j, DeadlinePair& pair) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "source_id", pair.source_id);
    core::detail::read_unsigned(j, "target_id", pair.target_id);
    core::detail::read_unsigned(j, "min_ms", pair.min_ms);
    core::detail::read_unsigned(j, "max_ms", pair.max_ms);
}

void from_json(const nlohmann::json& j, RecoveryRule& rule) noexcept {
    if (!j.is_object()) {
        return;
    }
    // Non-throwing parse; a miss leaves the current action unchanged.
    std::string action;
    if (core::detail::read_string(j, "action", action)) {
        static_cast<void>(nexigen::phm::from_string(action, rule.action));
    }
    core::detail::read_string(j, "function_group", rule.function_group);
}

void from_json(const nlohmann::json& j, SupervisedProcess& process) noexcept {
    if (!j.is_object()) {
        return;
    }
    parse_supervised_identity(j, process);
    if (!parse_checkpoints(j, process.checkpoints)) {
        return;
    }
    parse_transitions(j, process.transitions);
    parse_deadlines(j, process.deadlines);
    parse_recovery_rules(j, process.recovery_rules);
}

void from_json(const nlohmann::json& j, PhmManifest& manifest) noexcept {
    if (!j.is_object()) {
        return;
    }
    // A non-object element is tolerated: from_json leaves its defaults, which are
    // appended (pre-refactor behavior).
    static_cast<void>(core::detail::collect_array_into<SupervisedProcess>(
        j, "processes", manifest.processes,
        [](const nlohmann::json& item, SupervisedProcess& process) noexcept {
            from_json(item, process);
            return true;
        }));
}

} // namespace nexigen::phm
