// nexigen::phm — cold-path health-manifest serialization (change add-phm).
#include <nexigen/phm/phm_manifest.hpp>

#include <utility>

namespace nexigen::phm {

namespace {

// Reads a field into `out` when it is present and of the right JSON type;
// a hostile/malformed input leaves `out` unchanged (mandate 10).
void read_string(const nlohmann::json& j, const char* key, std::string& out) noexcept {
    const auto it = j.find(key);
    if (it != j.end() && it->is_string()) {
        out = it->get<std::string>();
    }
}

void read_uint(const nlohmann::json& j, const char* key, std::uint64_t& out) noexcept {
    const auto it = j.find(key);
    if (it != j.end() && it->is_number_unsigned()) {
        out = it->get<std::uint64_t>();
    }
}

void read_u32(const nlohmann::json& j, const char* key, std::uint32_t& out) noexcept {
    const auto it = j.find(key);
    if (it != j.end() && it->is_number_unsigned()) {
        out = it->get<std::uint32_t>();
    }
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
    read_u32(j, "from_id", transition.from_id);
    read_u32(j, "to_id", transition.to_id);
}

void from_json(const nlohmann::json& j, DeadlinePair& pair) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_u32(j, "source_id", pair.source_id);
    read_u32(j, "target_id", pair.target_id);
    read_uint(j, "min_ms", pair.min_ms);
    read_uint(j, "max_ms", pair.max_ms);
}

void from_json(const nlohmann::json& j, RecoveryRule& rule) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto action = j.find("action");
    if (action != j.end() && action->is_string()) {
        // Non-throwing parse; a miss leaves the current action unchanged.
        const auto name = action->get<std::string>();
        (void)nexigen::phm::from_string(name, rule.action);
    }
    read_string(j, "function_group", rule.function_group);
}

void from_json(const nlohmann::json& j, SupervisedProcess& process) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_string(j, "name", process.name);
    read_string(j, "service_name", process.service_name);
    read_uint(j, "alive_reference_cycle_ms", process.alive_reference_cycle_ms);
    read_uint(j, "alive_min_margin_ms", process.alive_min_margin_ms);
    read_uint(j, "alive_max_margin_ms", process.alive_max_margin_ms);
    read_uint(j, "failed_supervision_cycles_tolerance",
              process.failed_supervision_cycles_tolerance);
    const auto critical = j.find("safety_critical");
    if (critical != j.end() && critical->is_boolean()) {
        process.safety_critical = critical->get<bool>();
    }
    const auto checkpoints = j.find("checkpoints");
    if (checkpoints != j.end() && checkpoints->is_array()) {
        std::vector<std::uint32_t> parsed;
        for (const auto& item : *checkpoints) {
            if (!item.is_number_unsigned()) {
                return;
            }
            parsed.push_back(item.get<std::uint32_t>());
        }
        process.checkpoints = std::move(parsed);
    }
    const auto transitions = j.find("transitions");
    if (transitions != j.end() && transitions->is_array()) {
        std::vector<CheckpointTransition> parsed;
        for (const auto& item : *transitions) {
            CheckpointTransition transition;
            from_json(item, transition);
            parsed.push_back(transition);
        }
        process.transitions = std::move(parsed);
    }
    const auto deadlines = j.find("deadlines");
    if (deadlines != j.end() && deadlines->is_array()) {
        std::vector<DeadlinePair> parsed;
        for (const auto& item : *deadlines) {
            DeadlinePair pair;
            from_json(item, pair);
            parsed.push_back(pair);
        }
        process.deadlines = std::move(parsed);
    }
    const auto rules = j.find("recovery_rules");
    if (rules != j.end() && rules->is_array()) {
        std::vector<RecoveryRule> parsed;
        for (const auto& item : *rules) {
            RecoveryRule rule;
            from_json(item, rule);
            parsed.push_back(rule);
        }
        process.recovery_rules = std::move(parsed);
    }
}

void from_json(const nlohmann::json& j, PhmManifest& manifest) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto processes = j.find("processes");
    if (processes != j.end() && processes->is_array()) {
        std::vector<SupervisedProcess> parsed;
        for (const auto& item : *processes) {
            SupervisedProcess process;
            from_json(item, process);
            parsed.push_back(std::move(process));
        }
        manifest.processes = std::move(parsed);
    }
}

} // namespace nexigen::phm
