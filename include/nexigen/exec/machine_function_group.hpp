// nexigen::exec — reserved MachineFG Function Group vocabulary
// (change machinefg-as-em-function-group; spec exec/machine-function-group).
//
// The platform-foundation Function Group is an ordinary EM Function Group with
// a fixed state vocabulary. It is optional in a manifest; when declared, the EM
// owns the initial `Off -> Startup` transition and reserves `Off` for its own
// shutdown path.
#pragma once

#include <array>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::exec {

struct FunctionGroupConfig;

/// Reserved Function Group name for the platform foundation.
inline constexpr std::string_view k_machine_function_group_name = "MachineFG";

inline constexpr std::string_view k_machine_state_off = "Off";
inline constexpr std::string_view k_machine_state_startup = "Startup";
inline constexpr std::string_view k_machine_state_verify = "Verify";
inline constexpr std::string_view k_machine_state_run = "Run";
inline constexpr std::string_view k_machine_state_shutdown = "Shutdown";
inline constexpr std::string_view k_machine_state_restart = "Restart";

/// @brief Whether a Function Group name is the reserved `MachineFG`.
///
/// @param name Function Group name.
/// @return `true` for the exact reserved name.
[[nodiscard]] constexpr bool is_machine_function_group(std::string_view name) noexcept {
    return name == k_machine_function_group_name;
}

/// @brief The fixed `MachineFG` state vocabulary.
///
/// @return The six canonical states in lifecycle order.
[[nodiscard]] constexpr std::array<std::string_view, 6> machine_function_group_states() noexcept {
    return {k_machine_state_off, k_machine_state_startup,  k_machine_state_verify,
            k_machine_state_run, k_machine_state_shutdown, k_machine_state_restart};
}

/// @brief The EM-selected suite start state for `MachineFG`.
///
/// `Off` is honoured when explicitly declared; every other value (including an
/// omitted `initial_state`) resolves to `Startup`, because the EM owns the
/// initial `Off -> Startup` transition.
///
/// @param declared_initial_state The manifest `initial_state` value.
/// @return `Off` or `Startup`.
[[nodiscard]] constexpr std::string_view
machine_function_group_initial_state(std::string_view declared_initial_state) noexcept {
    return declared_initial_state == k_machine_state_off ? k_machine_state_off
                                                         : k_machine_state_startup;
}

/// @brief Validate a declared `MachineFG` group.
///
/// Requires exactly the six canonical states and an `initial_state` of empty,
/// `Off`, or `Startup`.
///
/// @param group Parsed Function Group configuration named `MachineFG`.
/// @return Success, or `InvalidArgument` when the vocabulary or initial state
///         violates the reserved contract.
[[nodiscard]] core::Status
validate_machine_function_group(const FunctionGroupConfig& group) noexcept;

} // namespace nexigen::exec
