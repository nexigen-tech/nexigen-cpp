// nexigen::exec — Function Group registry implementation (change
// add-em-orchestration).
#include <nexigen/exec/function_group_registry.hpp>

#include <utility>

#include <nexigen/exec/machine_function_group.hpp>

namespace nexigen::exec {

FunctionGroupRegistry::FunctionGroupRegistry(const SuiteManifest& manifest) : manifest_(&manifest) {
    groups_.reserve(manifest.function_groups.size());
    for (std::size_t i = 0; i < manifest.function_groups.size(); ++i) {
        GroupRuntime runtime;
        runtime.state_index = 0U; // first declared state
        runtime.apps_by_state.resize(manifest.function_groups[i].states.size());
        groups_.push_back(std::move(runtime));
    }
    // Bind every application to the states of its single Function Group.
    for (const AppSpec& app : manifest.apps) {
        const std::size_t index = find_group(app.function_group);
        if (index == static_cast<std::size_t>(-1)) {
            continue;
        }
        groups_[index].app_names.push_back(app.app);
        for (const StateDependentStartupConfig& config : app.startup_configs) {
            for (std::size_t state_index = 0;
                 state_index < manifest.function_groups[index].states.size(); ++state_index) {
                if (manifest.function_groups[index].states[state_index] == config.state) {
                    groups_[index].apps_by_state[state_index].push_back(app.app);
                }
            }
        }
    }
}

std::size_t FunctionGroupRegistry::group_count() const noexcept {
    return groups_.size();
}

bool FunctionGroupRegistry::is_machine_group(std::size_t index) const noexcept {
    return is_machine_function_group(manifest_->function_groups[index].name);
}

std::string_view FunctionGroupRegistry::group_name(std::size_t index) const noexcept {
    return manifest_->function_groups[index].name;
}

std::string_view FunctionGroupRegistry::group_state(std::size_t index) const noexcept {
    if (groups_[index].undefined) {
        return "Undefined";
    }
    const FunctionGroupConfig& group = manifest_->function_groups[index];
    const std::size_t state_index = groups_[index].state_index;
    return state_index < group.states.size() ? std::string_view(group.states[state_index])
                                             : std::string_view{};
}

bool FunctionGroupRegistry::has_state(std::size_t index, std::string_view state) const noexcept {
    for (const std::string& candidate : manifest_->function_groups[index].states) {
        if (candidate == state) {
            return true;
        }
    }
    return false;
}

void FunctionGroupRegistry::set_undefined(std::size_t index) noexcept {
    groups_[index].undefined = true;
}

void FunctionGroupRegistry::reset_state(std::size_t index) noexcept {
    groups_[index].state_index = 0U;
    groups_[index].undefined = false;
}

const std::vector<std::string>&
FunctionGroupRegistry::bound_apps(std::size_t index) const noexcept {
    return groups_[index].app_names;
}

const std::vector<std::string>&
FunctionGroupRegistry::apps_in_state(std::size_t index, std::string_view state) const noexcept {
    const FunctionGroupConfig& group = manifest_->function_groups[index];
    for (std::size_t state_index = 0; state_index < group.states.size(); ++state_index) {
        if (group.states[state_index] == state) {
            return groups_[index].apps_by_state[state_index];
        }
    }
    static const std::vector<std::string> empty;
    return empty;
}

const StateDependentStartupConfig*
FunctionGroupRegistry::config_of(std::string_view app_name, std::string_view state) const noexcept {
    for (const AppSpec& app : manifest_->apps) {
        if (app.app != app_name) {
            continue;
        }
        for (const StateDependentStartupConfig& config : app.startup_configs) {
            if (config.state == state) {
                return &config;
            }
        }
        return nullptr;
    }
    return nullptr;
}

std::vector<std::string> FunctionGroupRegistry::command_vector(std::string_view app_name,
                                                               std::string_view state) const {
    std::vector<std::string> command;
    const StateDependentStartupConfig* config = config_of(app_name, state);
    for (const AppSpec& app : manifest_->apps) {
        if (app.app == app_name) {
            command.reserve((config == nullptr ? 0U : config->args.size()) + 1U);
            command.push_back(app.command);
            if (config != nullptr) {
                for (const std::string& argument : config->args) {
                    command.push_back(argument);
                }
            }
            break;
        }
    }
    return command;
}

bool FunctionGroupRegistry::reporting_of(std::string_view app_name) const noexcept {
    for (const AppSpec& app : manifest_->apps) {
        if (app.app == app_name) {
            return app.reporting;
        }
    }
    return true;
}

std::string_view FunctionGroupRegistry::command_of(std::string_view app_name) const noexcept {
    for (const AppSpec& app : manifest_->apps) {
        if (app.app == app_name) {
            return app.command;
        }
    }
    return {};
}

std::string_view FunctionGroupRegistry::instance_of(std::string_view app_name) const noexcept {
    for (const AppSpec& app : manifest_->apps) {
        if (app.app == app_name) {
            return app.instance;
        }
    }
    return {};
}

bool FunctionGroupRegistry::is_authorized(std::string_view requester_id,
                                          std::string_view group_name) const noexcept {
    if (manifest_->function_group_port_mapping.empty()) {
        return true;
    }
    for (const FunctionGroupPortMapping& mapping : manifest_->function_group_port_mapping) {
        if (mapping.requester_id == requester_id && mapping.function_group == group_name) {
            return true;
        }
    }
    return false;
}

std::size_t FunctionGroupRegistry::find_group(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < manifest_->function_groups.size(); ++i) {
        if (manifest_->function_groups[i].name == name) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

ProcessType FunctionGroupRegistry::type_of(std::string_view app_name) const noexcept {
    for (const AppSpec& app : manifest_->apps) {
        if (app.app == app_name) {
            return app.type;
        }
    }
    return ProcessType::Daemon;
}

void FunctionGroupRegistry::set_state(std::size_t index, std::string_view state) noexcept {
    const FunctionGroupConfig& group = manifest_->function_groups[index];
    for (std::size_t i = 0; i < group.states.size(); ++i) {
        if (group.states[i] == state) {
            groups_[index].state_index = i;
            groups_[index].undefined = false;
            return;
        }
    }
}

bool FunctionGroupRegistry::is_off_target(std::string_view target) noexcept {
    return target == "Off" || target == "Stopped" || target == "Shutdown" || target == "Update";
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
