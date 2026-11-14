// nexigen::exec — ExecutionDependency JSON serialization
// (change align-exec-process-model-with-autosar; spec exec/process-supervision).
#include <nlohmann/json.hpp>

#include <nexigen/exec/execution_dependency.hpp>

namespace nexigen::exec {

void to_json(nlohmann::json& j, const ExecutionDependency& dependency) {
    j = nlohmann::json{{"process", dependency.process}, {"state", dependency.state}};
}

void from_json(const nlohmann::json& j, ExecutionDependency& dependency) noexcept {
    if (!j.is_object()) {
        return;
    }
    if (const auto process = j.find("process"); process != j.end() && process->is_string()) {
        dependency.process = process->get<std::string>();
    }
    if (const auto state = j.find("state"); state != j.end()) {
        from_json(*state, dependency.state);
    }
}

} // namespace nexigen::exec
