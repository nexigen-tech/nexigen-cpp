// nexigen::exec — one execution dependency (change
// align-exec-process-model-with-autosar; spec exec/process-supervision).
#pragma once

#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/exec/process_state.hpp>

namespace nexigen::exec {

/// @brief A process-state dependency of one startup configuration.
///
/// Only `Running` and `Terminated` are valid dependency targets, mirroring the
/// AUTOSAR execution-dependency process states.
///
/// @note Spec `exec/process-supervision`, requirement "Dependency-ordered
///       startup".
struct ExecutionDependency {
    std::string process{};                     ///< Target application name.
    ProcessState state{ProcessState::Running}; ///< Required state (Running/Terminated).

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return `ExecutionDependency(process=<name>, state=<state>)`.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const {
        return std::format("ExecutionDependency(process={}, state={})", process,
                           nexigen::exec::to_string(state));
    }
};

/// @brief Free helper mirroring `ExecutionDependency::to_string`.
[[nodiscard]] inline std::string to_string(const ExecutionDependency& dependency) {
    return dependency.to_string();
}

/// @brief Serialize one dependency.
///
/// @param j Destination JSON value.
/// @param dependency Dependency to serialize.
/// @note Project mandate 10.
void to_json(nlohmann::json& j, const ExecutionDependency& dependency);

/// @brief Parse one dependency without throwing.
///
/// @param j Source JSON value.
/// @param dependency Target; fields that parse are updated, hostile input is ignored.
/// @note Project mandate 10.
void from_json(const nlohmann::json& j, ExecutionDependency& dependency) noexcept;

} // namespace nexigen::exec

/// @brief `std::formatter` for `ExecutionDependency`.
template <>
struct std::formatter<nexigen::exec::ExecutionDependency> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::ExecutionDependency& dependency, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(dependency.to_string(), ctx);
    }
};
