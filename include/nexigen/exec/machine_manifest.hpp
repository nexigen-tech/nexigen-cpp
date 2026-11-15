// nexigen::exec — Machine Manifest model and reference resolution
// (change p3-arxml-machine-manifest; spec exec/machine-manifest).
#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec {

/// @brief One process declared by a Machine Manifest.
struct MachineProcess {
    std::string name{};
    std::string executable{};
    std::string function_group_ref{}; ///< `/Machine/FunctionGroups/<id>`.
    std::string function_group{};     ///< Resolved Function Group name.
    std::vector<std::string> args{};

    [[nodiscard]] std::string to_string() const;
};

/// @brief One Function Group declared by a Machine Manifest.
struct MachineFunctionGroup {
    std::string name{};
    std::string id{};
    std::vector<std::string> states{};

    [[nodiscard]] std::string to_string() const;
};

/// @brief One Machine with its Function Groups and processes.
struct Machine {
    std::string name{};
    std::vector<MachineFunctionGroup> function_groups{};
    std::vector<MachineProcess> processes{};

    [[nodiscard]] const MachineFunctionGroup*
    find_function_group(std::string_view name_or_id) const noexcept;
    [[nodiscard]] const MachineProcess* find_process(std::string_view name) const noexcept;
    [[nodiscard]] std::string to_string() const;
};

/// @brief Parsed Machine Manifest.
struct MachineManifest {
    std::vector<Machine> machines{};

    [[nodiscard]] const Machine* find_machine(std::string_view name) const noexcept;

    /// @brief Resolve every `/Machine/FunctionGroups/<id>` reference.
    [[nodiscard]] core::Status resolve_references() noexcept;

    /// @brief Validate names, ids, executables, and resolved references.
    [[nodiscard]] core::Status validate() const noexcept;

    [[nodiscard]] std::string to_string() const;
};

/// @brief Cross-check an Execution Manifest against a Machine Manifest.
///
/// Every application must map to a machine process with the same name, command,
/// and Function Group.
[[nodiscard]] core::Status
validate_execution_against_machine(const SuiteManifest& execution, const MachineManifest& machine,
                                   std::string_view machine_name = {}) noexcept;

[[nodiscard]] inline std::string to_string(const MachineProcess& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const MachineFunctionGroup& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const Machine& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const MachineManifest& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const MachineProcess& value);
void from_json(const nlohmann::json& j, MachineProcess& value) noexcept;
void to_json(nlohmann::json& j, const MachineFunctionGroup& value);
void from_json(const nlohmann::json& j, MachineFunctionGroup& value) noexcept;
void to_json(nlohmann::json& j, const Machine& value);
void from_json(const nlohmann::json& j, Machine& value) noexcept;
void to_json(nlohmann::json& j, const MachineManifest& value);
void from_json(const nlohmann::json& j, MachineManifest& value) noexcept;

} // namespace nexigen::exec

template <>
struct std::formatter<nexigen::exec::MachineProcess> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::exec::MachineProcess& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};
template <>
struct std::formatter<nexigen::exec::MachineFunctionGroup> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::MachineFunctionGroup& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};
template <> struct std::formatter<nexigen::exec::Machine> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::exec::Machine& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};
template <>
struct std::formatter<nexigen::exec::MachineManifest> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::exec::MachineManifest& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};
