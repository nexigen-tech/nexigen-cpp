// nexigen::exec — StateDependentStartupConfig serialization and diagnostics
// (change align-exec-process-model-with-autosar; spec exec/app-suite).
#include <format>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/exec/state_dependent_startup_config.hpp>

namespace nexigen::exec {

namespace {

[[nodiscard]] nlohmann::json
env_document(const std::vector<std::pair<std::string, std::string>>& env) {
    nlohmann::json object = nlohmann::json::object();
    for (const auto& [key, value] : env) {
        object[key] = value;
    }
    return object;
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>>
parse_env(const nlohmann::json& object) {
    std::vector<std::pair<std::string, std::string>> env;
    if (!object.is_object()) {
        return env;
    }
    env.reserve(object.size());
    for (const auto& [key, value] : object.items()) {
        if (value.is_string()) {
            env.emplace_back(key, value.get<std::string>());
        }
    }
    return env;
}

} // namespace

std::string StateDependentStartupConfig::to_string() const {
    return std::format(
        "StateDependentStartupConfig(state={}, args={}, env={}, startup_timeout_ms={}, "
        "termination_timeout_ms={}, restart_attempts={}, restart_delay_s={}, dependencies={})",
        state, args.size(), env.size(), startup_timeout_ms, termination_timeout_ms,
        restart_attempts, restart_delay_s, dependencies.size());
}

void to_json(nlohmann::json& j, const StateDependentStartupConfig& config) {
    auto dependencies = nlohmann::json::array();
    for (const ExecutionDependency& dependency : config.dependencies) {
        nlohmann::json item;
        to_json(item, dependency);
        dependencies.push_back(std::move(item));
    }
    j = nlohmann::json{{"state", config.state},
                       {"args", config.args},
                       {"env", env_document(config.env)},
                       {"startup_timeout_ms", config.startup_timeout_ms},
                       {"termination_timeout_ms", config.termination_timeout_ms},
                       {"restart_attempts", config.restart_attempts},
                       {"restart_delay_s", config.restart_delay_s},
                       {"dependencies", std::move(dependencies)}};
}

void from_json(const nlohmann::json& j, StateDependentStartupConfig& config) noexcept {
    if (!j.is_object()) {
        return;
    }
    if (const auto state = j.find("state"); state != j.end() && state->is_string()) {
        config.state = state->get<std::string>();
    }
    if (const auto args = j.find("args"); args != j.end() && args->is_array()) {
        std::vector<std::string> parsed;
        parsed.reserve(args->size());
        for (const auto& item : *args) {
            if (!item.is_string()) {
                return;
            }
            parsed.push_back(item.get<std::string>());
        }
        config.args = std::move(parsed);
    }
    if (const auto env = j.find("env"); env != j.end()) {
        config.env = parse_env(*env);
    }
    static_cast<void>(
        core::detail::read_unsigned(j, "startup_timeout_ms", config.startup_timeout_ms));
    static_cast<void>(
        core::detail::read_unsigned(j, "termination_timeout_ms", config.termination_timeout_ms));
    static_cast<void>(core::detail::read_unsigned(j, "restart_attempts", config.restart_attempts));
    static_cast<void>(core::detail::read_unsigned(j, "restart_delay_s", config.restart_delay_s));
    if (const auto deps = j.find("dependencies"); deps != j.end() && deps->is_array()) {
        std::vector<ExecutionDependency> parsed;
        parsed.reserve(deps->size());
        for (const auto& item : *deps) {
            ExecutionDependency dependency;
            from_json(item, dependency);
            parsed.push_back(std::move(dependency));
        }
        config.dependencies = std::move(parsed);
    }
}

} // namespace nexigen::exec
