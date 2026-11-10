// nexigen::sm — cold-path state-management manifest serialization (change add-sm).
#include <nexigen/sm/sm_manifest.hpp>

#include <utility>

namespace nexigen::sm {

namespace {

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

void read_int(const nlohmann::json& j, const char* key, std::int32_t& out) noexcept {
    const auto it = j.find(key);
    if (it != j.end() && it->is_number_integer()) {
        out = it->get<std::int32_t>();
    }
}

void read_bool(const nlohmann::json& j, const char* key, bool& out) noexcept {
    const auto it = j.find(key);
    if (it != j.end() && it->is_boolean()) {
        out = it->get<bool>();
    }
}

} // namespace

std::string AllowedTransition::to_string() const {
    return std::format("AllowedTransition(from={}, to={})", from, to);
}

std::string FunctionGroup::to_string() const {
    return std::format("FunctionGroup(name={}, states={}, transitions={}, transition_max_time_ms={}, "
                       "preparation_timeout_ms={})",
                       name, states.size(), allowed_transitions.size(), transition_max_time_ms,
                       preparation_timeout_ms);
}

std::string RequesterPriority::to_string() const {
    return std::format("RequesterPriority(requester_id={}, priority={})", requester_id, priority);
}

std::string SmManifest::to_string() const {
    std::string out = "SmManifest(function_groups=[";
    for (std::size_t i = 0; i < function_groups.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += function_groups[i].name;
    }
    out += "], metrics_flush_interval_ms=" + std::to_string(metrics_flush_interval_ms) + ")";
    return out;
}

std::int32_t SmManifest::priority_of(std::string_view requester_id) const noexcept {
    for (const RequesterPriority& entry : requester_priorities) {
        if (entry.requester_id == requester_id) {
            return entry.priority;
        }
    }
    return 0;
}

void to_json(nlohmann::json& j, const AllowedTransition& transition) {
    j = nlohmann::json{{"from", transition.from}, {"to", transition.to}};
}

void to_json(nlohmann::json& j, const FunctionGroup& group) {
    auto states = nlohmann::json::array();
    for (const std::string& state : group.states) {
        states.push_back(state);
    }
    auto transitions = nlohmann::json::array();
    for (const AllowedTransition& transition : group.allowed_transitions) {
        nlohmann::json t;
        to_json(t, transition);
        transitions.push_back(std::move(t));
    }
    j = nlohmann::json{{"name", group.name},
                       {"states", std::move(states)},
                       {"allowed_transitions", std::move(transitions)},
                       {"transition_max_time_ms", group.transition_max_time_ms},
                       {"preparation_timeout_ms", group.preparation_timeout_ms},
                       {"application_group", group.application_group}};
}

void to_json(nlohmann::json& j, const RequesterPriority& priority) {
    j = nlohmann::json{{"requester_id", priority.requester_id},
                       {"priority", priority.priority}};
}

void to_json(nlohmann::json& j, const SmManifest& manifest) {
    auto groups = nlohmann::json::array();
    for (const FunctionGroup& group : manifest.function_groups) {
        nlohmann::json g;
        to_json(g, group);
        groups.push_back(std::move(g));
    }
    auto priorities = nlohmann::json::array();
    for (const RequesterPriority& priority : manifest.requester_priorities) {
        nlohmann::json p;
        to_json(p, priority);
        priorities.push_back(std::move(p));
    }
    j = nlohmann::json{{"function_groups", std::move(groups)},
                       {"requester_priorities", std::move(priorities)},
                       {"metrics_flush_interval_ms", manifest.metrics_flush_interval_ms},
                       {"startup_timeout_ms", manifest.startup_timeout_ms}};
}

void from_json(const nlohmann::json& j, AllowedTransition& transition) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_string(j, "from", transition.from);
    read_string(j, "to", transition.to);
}

void from_json(const nlohmann::json& j, FunctionGroup& group) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_string(j, "name", group.name);
    read_uint(j, "transition_max_time_ms", group.transition_max_time_ms);
    read_uint(j, "preparation_timeout_ms", group.preparation_timeout_ms);
    read_bool(j, "application_group", group.application_group);
    const auto states = j.find("states");
    if (states != j.end() && states->is_array()) {
        std::vector<std::string> parsed;
        for (const auto& item : *states) {
            if (!item.is_string()) {
                return;
            }
            parsed.push_back(item.get<std::string>());
        }
        group.states = std::move(parsed);
    }
    const auto transitions = j.find("allowed_transitions");
    if (transitions != j.end() && transitions->is_array()) {
        std::vector<AllowedTransition> parsed;
        for (const auto& item : *transitions) {
            AllowedTransition transition;
            from_json(item, transition);
            parsed.push_back(std::move(transition));
        }
        group.allowed_transitions = std::move(parsed);
    }
}

void from_json(const nlohmann::json& j, RequesterPriority& priority) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_string(j, "requester_id", priority.requester_id);
    read_int(j, "priority", priority.priority);
}

void from_json(const nlohmann::json& j, SmManifest& manifest) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_uint(j, "metrics_flush_interval_ms", manifest.metrics_flush_interval_ms);
    read_uint(j, "startup_timeout_ms", manifest.startup_timeout_ms);
    const auto groups = j.find("function_groups");
    if (groups != j.end() && groups->is_array()) {
        std::vector<FunctionGroup> parsed;
        for (const auto& item : *groups) {
            FunctionGroup group;
            from_json(item, group);
            parsed.push_back(std::move(group));
        }
        manifest.function_groups = std::move(parsed);
    }
    const auto priorities = j.find("requester_priorities");
    if (priorities != j.end() && priorities->is_array()) {
        std::vector<RequesterPriority> parsed;
        for (const auto& item : *priorities) {
            RequesterPriority priority;
            from_json(item, priority);
            parsed.push_back(std::move(priority));
        }
        manifest.requester_priorities = std::move(parsed);
    }
}

} // namespace nexigen::sm
