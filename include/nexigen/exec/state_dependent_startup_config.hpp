// nexigen::exec — per-state startup configuration (change
// align-exec-process-model-with-autosar; spec exec/app-suite).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/exec/execution_dependency.hpp>

namespace nexigen::exec {

/// @brief One application's launch configuration for one Function Group State.
///
/// A process may carry several configurations for states of its single
/// Function Group. The configuration supplies the arguments, environment,
/// startup/termination timeouts, restart budget, and execution dependencies
/// that apply while that state is requested.
///
/// @note Spec `exec/app-suite`, requirement "State-dependent startup
///       configuration".
struct StateDependentStartupConfig {
    std::string state{};             ///< Function Group State this configuration applies to.
    std::vector<std::string> args{}; ///< Arguments passed verbatim, without shell interpretation.
    std::vector<std::pair<std::string, std::string>> env{}; ///< Child-only environment additions.
    std::uint64_t startup_timeout_ms{0};        ///< Readiness budget; 0 = no explicit budget.
    std::uint64_t termination_timeout_ms{5000}; ///< SIGTERM grace before SIGKILL.
    std::uint32_t restart_attempts{0};          ///< Startup retries before the transition fails.
    std::uint64_t restart_delay_s{0};           ///< Delay between startup attempts.
    std::vector<ExecutionDependency> dependencies{}; ///< Required process states.

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text naming the state, argument/environment counts, timeouts,
    ///         restart budget, and dependency count. Allocates; cold path only.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `StateDependentStartupConfig::to_string`.
[[nodiscard]] inline std::string to_string(const StateDependentStartupConfig& config) {
    return config.to_string();
}

/// @brief Serialize one startup configuration.
///
/// @param j Destination JSON value.
/// @param config Configuration to serialize.
/// @note Project mandate 10.
void to_json(nlohmann::json& j, const StateDependentStartupConfig& config);

/// @brief Parse one startup configuration without throwing.
///
/// @param j Source JSON value.
/// @param config Target; fields that parse are updated, hostile input is ignored.
/// @note Project mandate 10.
void from_json(const nlohmann::json& j, StateDependentStartupConfig& config) noexcept;

} // namespace nexigen::exec

/// @brief `std::formatter` for `StateDependentStartupConfig`.
template <>
struct std::formatter<nexigen::exec::StateDependentStartupConfig>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::StateDependentStartupConfig& config, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(config.to_string(), ctx);
    }
};
