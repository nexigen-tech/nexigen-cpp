// nexigen::exec — Function Group registry implementation (change
// add-em-orchestration).
#include <nexigen/exec/function_group_registry.hpp>

#include <utility>

namespace nexigen::exec {

FunctionGroupRegistry::FunctionGroupRegistry(const SuiteManifest& manifest) : manifest_(&manifest) {
    groups_.reserve(manifest.function_groups.size());
    for (std::size_t i = 0; i < manifest.function_groups.size(); ++i) {
        GroupRuntime runtime;
        runtime.state_index = 0U; // first declared state
        groups_.push_back(std::move(runtime));
    }
    // Bind every application to the groups it declares.
    for (const AppSpec& app : manifest.apps) {
        for (const std::string& group_name : app.function_groups) {
            const std::size_t index = find_group(group_name);
            if (index != static_cast<std::size_t>(-1)) {
                groups_[index].app_names.push_back(app.app);
            }
        }
    }
}

std::size_t FunctionGroupRegistry::group_count() const noexcept {
    return groups_.size();
}

std::string_view FunctionGroupRegistry::group_name(std::size_t index) const noexcept {
    return manifest_->function_groups[index].name;
}

std::string_view FunctionGroupRegistry::group_state(std::size_t index) const noexcept {
    const FunctionGroupConfig& group = manifest_->function_groups[index];
    const std::size_t state_index = groups_[index].state_index;
    return state_index < group.states.size() ? std::string_view(group.states[state_index])
                                             : std::string_view{};
}

const std::vector<std::string>&
FunctionGroupRegistry::bound_apps(std::size_t index) const noexcept {
    return groups_[index].app_names;
}

std::string_view FunctionGroupRegistry::command_of(std::string_view app_name) const noexcept {
    for (const AppSpec& app : manifest_->apps) {
        if (app.app == app_name) {
            return app.command;
        }
    }
    return {};
}

std::size_t FunctionGroupRegistry::find_group(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < manifest_->function_groups.size(); ++i) {
        if (manifest_->function_groups[i].name == name) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

void FunctionGroupRegistry::set_state(std::size_t index, std::string_view state) noexcept {
    const FunctionGroupConfig& group = manifest_->function_groups[index];
    for (std::size_t i = 0; i < group.states.size(); ++i) {
        if (group.states[i] == state) {
            groups_[index].state_index = i;
            return;
        }
    }
}

bool FunctionGroupRegistry::is_off_target(std::string_view target) noexcept {
    return target == "Off" || target == "Stopped" || target == "Shutdown";
}

std::string FunctionGroupRegistry::to_string() const {
    std::string out = "FunctionGroupRegistry([";
    for (std::size_t i = 0; i < groups_.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += std::format("{}={}", manifest_->function_groups[i].name, group_state(i));
    }
    out += "])";
    return out;
}

} // namespace nexigen::exec
