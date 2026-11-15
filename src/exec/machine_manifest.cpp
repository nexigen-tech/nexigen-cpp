// nexigen::exec — Machine Manifest model and reference resolution
// (change p3-arxml-machine-manifest).
#include <nexigen/exec/machine_manifest.hpp>

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::exec {

namespace {

[[nodiscard]] bool duplicate_machine(const std::vector<Machine>& machines,
                                     std::size_t index) noexcept {
    for (std::size_t i = 0; i < index; ++i) {
        if (machines[i].name == machines[index].name) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool duplicate_function_group(const Machine& machine, std::size_t index) noexcept {
    for (std::size_t i = 0; i < index; ++i) {
        if (machine.function_groups[i].name == machine.function_groups[index].name ||
            machine.function_groups[i].id == machine.function_groups[index].id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool duplicate_process(const Machine& machine, std::size_t index) noexcept {
    for (std::size_t i = 0; i < index; ++i) {
        if (machine.processes[i].name == machine.processes[index].name) {
            return true;
        }
    }
    return false;
}

/// Split `/Machine/FunctionGroups/<target>` into machine and target.
[[nodiscard]] bool split_reference(std::string_view reference, std::string& machine,
                                   std::string& target) noexcept {
    if (reference.empty() || reference.front() != '/') {
        return false;
    }
    reference.remove_prefix(1);
    const std::size_t machine_end = reference.find('/');
    if (machine_end == std::string_view::npos) {
        return false;
    }
    machine = std::string(reference.substr(0, machine_end));
    reference.remove_prefix(machine_end + 1);
    const std::size_t group_end = reference.find('/');
    if (group_end == std::string_view::npos) {
        return false;
    }
    if (reference.substr(0, group_end) != "FunctionGroups") {
        return false;
    }
    target = std::string(reference.substr(group_end + 1));
    return !machine.empty() && !target.empty();
}

} // namespace

std::string MachineProcess::to_string() const {
    return std::format("MachineProcess(name={}, executable={}, function_group={}, ref={})", name,
                       executable, function_group, function_group_ref);
}

std::string MachineFunctionGroup::to_string() const {
    return std::format("MachineFunctionGroup(name={}, id={}, states={})", name, id, states.size());
}

const MachineFunctionGroup*
Machine::find_function_group(std::string_view name_or_id) const noexcept {
    for (const MachineFunctionGroup& group : function_groups) {
        if (group.name == name_or_id || group.id == name_or_id) {
            return &group;
        }
    }
    return nullptr;
}

const MachineProcess* Machine::find_process(std::string_view process_name) const noexcept {
    for (const MachineProcess& process : processes) {
        if (process.name == process_name) {
            return &process;
        }
    }
    return nullptr;
}

std::string Machine::to_string() const {
    return std::format("Machine(name={}, function_groups={}, processes={})", name,
                       function_groups.size(), processes.size());
}

const Machine* MachineManifest::find_machine(std::string_view name) const noexcept {
    for (const Machine& machine : machines) {
        if (machine.name == name) {
            return &machine;
        }
    }
    return nullptr;
}

core::Status MachineManifest::resolve_references() noexcept {
    for (Machine& machine : machines) {
        for (MachineProcess& process : machine.processes) {
            std::string referenced_machine;
            std::string target;
            if (!split_reference(process.function_group_ref, referenced_machine, target) ||
                referenced_machine != machine.name) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            const MachineFunctionGroup* group = machine.find_function_group(target);
            if (group == nullptr) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            process.function_group = group->name;
        }
    }
    return {};
}

core::Status MachineManifest::validate() const noexcept {
    if (machines.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    for (std::size_t m = 0; m < machines.size(); ++m) {
        const Machine& machine = machines[m];
        if (machine.name.empty() || duplicate_machine(machines, m)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        for (std::size_t g = 0; g < machine.function_groups.size(); ++g) {
            const MachineFunctionGroup& group = machine.function_groups[g];
            if (group.name.empty() || group.id.empty() || duplicate_function_group(machine, g)) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }
        for (std::size_t p = 0; p < machine.processes.size(); ++p) {
            const MachineProcess& process = machine.processes[p];
            if (process.name.empty() || process.executable.empty() ||
                duplicate_process(machine, p) ||
                machine.find_function_group(process.function_group) == nullptr) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }
    }
    return {};
}

core::Status validate_execution_against_machine(const SuiteManifest& execution,
                                                const MachineManifest& manifest,
                                                std::string_view machine_name) noexcept {
    const Machine* machine = nullptr;
    if (!machine_name.empty()) {
        machine = manifest.find_machine(machine_name);
    } else if (manifest.machines.size() == 1U) {
        machine = &manifest.machines.front();
    }
    if (machine == nullptr) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    for (const AppSpec& app : execution.apps) {
        if (machine->find_function_group(app.function_group) == nullptr) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        const MachineProcess* process = machine->find_process(app.app);
        if (process == nullptr || process->executable != app.command ||
            process->function_group != app.function_group) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
    }
    return {};
}

std::string MachineManifest::to_string() const {
    std::string out = "MachineManifest([";
    for (std::size_t i = 0; i < machines.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += machines[i].to_string();
    }
    out += "])";
    return out;
}

void to_json(nlohmann::json& j, const MachineProcess& value) {
    j = nlohmann::json{{"name", value.name},
                       {"executable", value.executable},
                       {"function_group_ref", value.function_group_ref},
                       {"function_group", value.function_group},
                       {"args", value.args}};
}

void from_json(const nlohmann::json& j, MachineProcess& value) noexcept {
    static_cast<void>(core::detail::read_string(j, "name", value.name));
    static_cast<void>(core::detail::read_string(j, "executable", value.executable));
    static_cast<void>(core::detail::read_string(j, "function_group_ref", value.function_group_ref));
    static_cast<void>(core::detail::read_string(j, "function_group", value.function_group));
    if (const auto it = j.find("args"); it != j.end() && it->is_array()) {
        std::vector<std::string> args;
        for (const auto& item : *it) {
            if (item.is_string()) {
                args.push_back(item.get<std::string>());
            }
        }
        value.args = std::move(args);
    }
}

void to_json(nlohmann::json& j, const MachineFunctionGroup& value) {
    j = nlohmann::json{{"name", value.name}, {"id", value.id}, {"states", value.states}};
}

void from_json(const nlohmann::json& j, MachineFunctionGroup& value) noexcept {
    static_cast<void>(core::detail::read_string(j, "name", value.name));
    static_cast<void>(core::detail::read_string(j, "id", value.id));
    if (const auto it = j.find("states"); it != j.end() && it->is_array()) {
        std::vector<std::string> states;
        for (const auto& item : *it) {
            if (item.is_string()) {
                states.push_back(item.get<std::string>());
            }
        }
        value.states = std::move(states);
    }
}

void to_json(nlohmann::json& j, const Machine& value) {
    nlohmann::json groups = nlohmann::json::array();
    for (const MachineFunctionGroup& group : value.function_groups) {
        nlohmann::json entry;
        to_json(entry, group);
        groups.push_back(std::move(entry));
    }
    nlohmann::json processes = nlohmann::json::array();
    for (const MachineProcess& process : value.processes) {
        nlohmann::json entry;
        to_json(entry, process);
        processes.push_back(std::move(entry));
    }
    j = nlohmann::json{{"name", value.name},
                       {"function_groups", std::move(groups)},
                       {"processes", std::move(processes)}};
}

void from_json(const nlohmann::json& j, Machine& value) noexcept {
    static_cast<void>(core::detail::read_string(j, "name", value.name));
    if (const auto it = j.find("function_groups"); it != j.end() && it->is_array()) {
        std::vector<MachineFunctionGroup> groups;
        for (const auto& entry : *it) {
            MachineFunctionGroup group;
            from_json(entry, group);
            groups.push_back(std::move(group));
        }
        value.function_groups = std::move(groups);
    }
    if (const auto it = j.find("processes"); it != j.end() && it->is_array()) {
        std::vector<MachineProcess> processes;
        for (const auto& entry : *it) {
            MachineProcess process;
            from_json(entry, process);
            processes.push_back(std::move(process));
        }
        value.processes = std::move(processes);
    }
}

void to_json(nlohmann::json& j, const MachineManifest& value) {
    nlohmann::json machines = nlohmann::json::array();
    for (const Machine& machine : value.machines) {
        nlohmann::json entry;
        to_json(entry, machine);
        machines.push_back(std::move(entry));
    }
    j = nlohmann::json{{"machines", std::move(machines)}};
}

void from_json(const nlohmann::json& j, MachineManifest& value) noexcept {
    if (const auto it = j.find("machines"); it != j.end() && it->is_array()) {
        std::vector<Machine> machines;
        for (const auto& entry : *it) {
            Machine machine;
            from_json(entry, machine);
            machines.push_back(std::move(machine));
        }
        value.machines = std::move(machines);
    }
}

} // namespace nexigen::exec
