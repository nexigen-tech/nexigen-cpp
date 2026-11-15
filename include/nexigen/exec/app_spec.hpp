// nexigen::exec — one application entry in the suite manifest
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/exec/impact_on_app_suite_operability.hpp>
#include <nexigen/exec/process_type.hpp>
#include <nexigen/exec/state_dependent_startup_config.hpp>

namespace nexigen::exec {

/// @brief One application entry in the suite manifest.
///
/// Carries the identity, command, launch settings, restart policy, dependency
/// list, Function Group bindings, and failure impact for one suite
/// application. It is a value type owned by `SuiteManifest`; the suite
/// supervisor reads it on the cold path once per launch attempt and never
/// mutates it.
///
/// @note Spec `exec/app-suite`, requirement "Suite manifest has a fixed
///       schema".
/// @note Spec `exec/app-suite`, requirement "Manifest launch settings reach
///       each child".
/// @note Spec `exec/app-suite`, requirement "Settings sources merge per
///       application".
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
struct AppSpec {
    std::string app{};      ///< Application name; part of the instance identity.
    std::string instance{}; ///< Instance id; part of the instance identity.
    std::string command{};  ///< Executable path or command name.
    /// The single Function Group this application belongs to.
    std::string function_group{};
    /// One or more state-dependent launch configurations for that group.
    std::vector<StateDependentStartupConfig> startup_configs{};
    core::CpuSet cpus{};             ///< Affinity request; empty means unrestricted.
    std::uint64_t idle_timeout_s{0}; ///< Run time before the stop sequence; 0 = no timeout.
    ImpactOnAppSuiteOperability impact{ImpactOnAppSuiteOperability::Degrade};
    /// Termination behavior (`daemon`/`oneshot`), mirroring AUTOSAR
    /// `terminationBehavior`; independent of reporting mode.
    ProcessType type{ProcessType::Daemon};
    /// Reporting processes wait for `ReportExecutionState(Run)`; non-reporting
    /// processes enter Running immediately after creation.
    bool reporting{true};
    /// Per-application global settings directory; replaces the suite value.
    std::string global_settings_path{};
    /// Per-application local settings path; resolved relative to the manifest.
    std::string local_settings_path{};
    std::optional<core::LogLevel> log_level{}; ///< Empty = use the suite level.
    std::string resource_group{};              ///< Optional resource-group name.
    std::string executable_digest{};           ///< Optional SHA-256 hex digest.
    std::optional<std::uint32_t> uid{};        ///< Optional target user; 0 is rejected.
    std::optional<std::uint32_t> gid{};        ///< Optional target group; requires `uid`.
    /// Keep `CAP_SYS_NICE` in the child's bounding set (RT priority opt-in).
    /// @note Spec `exec/process-supervision`, requirement "RT priority
    ///       capability is opt-in". A non-root `uid` cannot be combined with
    ///       this option because the credential drop clears the capability.
    bool allow_rt_priority{false};

    /// @brief Cold-path diagnostic rendering of the manifest entry.
    ///
    /// @return Human-readable text naming the application and its launch
    ///         policy. Allocated on the cold path only.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper (project mandate 9): mirrors the member.
///
/// @param value Manifest entry to render.
/// @return The same text as `value.to_string()`.
[[nodiscard]] inline std::string to_string(const AppSpec& value) {
    return value.to_string();
}

/// @brief Find the startup configuration for one Function Group State.
///
/// @param spec Application entry.
/// @param state State name.
/// @return The matching configuration, or `nullptr` when the application has no
///         configuration for that state.
[[nodiscard]] inline const StateDependentStartupConfig*
find_startup_config(const AppSpec& spec, std::string_view state) noexcept {
    for (const StateDependentStartupConfig& config : spec.startup_configs) {
        if (config.state == state) {
            return &config;
        }
    }
    return nullptr;
}

/// @brief Serialize an application entry into its manifest object.
///
/// @param j Destination JSON value.
/// @param spec Entry to serialize.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void to_json(nlohmann::json& j, const AppSpec& spec);

/// @brief Parse an application entry from JSON without throwing.
///
/// Unknown fields are ignored; hostile or wrong-typed input leaves `spec`
/// unchanged.
///
/// @param j Source JSON value.
/// @param spec Target entry.
/// @note Spec `exec/app-suite`, requirement "Suite manifest has a fixed
///       schema"; project mandate 10.
void from_json(const nlohmann::json& j, AppSpec& spec) noexcept;

} // namespace nexigen::exec
