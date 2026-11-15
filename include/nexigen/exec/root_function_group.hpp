// nexigen::exec — reserved RootFG Function Group vocabulary
// (change machinefg-as-em-function-group; spec exec/root-function-group).
//
// The root-foundation Function Group is an ordinary EM Function Group with
// a fixed state vocabulary. It is optional in a manifest; when declared, the EM
// owns the initial `Off -> Startup` transition and reserves `Off` for its own
// shutdown path.
#pragma once

#include <array>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::exec {

struct FunctionGroupConfig;

/// Reserved Function Group name for the root foundation.
inline constexpr std::string_view k_root_function_group_name = "RootFG";

inline constexpr std::string_view k_root_state_off = "Off";
inline constexpr std::string_view k_root_state_startup = "Startup";
inline constexpr std::string_view k_root_state_verify = "Verify";
inline constexpr std::string_view k_root_state_run = "Run";
inline constexpr std::string_view k_root_state_shutdown = "Shutdown";
inline constexpr std::string_view k_root_state_restart = "Restart";

/// @brief Whether a Function Group name is the reserved `RootFG`.
///
/// @param name Function Group name.
/// @return `true` for the exact reserved name.
[[nodiscard]] constexpr bool is_root_function_group(std::string_view name) noexcept {
    return name == k_root_function_group_name;
}

/// @brief The fixed `RootFG` state vocabulary.
///
/// @return The six canonical states in lifecycle order.
[[nodiscard]] constexpr std::array<std::string_view, 6> root_function_group_states() noexcept {
    return {k_root_state_off, k_root_state_startup,  k_root_state_verify,
            k_root_state_run, k_root_state_shutdown, k_root_state_restart};
}

/// @brief The EM-selected suite start state for `RootFG`.
///
/// `Off` is honoured when explicitly declared; every other value (including an
/// omitted `initial_state`) resolves to `Startup`, because the EM owns the
/// initial `Off -> Startup` transition.
///
/// @param declared_initial_state The manifest `initial_state` value.
/// @return `Off` or `Startup`.
[[nodiscard]] constexpr std::string_view
root_function_group_initial_state(std::string_view declared_initial_state) noexcept {
    return declared_initial_state == k_root_state_off ? k_root_state_off
                                                         : k_root_state_startup;
}

/// @brief Validate a declared `RootFG` group.
///
/// Requires exactly the six canonical states and an `initial_state` of empty,
/// `Off`, or `Startup`.
///
/// @param group Parsed Function Group configuration named `RootFG`.
/// @return Success, or `InvalidArgument` when the vocabulary or initial state
///         violates the reserved contract.
[[nodiscard]] core::Status
validate_root_function_group(const FunctionGroupConfig& group) noexcept;

} // namespace nexigen::exec
