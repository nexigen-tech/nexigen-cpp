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
    std::string name{};               ///< Process name used by the execution manifest.
    std::string executable{};         ///< Executable path or command name.
    std::string function_group_ref{}; ///< `/Machine/FunctionGroups/<id>`.
    std::string function_group{};     ///< Resolved Function Group name.
    std::vector<std::string> args{};  ///< Command-line arguments.

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Human-readable text naming the process and its executable.
    [[nodiscard]] std::string to_string() const;
};

/// @brief One Function Group declared by a Machine Manifest.
struct MachineFunctionGroup {
    std::string name{};                ///< Function Group name.
    std::string id{};                  ///< Machine-unique identifier referenced by processes.
    std::vector<std::string> states{}; ///< Declared state names.

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Human-readable text naming the Function Group and its states.
    [[nodiscard]] std::string to_string() const;
};

/// @brief One Machine with its Function Groups and processes.
struct Machine {
    std::string name{};                                  ///< Machine name.
    std::vector<MachineFunctionGroup> function_groups{}; ///< Declared Function Groups.
    std::vector<MachineProcess> processes{};             ///< Declared processes.

    /// @brief Find one Function Group by name or id.
    ///
    /// @param name_or_id Function Group name or machine-unique id.
    /// @return The matching group, or `nullptr` when none matches.
    [[nodiscard]] const MachineFunctionGroup*
    find_function_group(std::string_view name_or_id) const noexcept;

    /// @brief Find one process by name.
    ///
    /// @param name Process name.
    /// @return The matching process, or `nullptr` when none matches.
    [[nodiscard]] const MachineProcess* find_process(std::string_view name) const noexcept;

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Human-readable text naming the machine and its contents.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Parsed Machine Manifest.
struct MachineManifest {
    std::vector<Machine> machines{}; ///< Declared machines.

    /// @brief Find one Machine by name.
    ///
    /// @param name Machine name.
    /// @return The matching machine, or `nullptr` when none matches.
    /// @note Spec `exec/machine-manifest`, requirement "Machine Manifest
    ///       vocabulary".
    [[nodiscard]] const Machine* find_machine(std::string_view name) const noexcept;

    /// @brief Resolve every `/Machine/FunctionGroups/<id>` reference.
    ///
    /// @return Success, or `InvalidArgument` when a reference does not resolve.
    /// @note Spec `exec/machine-manifest`, requirement "Machine reference
    ///       resolution".
    [[nodiscard]] core::Status resolve_references() noexcept;

    /// @brief Validate names, ids, executables, and resolved references.
    ///
    /// @return Success, or `InvalidArgument` describing the first violation.
    /// @note Spec `exec/machine-manifest`, requirement "Machine validation".
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Human-readable text listing the declared machines.
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
