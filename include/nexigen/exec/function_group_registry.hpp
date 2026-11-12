// nexigen::exec — EM Function Group registry (change add-em-orchestration;
// spec exec/app-suite "Function Group configuration"; design D3).
//
// The EM's own view of Function Groups (the execution mapping): which
// applications are bound to which group, and the group's current state. The
// SM owns arbitration; the EM owns execution, so both derive their groups
// from their own manifests.
#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec {

/// @brief The EM's Function Group registry: groups -> bound applications +
///        state.
///
/// Built from `SuiteManifest::function_groups` and each application's
/// `function_groups` list. Owns only the binding table and current group-state
/// indices; it borrows the manifest and must not outlive it. Mutated by the
/// `StateClient` on its control thread and read by status/diagnostics, so it
/// is single-threaded by design. Cold path only.
///
/// @note Spec `exec/app-suite`, requirement "Function Group configuration".
/// @note Spec `exec/app-suite`, requirement "Function Group diagnostics".
/// @warning The manifest passed to the constructor must outlive the registry.
class FunctionGroupRegistry {
public:
    /// @brief Build the registry from the suite manifest.
    ///
    /// Copies group names, their state vocabularies, and the per-application
    /// bindings in manifest order. The initial state of each group is the
    /// first declared state.
    ///
    /// @param manifest Parsed manifest; borrowed, not copied.
    /// @note Spec `exec/app-suite`, requirement "Function Group configuration".
    explicit FunctionGroupRegistry(const SuiteManifest& manifest);

    /// @brief Number of configured groups.
    ///
    /// @return The size of the group table.
    [[nodiscard]] std::size_t group_count() const noexcept;

    /// @brief The name of one group.
    ///
    /// @param index Group index; must be below `group_count()`.
    /// @return The group name.
    /// @warning Passing an out-of-range index is undefined behavior.
    [[nodiscard]] std::string_view group_name(std::size_t index) const noexcept;

    /// @brief The current state of one group.
    ///
    /// @param index Group index; must be below `group_count()`.
    /// @return The current state name, or an empty view when the group declares
    ///         no states.
    /// @warning Passing an out-of-range index is undefined behavior.
    [[nodiscard]] std::string_view group_state(std::size_t index) const noexcept;

    /// @brief The applications bound to one group.
    ///
    /// @param index Group index; must be below `group_count()`.
    /// @return The bound names in manifest order.
    /// @warning Passing an out-of-range index is undefined behavior.
    [[nodiscard]] const std::vector<std::string>& bound_apps(std::size_t index) const noexcept;

    /// @brief The launch command of one bound application.
    ///
    /// @param app_name Application name to look up.
    /// @return The manifest command, or an empty view meaning the application
    ///         name itself is the command; empty also for an unknown name.
    [[nodiscard]] std::string_view command_of(std::string_view app_name) const noexcept;

    /// @brief Index of the group with `name`.
    ///
    /// @param name Group name to find.
    /// @return The group index, or `std::string::npos` when unknown.
    [[nodiscard]] std::size_t find_group(std::string_view name) const noexcept;

    /// @brief Update one group's current state.
    ///
    /// Driven by the `StateClient` after it executes a switch. The registry is
    /// the EM's execution-side state; the SM owns the authoritative
    /// arbitration.
    ///
    /// @param index Group index; must be below `group_count()`.
    /// @param state New state name; an unknown name leaves the state unchanged.
    /// @note Spec `exec/state-client`, requirement "Function Group execution".
    /// @warning Passing an out-of-range index is undefined behavior.
    void set_state(std::size_t index, std::string_view state) noexcept;

    /// @brief Whether a target state means "stop the group's processes".
    ///
    /// Only the exact, case-sensitive targets `Off`, `Stopped`, and `Shutdown`
    /// are Off-like; any other target starts the bound applications.
    ///
    /// @param target Target state from a `SetState` command.
    /// @return `true` when the target is Off-like.
    /// @note Spec `exec/state-client`, requirement "Function Group execution".
    [[nodiscard]] static bool is_off_target(std::string_view target) noexcept;

    /// @brief Cold-path diagnostic (mandate 9): groups and their states.
    ///
    /// @return Text naming every group and its current state.
    /// @note Spec `exec/app-suite`, requirement "Function Group diagnostics".
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Per-group runtime record: bound applications and current state.
    struct GroupRuntime {
        std::vector<std::string> app_names{}; ///< Applications bound to the group.
        std::size_t state_index{0};           ///< Index into the group's state list.
    };

    const SuiteManifest* manifest_{nullptr}; ///< Borrowed source manifest.
    std::vector<GroupRuntime> groups_{};     ///< Group table in manifest order.
};

/// @brief Free helper (mandate 9): mirrors the member.
///
/// @param registry Registry to render.
/// @return The same text as `registry.to_string()`.
[[nodiscard]] inline std::string to_string(const FunctionGroupRegistry& registry) {
    return registry.to_string();
}

} // namespace nexigen::exec

/// @brief The registry formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::exec::FunctionGroupRegistry> : std::formatter<std::string_view> {
    /// @brief Format the registry through its `to_string()` member.
    ///
    /// @tparam Ctx `std::formatter` context type.
    /// @param registry Registry to format.
    /// @param ctx Format context receiving the text.
    /// @return The context's output iterator.
    template <class Ctx>
    auto format(const nexigen::exec::FunctionGroupRegistry& registry, Ctx& ctx) const {
        const std::string text = registry.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
