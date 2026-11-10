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

/// The EM's Function Group registry: groups -> bound applications + state.
class FunctionGroupRegistry {
public:
    /// Build the registry from the suite manifest (groups + per-app bindings).
    explicit FunctionGroupRegistry(const SuiteManifest& manifest);

    /// Number of configured groups.
    [[nodiscard]] std::size_t group_count() const noexcept;

    /// The name of one group.
    [[nodiscard]] std::string_view group_name(std::size_t index) const noexcept;

    /// The current state of one group.
    [[nodiscard]] std::string_view group_state(std::size_t index) const noexcept;

    /// The applications bound to one group (in manifest order).
    [[nodiscard]] const std::vector<std::string>& bound_apps(std::size_t index) const noexcept;

    /// The launch command of one bound application; empty = app name itself.
    [[nodiscard]] std::string_view command_of(std::string_view app_name) const noexcept;

    /// Index of the group with `name`; npos when unknown.
    [[nodiscard]] std::size_t find_group(std::string_view name) const noexcept;

    /// Update one group's current state (the StateClient drives this).
    void set_state(std::size_t index, std::string_view state) noexcept;

    /// True for Off-like targets (stop the group's processes).
    [[nodiscard]] static bool is_off_target(std::string_view target) noexcept;

    /// Cold-path diagnostic (mandate 9): groups and their states.
    [[nodiscard]] std::string to_string() const;

private:
    struct GroupRuntime {
        std::vector<std::string> app_names{};
        std::size_t state_index{0};
    };

    const SuiteManifest* manifest_{nullptr};
    std::vector<GroupRuntime> groups_{};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const FunctionGroupRegistry& registry) {
    return registry.to_string();
}

} // namespace nexigen::exec

/// The registry formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::exec::FunctionGroupRegistry> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::FunctionGroupRegistry& registry, Ctx& ctx) const {
        const std::string text = registry.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
