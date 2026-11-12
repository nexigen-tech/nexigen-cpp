// nexigen::exec — one application entry in the suite manifest
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/exec/impact_on_app_suite_operability.hpp>

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
    std::string app{};               ///< Application name; part of the instance identity.
    std::string instance{};          ///< Instance id; part of the instance identity.
    std::string command{};           ///< Executable path or command name.
    std::vector<std::string> args{}; ///< Arguments passed verbatim, without shell interpretation.
    std::vector<std::pair<std::string, std::string>> env{}; ///< Child-only environment additions.
    core::CpuSet cpus{};               ///< Affinity request; empty means unrestricted.
    std::uint64_t idle_timeout_s{0};   ///< Run time before the stop sequence; 0 = no timeout.
    std::uint64_t stop_grace_s{5};     ///< SIGTERM grace before SIGKILL.
    std::uint32_t restart_attempts{0}; ///< Per-application retries for degrade/not-impact.
    std::uint64_t restart_delay_s{0};  ///< Delay before each per-application retry.
    /// Applications that must report Running before this one starts (REQ_EM_001).
    std::vector<std::string> dependencies{};
    /// Function Groups whose active states keep this application running (REQ_EM_005/006).
    std::vector<std::string> function_groups{};
    ImpactOnAppSuiteOperability impact{ImpactOnAppSuiteOperability::Degrade};
    /// Per-application global settings directory; replaces the suite value.
    std::string global_settings_path{};
    /// Per-application local settings path; resolved relative to the manifest.
    std::string local_settings_path{};
    std::optional<core::LogLevel> log_level{}; ///< Empty = use the suite level.

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
