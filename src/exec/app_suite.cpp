// nexigen::exec — application suite manifest and supervision
// (change nexigen-exec-app-suite; spec execution_manager/app-suite).
//
// COLD PATH ONLY. The suite supervisor is a detached process that launches
// one existing per-instance supervisor per application, polls instance files,
// and applies restart/impact policy.
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/app/telemetry_layout.hpp>
#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/exec/detail/cpu_validation.hpp>
#include <nexigen/exec/detail/path_utils.hpp>
#include <nexigen/exec/detail/sys_helpers.hpp>
#include <nexigen/exec/app_suite.hpp>
#include <nexigen/exec/instance_lifecycle.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/execution_manager_logging.hpp>

#include "coverage_flush.hpp"

namespace nexigen::exec {

using detail::absolute_from_cwd;
using detail::absolute_path;
using detail::child_exit;
using detail::cpus_within_online;
using detail::current_directory;
using detail::directory_of;
using detail::file_exists;
using detail::group_alive;
using detail::has_parent_segment;
using detail::identity_matches;
using detail::instance_dir_path;
using detail::join_path;
using detail::max_path_bytes;
using detail::monotonic_now_ns;
using detail::nanoseconds_per_second;
using detail::proc_start_ticks;
using detail::process_alive;
using core::mandate::read_enum;
using detail::read_i32;
using detail::read_start_report;
using detail::read_string;
using detail::read_u32;
using detail::read_u64;
using detail::redirect_to_null;
using detail::safe_id;
using detail::sleep_ms;
using detail::StartReport;
using detail::utc_now_rfc3339;
using detail::validate_env_entries;
using detail::write_start_report;

namespace {

constexpr std::size_t max_apps = 256;
constexpr std::uint32_t max_restart_attempts = 1000;
constexpr std::uint64_t max_delay_seconds = 86400;
constexpr std::uint64_t suite_poll_interval_ms = 100;

// --- JSON helpers ----------------------------------------------------------

[[nodiscard]] bool read_log_level(const nlohmann::json& document, const char* key,
                                  std::optional<core::LogLevel>& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end()) {
        return false;
    }
    if (it->is_null()) {
        target = std::nullopt;
        return true;
    }
    if (!it->is_string()) {
        return false;
    }
    const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
    if (name == nullptr) {
        return false;
    }
    core::LogLevel level = core::LogLevel::Info;
    if (!core::from_string(std::string_view(*name), level)) {
        return false;
    }
    target = level;
    return true;
}

[[nodiscard]] nlohmann::json log_level_json(const std::optional<core::LogLevel>& level) {
    if (!level.has_value()) {
        return nullptr;
    }
    return std::string(core::to_string(*level));
}

[[nodiscard]] bool read_string_array(const nlohmann::json& document, const char* key,
                                     std::vector<std::string>& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_array()) {
        return false;
    }
    std::vector<std::string> values;
    values.reserve(it->size());
    for (const auto& item : *it) {
        if (!item.is_string()) {
            return false;
        }
        values.emplace_back(item.get<std::string>());
    }
    target = std::move(values);
    return true;
}

[[nodiscard]] bool read_cpu_array(const nlohmann::json& document, const char* key,
                                  std::vector<std::uint32_t>& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_array()) {
        return false;
    }
    std::vector<std::uint32_t> values;
    values.reserve(it->size());
    for (const auto& item : *it) {
        if (!item.is_number_unsigned()) {
            return false;
        }
        const std::uint64_t value = item.get<std::uint64_t>();
        if (value > UINT32_MAX) {
            return false;
        }
        values.push_back(static_cast<std::uint32_t>(value));
    }
    target = std::move(values);
    return true;
}

[[nodiscard]] bool read_cpu_set(const nlohmann::json& document, const char* key,
                                core::CpuSet& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end()) {
        return false;
    }
    return core::parse_cpu_set_json(*it, target);
}

void read_legacy_execution_manager_cpus(const nlohmann::json& document, const char* key,
                                        core::ThreadSpec& target) noexcept {
    std::vector<std::uint32_t> values;
    if (!read_cpu_array(document, key, values)) {
        return;
    }
    const core::Result<core::CpuSet> converted = core::CpuSet::from_cpus(values);
    if (converted) {
        target.set_cpus(*converted);
    }
}

void read_canonical_thread(const nlohmann::json& document, const char* key,
                           core::ThreadSpec& target) noexcept {
    const auto it = document.find(key);
    if (it != document.end()) {
        static_cast<void>(core::parse_thread_spec_json(*it, target));
    }
}

[[nodiscard]] bool
read_env_object(const nlohmann::json& document, const char* key,
                std::vector<std::pair<std::string, std::string>>& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_object()) {
        return false;
    }
    std::vector<std::pair<std::string, std::string>> values;
    values.reserve(it->size());
    for (auto entry = it->begin(); entry != it->end(); ++entry) {
        if (!entry.value().is_string()) {
            return false;
        }
        values.emplace_back(entry.key(), entry.value().get<std::string>());
    }
    target = std::move(values);
    return true;
}

// --- to_string -------------------------------------------------------------

} // namespace

std::string AppSpec::to_string() const {
    return std::format("AppSpec(app={}, instance={}, command={}, args={}, env={}, cpus={}, "
                       "idle_timeout_s={}, stop_grace_s={}, restart_attempts={}, "
                       "restart_delay_s={}, impact={}, global_settings_path={}, "
                       "local_settings_path={}, log_level={})",
                       app, instance, command, args.size(), env.size(), cpus.count(),
                       idle_timeout_s, stop_grace_s, restart_attempts, restart_delay_s,
                       nexigen::exec::to_string(impact), global_settings_path, local_settings_path,
                       log_level.has_value() ? std::string(core::to_string(*log_level))
                                             : std::string("inherit"));
}

std::string SuiteManifest::to_string() const {
    return std::format("SuiteManifest(version={}, suite_id={}, run_id={}, run_dir={}, "
                       "global_settings_path={}, artifacts_path={}, log_level={}, "
                       "suite_restart_attempts={}, suite_restart_delay_s={}, apps={})",
                       version, suite_id, run_id, run_dir, global_settings_path, artifacts_path,
                       log_level.has_value() ? std::string(core::to_string(*log_level))
                                             : std::string("inherit"),
                       suite_restart_attempts, suite_restart_delay_s, apps.size());
}

std::string SuiteAppMetadata::to_string() const {
    return std::format("SuiteAppMetadata(app={}, instance={}, instance_dir={}, "
                       "stop_grace_s={})",
                       app, instance, instance_dir, stop_grace_s);
}

std::string SuiteMetadata::to_string() const {
    return std::format("SuiteMetadata(suite_id={}, run_id={}, run_dir={}, "
                       "global_settings_path={}, artifacts_path={}, pid={}, "
                       "start_time_rfc3339={}, proc_start_ticks={}, "
                       "manifest_path={}, apps={})",
                       suite_id, run_id, run_dir, global_settings_path, artifacts_path, pid,
                       start_time_rfc3339, proc_start_ticks, manifest_path, apps.size());
}

std::string SuiteExit::to_string() const {
    return std::format("SuiteExit(state={}, reason={}, uptime_s={})",
                       nexigen::exec::to_string(state), reason, uptime_s);
}

std::string SuiteAppStatus::to_string() const {
    return std::format("SuiteAppStatus(app={}, instance={}, instance_dir={}, "
                       "artifact_dir={}, attempt={}, state={}, restarts_used={})",
                       app, instance, instance_dir, artifact_dir, attempt,
                       nexigen::exec::to_string(state), restarts_used);
}

std::string SuiteStatus::to_string() const {
    return std::format("SuiteStatus(state={}, suite_id={}, run_dir={}, artifacts_path={}, "
                       "suite_restarts_used={}, apps={})",
                       nexigen::exec::to_string(state), suite_id, run_dir, artifacts_path,
                       suite_restarts_used, apps.size());
}

// --- struct JSON -----------------------------------------------------------

void to_json(nlohmann::json& j, const AppSpec& spec) {
    nlohmann::json env = nlohmann::json::object();
    for (const auto& [key, value] : spec.env) {
        env[key] = value;
    }
    j = nlohmann::json{
        {"app", spec.app},
        {"instance", spec.instance},
        {"command", spec.command},
        {"args", spec.args},
        {"env", std::move(env)},
        {"cpus", spec.cpus},
        {"idle_timeout_s", spec.idle_timeout_s},
        {"stop_grace_s", spec.stop_grace_s},
        {"restart_attempts", spec.restart_attempts},
        {"restart_delay_s", spec.restart_delay_s},
        {"dependencies", spec.dependencies},
        {"function_groups", spec.function_groups},
        {"impact_on_app_suite_operability", std::string(to_string(spec.impact))},
        {"global_settings_path", spec.global_settings_path},
        {"local_settings_path", spec.local_settings_path},
        {"log_level", log_level_json(spec.log_level)},
    };
}

void from_json(const nlohmann::json& j, AppSpec& spec) noexcept {
    static_cast<void>(read_string(j, "app", spec.app));
    static_cast<void>(read_string(j, "instance", spec.instance));
    static_cast<void>(read_string(j, "command", spec.command));
    static_cast<void>(read_string_array(j, "args", spec.args));
    static_cast<void>(read_env_object(j, "env", spec.env));
    static_cast<void>(read_cpu_set(j, "cpus", spec.cpus));
    static_cast<void>(read_u64(j, "idle_timeout_s", spec.idle_timeout_s));
    static_cast<void>(read_u64(j, "stop_grace_s", spec.stop_grace_s));
    static_cast<void>(read_u32(j, "restart_attempts", spec.restart_attempts));
    static_cast<void>(read_u64(j, "restart_delay_s", spec.restart_delay_s));
    static_cast<void>(read_string_array(j, "dependencies", spec.dependencies));
    static_cast<void>(read_string_array(j, "function_groups", spec.function_groups));
    read_enum(j, "impact_on_app_suite_operability", spec.impact);
    static_cast<void>(read_string(j, "global_settings_path", spec.global_settings_path));
    static_cast<void>(read_string(j, "local_settings_path", spec.local_settings_path));
    static_cast<void>(read_log_level(j, "log_level", spec.log_level));
}

void to_json(nlohmann::json& j, const SuiteManifest& manifest) {
    nlohmann::json apps = nlohmann::json::array();
    for (const AppSpec& app : manifest.apps) {
        nlohmann::json entry;
        to_json(entry, app);
        apps.push_back(std::move(entry));
    }
    nlohmann::json groups = nlohmann::json::array();
    for (const FunctionGroupConfig& group : manifest.function_groups) {
        groups.push_back(nlohmann::json{{"name", group.name}, {"states", group.states}});
    }
    j = nlohmann::json{
        {"version", manifest.version},
        {"suite_id", manifest.suite_id},
        {"execution_manager_name", manifest.execution_manager_name},
        {"run_id", manifest.run_id},
        {"run_dir", manifest.run_dir},
        {"global_settings_path", manifest.global_settings_path},
        {"artifacts_path", manifest.artifacts_path},
        {"log_level", log_level_json(manifest.log_level)},
        {"suite_restart_attempts", manifest.suite_restart_attempts},
        {"suite_restart_delay_s", manifest.suite_restart_delay_s},
        {"manifest_path", manifest.manifest_path},
        {"execution_manager_control_thread", manifest.execution_manager_control_thread},
        {"execution_manager_logger_thread", manifest.execution_manager_logger_thread},
        {"function_groups", std::move(groups)},
        {"apps", std::move(apps)},
    };
}

void from_json(const nlohmann::json& j, SuiteManifest& manifest) noexcept {
    static_cast<void>(read_u32(j, "version", manifest.version));
    static_cast<void>(read_string(j, "suite_id", manifest.suite_id));
    static_cast<void>(read_string(j, "execution_manager_name", manifest.execution_manager_name));
    static_cast<void>(read_string(j, "run_id", manifest.run_id));
    static_cast<void>(read_string(j, "run_dir", manifest.run_dir));
    static_cast<void>(read_string(j, "global_settings_path", manifest.global_settings_path));
    static_cast<void>(read_string(j, "artifacts_path", manifest.artifacts_path));
    static_cast<void>(read_log_level(j, "log_level", manifest.log_level));
    static_cast<void>(read_u32(j, "suite_restart_attempts", manifest.suite_restart_attempts));
    static_cast<void>(read_u64(j, "suite_restart_delay_s", manifest.suite_restart_delay_s));
    static_cast<void>(read_string(j, "manifest_path", manifest.manifest_path));
    read_legacy_execution_manager_cpus(j, "execution_manager_control_cpus",
                                       manifest.execution_manager_control_thread);
    read_legacy_execution_manager_cpus(j, "execution_manager_logger_cpus",
                                       manifest.execution_manager_logger_thread);
    read_canonical_thread(j, "execution_manager_control_thread",
                          manifest.execution_manager_control_thread);
    read_canonical_thread(j, "execution_manager_logger_thread",
                          manifest.execution_manager_logger_thread);
    if (const auto it = j.find("function_groups"); it != j.end() && it->is_array()) {
        std::vector<FunctionGroupConfig> groups;
        groups.reserve(it->size());
        for (const auto& entry : *it) {
            if (!entry.is_object()) {
                continue;
            }
            FunctionGroupConfig group;
            static_cast<void>(read_string(entry, "name", group.name));
            static_cast<void>(read_string_array(entry, "states", group.states));
            groups.push_back(std::move(group));
        }
        manifest.function_groups = std::move(groups);
    }
    if (const auto it = j.find("apps"); it != j.end() && it->is_array()) {
        std::vector<AppSpec> apps;
        apps.reserve(it->size());
        for (const auto& entry : *it) {
            AppSpec app;
            from_json(entry, app);
            apps.push_back(std::move(app));
        }
        manifest.apps = std::move(apps);
    }
}

void to_json(nlohmann::json& j, const SuiteAppMetadata& app) {
    j = nlohmann::json{{"app", app.app},
                       {"instance", app.instance},
                       {"instance_dir", app.instance_dir},
                       {"stop_grace_s", app.stop_grace_s}};
}

void from_json(const nlohmann::json& j, SuiteAppMetadata& app) noexcept {
    static_cast<void>(read_string(j, "app", app.app));
    static_cast<void>(read_string(j, "instance", app.instance));
    static_cast<void>(read_string(j, "instance_dir", app.instance_dir));
    static_cast<void>(read_u64(j, "stop_grace_s", app.stop_grace_s));
}

void to_json(nlohmann::json& j, const SuiteMetadata& metadata) {
    nlohmann::json apps = nlohmann::json::array();
    for (const SuiteAppMetadata& app : metadata.apps) {
        nlohmann::json entry;
        to_json(entry, app);
        apps.push_back(std::move(entry));
    }
    j = nlohmann::json{
        {"suite_id", metadata.suite_id},
        {"execution_manager_name", metadata.execution_manager_name},
        {"run_id", metadata.run_id},
        {"run_dir", metadata.run_dir},
        {"global_settings_path", metadata.global_settings_path},
        {"artifacts_path", metadata.artifacts_path},
        {"pid", metadata.pid},
        {"start_time_rfc3339", metadata.start_time_rfc3339},
        {"start_time_mono_ns", metadata.start_time_mono_ns},
        {"proc_start_ticks", metadata.proc_start_ticks},
        {"manifest_path", metadata.manifest_path},
        {"execution_manager_control_thread", metadata.execution_manager_control_thread},
        {"execution_manager_logger_thread", metadata.execution_manager_logger_thread},
        {"apps", std::move(apps)},
    };
}

void from_json(const nlohmann::json& j, SuiteMetadata& metadata) noexcept {
    static_cast<void>(read_string(j, "suite_id", metadata.suite_id));
    static_cast<void>(read_string(j, "execution_manager_name", metadata.execution_manager_name));
    static_cast<void>(read_string(j, "run_id", metadata.run_id));
    static_cast<void>(read_string(j, "run_dir", metadata.run_dir));
    static_cast<void>(read_string(j, "global_settings_path", metadata.global_settings_path));
    static_cast<void>(read_string(j, "artifacts_path", metadata.artifacts_path));
    static_cast<void>(read_i32(j, "pid", metadata.pid));
    static_cast<void>(read_string(j, "start_time_rfc3339", metadata.start_time_rfc3339));
    static_cast<void>(read_u64(j, "start_time_mono_ns", metadata.start_time_mono_ns));
    static_cast<void>(read_u64(j, "proc_start_ticks", metadata.proc_start_ticks));
    static_cast<void>(read_string(j, "manifest_path", metadata.manifest_path));
    read_legacy_execution_manager_cpus(j, "execution_manager_control_cpus",
                                       metadata.execution_manager_control_thread);
    read_legacy_execution_manager_cpus(j, "execution_manager_logger_cpus",
                                       metadata.execution_manager_logger_thread);
    read_canonical_thread(j, "execution_manager_control_thread",
                          metadata.execution_manager_control_thread);
    read_canonical_thread(j, "execution_manager_logger_thread",
                          metadata.execution_manager_logger_thread);
    if (const auto it = j.find("apps"); it != j.end() && it->is_array()) {
        std::vector<SuiteAppMetadata> apps;
        apps.reserve(it->size());
        for (const auto& entry : *it) {
            SuiteAppMetadata app;
            from_json(entry, app);
            apps.push_back(std::move(app));
        }
        metadata.apps = std::move(apps);
    }
}

void to_json(nlohmann::json& j, const SuiteExit& exit) {
    j = nlohmann::json{{"state", std::string(to_string(exit.state))},
                       {"reason", exit.reason},
                       {"uptime_s", exit.uptime_s}};
}

void from_json(const nlohmann::json& j, SuiteExit& exit) noexcept {
    read_enum(j, "state", exit.state);
    static_cast<void>(read_string(j, "reason", exit.reason));
    static_cast<void>(read_u64(j, "uptime_s", exit.uptime_s));
}

void to_json(nlohmann::json& j, const SuiteAppStatus& status) {
    j = nlohmann::json{{"app", status.app},
                       {"instance", status.instance},
                       {"instance_dir", status.instance_dir},
                       {"artifact_dir", status.artifact_dir},
                       {"attempt", status.attempt},
                       {"state", std::string(to_string(status.state))},
                       {"restarts_used", status.restarts_used}};
}

void from_json(const nlohmann::json& j, SuiteAppStatus& status) noexcept {
    static_cast<void>(read_string(j, "app", status.app));
    static_cast<void>(read_string(j, "instance", status.instance));
    static_cast<void>(read_string(j, "instance_dir", status.instance_dir));
    static_cast<void>(read_string(j, "artifact_dir", status.artifact_dir));
    static_cast<void>(read_u32(j, "attempt", status.attempt));
    read_enum(j, "state", status.state);
    static_cast<void>(read_u32(j, "restarts_used", status.restarts_used));
}

void to_json(nlohmann::json& j, const SuiteStatus& status) {
    nlohmann::json apps = nlohmann::json::array();
    for (const SuiteAppStatus& app : status.apps) {
        nlohmann::json entry;
        to_json(entry, app);
        apps.push_back(std::move(entry));
    }
    j = nlohmann::json{{"state", std::string(to_string(status.state))},
                       {"suite_id", status.suite_id},
                       {"run_dir", status.run_dir},
                       {"artifacts_path", status.artifacts_path},
                       {"suite_restarts_used", status.suite_restarts_used},
                       {"apps", std::move(apps)}};
}

void from_json(const nlohmann::json& j, SuiteStatus& status) noexcept {
    read_enum(j, "state", status.state);
    static_cast<void>(read_string(j, "suite_id", status.suite_id));
    static_cast<void>(read_string(j, "run_dir", status.run_dir));
    static_cast<void>(read_string(j, "artifacts_path", status.artifacts_path));
    static_cast<void>(read_u32(j, "suite_restarts_used", status.suite_restarts_used));
    if (const auto it = j.find("apps"); it != j.end() && it->is_array()) {
        std::vector<SuiteAppStatus> apps;
        apps.reserve(it->size());
        for (const auto& entry : *it) {
            SuiteAppStatus app;
            from_json(entry, app);
            apps.push_back(std::move(app));
        }
        status.apps = std::move(apps);
    }
}

// --- path contract ---------------------------------------------------------

std::string suite_dir_path(std::string_view run_dir, std::string_view suite_id) {
    return std::string(run_dir) + "/.suite/" + std::string(suite_id);
}

std::string attempt_dir_path(std::string_view artifacts_path, std::string_view suite_id,
                             std::string_view app, std::string_view instance,
                             std::uint32_t attempt) {
    return std::string(artifacts_path) + "/" + std::string(suite_id) + "/" + std::string(app) +
           "/" + std::string(instance) + "/" + std::format("attempt-{:04}", attempt);
}

// --- validation ------------------------------------------------------------

namespace {

[[nodiscard]] core::Status invalid_argument() noexcept {
    return std::unexpected(core::ErrorCode::InvalidArgument);
}

[[nodiscard]] bool is_valid_log_level(const std::optional<core::LogLevel>& level) noexcept {
    if (!level.has_value()) {
        return true;
    }
    switch (*level) {
        case core::LogLevel::Verbose:
        case core::LogLevel::Debug:
        case core::LogLevel::Info:
        case core::LogLevel::Warning:
        case core::LogLevel::Error:
        case core::LogLevel::Fatal:
        case core::LogLevel::Off:     return true;
        default:                      return false;
    }
}

[[nodiscard]] core::Status
validate_suite_version_and_log_level(const SuiteManifest& manifest) noexcept {
    if (manifest.version != 1U) {
        return invalid_argument();
    }
    if (!is_valid_log_level(manifest.log_level)) {
        return invalid_argument();
    }
    return {};
}

[[nodiscard]] core::Status validate_suite_paths(const SuiteManifest& manifest) noexcept {
    if (!safe_id(manifest.suite_id) || !absolute_path(manifest.run_dir) ||
        !absolute_path(manifest.artifacts_path)) {
        return invalid_argument();
    }
    if (manifest.run_dir.find('\0') != std::string::npos ||
        manifest.artifacts_path.find('\0') != std::string::npos ||
        manifest.global_settings_path.find('\0') != std::string::npos) {
        return invalid_argument();
    }
    if (!manifest.global_settings_path.empty() && !absolute_path(manifest.global_settings_path)) {
        return invalid_argument();
    }
    return {};
}

[[nodiscard]] core::Status validate_suite_restart_budgets(const SuiteManifest& manifest) noexcept {
    if (manifest.suite_restart_attempts > max_restart_attempts ||
        manifest.suite_restart_delay_s > max_delay_seconds) {
        return invalid_argument();
    }
    return {};
}

[[nodiscard]] core::Status validate_suite_header_and_paths(const SuiteManifest& manifest) noexcept {
    if (const core::Status status = validate_suite_version_and_log_level(manifest); !status) {
        return status;
    }
    if (const core::Status status = validate_suite_paths(manifest); !status) {
        return status;
    }
    return validate_suite_restart_budgets(manifest);
}

[[nodiscard]] core::Status validate_app_count(const SuiteManifest& manifest) noexcept {
    if (manifest.apps.empty() || manifest.apps.size() > max_apps) {
        return invalid_argument();
    }
    return {};
}

[[nodiscard]] core::Status validate_app_identity_and_command(const AppSpec& app) noexcept {
    if (!safe_id(app.app) || !safe_id(app.instance) || app.app == ".suite" || app.command.empty() ||
        app.command.find('\0') != std::string::npos) {
        return invalid_argument();
    }
    return {};
}

[[nodiscard]] core::Status validate_app_arguments(const AppSpec& app) noexcept {
    for (const std::string& argument : app.args) {
        if (argument.find('\0') != std::string::npos) {
            return invalid_argument();
        }
    }
    return {};
}

[[nodiscard]] core::Status validate_app_impact_and_log_level(const AppSpec& app) noexcept {
    if (app.impact != ImpactOnAppSuiteOperability::Failure &&
        app.impact != ImpactOnAppSuiteOperability::Degrade &&
        app.impact != ImpactOnAppSuiteOperability::Not_impact) {
        return invalid_argument();
    }
    if (!is_valid_log_level(app.log_level)) {
        return invalid_argument();
    }
    return {};
}

[[nodiscard]] core::Status validate_app_timeouts(const AppSpec& app) noexcept {
    if (app.restart_attempts > max_restart_attempts || app.restart_delay_s > max_delay_seconds ||
        app.idle_timeout_s > max_delay_seconds || app.stop_grace_s > max_delay_seconds) {
        return invalid_argument();
    }
    return {};
}

[[nodiscard]] core::Status validate_app_settings_paths(const AppSpec& app) noexcept {
    if (!app.global_settings_path.empty() && !absolute_path(app.global_settings_path)) {
        return invalid_argument();
    }
    if (!app.local_settings_path.empty() &&
        (absolute_path(app.local_settings_path) || has_parent_segment(app.local_settings_path))) {
        return invalid_argument();
    }
    return {};
}

[[nodiscard]] core::Status validate_app_cpu_set(const AppSpec& app) noexcept {
    return cpus_within_online(app.cpus) ? core::Status{} : invalid_argument();
}

[[nodiscard]] core::Status validate_app_paths(const AppSpec& app,
                                              const SuiteManifest& manifest) noexcept {
    const std::string instance_dir = instance_dir_path(manifest.run_dir, app.app, app.instance);
    if (instance_dir.size() > max_path_bytes) {
        return invalid_argument();
    }
    const std::string attempt_dir =
        attempt_dir_path(manifest.artifacts_path, manifest.suite_id, app.app, app.instance, 1);
    if (attempt_dir.size() > max_path_bytes) {
        return invalid_argument();
    }
    return {};
}

[[nodiscard]] core::Status validate_app_spec(const AppSpec& app,
                                             const SuiteManifest& manifest) noexcept {
    if (const core::Status status = validate_app_identity_and_command(app); !status) {
        return status;
    }
    if (const core::Status status = validate_app_arguments(app); !status) {
        return status;
    }
    if (const core::Status status = validate_env_entries(app.env); !status) {
        return status;
    }
    if (const core::Status status = validate_app_impact_and_log_level(app); !status) {
        return status;
    }
    if (const core::Status status = validate_app_timeouts(app); !status) {
        return status;
    }
    if (const core::Status status = validate_app_settings_paths(app); !status) {
        return status;
    }
    if (const core::Status status = validate_app_cpu_set(app); !status) {
        return status;
    }
    return validate_app_paths(app, manifest);
}

[[nodiscard]] core::Status
validate_unique_app_instances(const std::vector<AppSpec>& apps) noexcept {
    for (std::size_t index = 0; index < apps.size(); ++index) {
        const AppSpec& app = apps[index];
        for (std::size_t other = 0; other < index; ++other) {
            if (apps[other].app == app.app && apps[other].instance == app.instance) {
                return invalid_argument();
            }
        }
    }
    return {};
}

} // namespace

core::Status validate_suite_manifest(const SuiteManifest& manifest) noexcept {
    if (const core::Status status = validate_suite_header_and_paths(manifest); !status) {
        return status;
    }
    if (const core::Status status = validate_app_count(manifest); !status) {
        return status;
    }
    for (const AppSpec& app : manifest.apps) {
        if (const core::Status status = validate_app_spec(app, manifest); !status) {
            return status;
        }
    }
    return validate_unique_app_instances(manifest.apps);
}

[[nodiscard]] bool valid_json_log_level(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return true;
    }
    if (!it->is_string()) {
        return false;
    }
    const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
    if (name == nullptr) {
        return false;
    }
    core::LogLevel level = core::LogLevel::Info;
    return core::from_string(std::string_view(*name), level);
}

[[nodiscard]] bool valid_json_cpu_array(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_array()) {
        return false;
    }
    for (const auto& item : *it) {
        if (!item.is_number_unsigned()) {
            return false;
        }
        if (item.get<std::uint64_t>() > UINT32_MAX) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_json_thread_spec(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    core::ThreadSpec spec{};
    return core::parse_thread_spec_json(*it, spec);
}

[[nodiscard]] bool valid_json_app_entry(const nlohmann::json& entry) {
    if (!entry.is_object()) {
        return false;
    }
    const auto impact = entry.find("impact_on_app_suite_operability");
    if (impact == entry.end() || !impact->is_string()) {
        return false;
    }
    const auto* const name = impact->get_ptr<const nlohmann::json::string_t*>();
    if (name == nullptr) {
        return false;
    }
    ImpactOnAppSuiteOperability parsed = ImpactOnAppSuiteOperability::Degrade;
    if (!from_string(std::string_view(*name), parsed)) {
        return false;
    }
    return valid_json_log_level(entry, "log_level");
}

[[nodiscard]] bool valid_json_thread_specs(const nlohmann::json& document) {
    for (const char* key :
         {"execution_manager_control_thread", "execution_manager_logger_thread"}) {
        if (!valid_json_thread_spec(document, key)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_json_apps(const nlohmann::json& document) {
    const auto apps = document.find("apps");
    if (apps == document.end() || !apps->is_array()) {
        return false;
    }
    for (const auto& entry : *apps) {
        if (!valid_json_app_entry(entry)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool json_enum_fields_valid(const nlohmann::json& document) {
    if (!document.is_object()) {
        return false;
    }
    if (!valid_json_log_level(document, "log_level")) {
        return false;
    }
    if (!valid_json_cpu_array(document, "execution_manager_control_cpus") ||
        !valid_json_cpu_array(document, "execution_manager_logger_cpus")) {
        return false;
    }
    if (!valid_json_thread_specs(document)) {
        return false;
    }
    return valid_json_apps(document);
}

core::Status load_suite_manifest(std::string_view path, SuiteManifest& out) noexcept {
    const core::Result<nlohmann::json> document = read_json_file(path);
    if (!document) {
        return std::unexpected(document.error());
    }
    if (!json_enum_fields_valid(*document)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    SuiteManifest parsed{};
    from_json(*document, parsed);
    parsed.manifest_path = absolute_from_cwd(path);
    const core::Status valid = validate_suite_manifest(parsed);
    if (!valid) {
        return valid;
    }
    out = std::move(parsed);
    return {};
}

// --- suite supervisor ------------------------------------------------------

namespace {

struct RuntimeApp {
    AppSpec spec{};
    std::string instance_dir{};
    std::string artifact_dir{};
    AppState state{AppState::Unknown};
    std::uint32_t attempt{0};
    std::uint32_t restarts_used{0};
    std::uint64_t next_restart_ns{0};
    std::int32_t child_pid{0};
    std::int32_t supervisor_pid{0};
    bool exit_consumed{false};
    bool launch_failed{false};
};

struct RuntimeSuite {
    const SuiteManifest* manifest{nullptr};
    std::string suite_dir{};
    std::string state_path{};
    std::string exit_path{};
    std::string stop_path{};
    SuiteState state{SuiteState::Running};
    std::uint32_t suite_restarts_used{0};
    bool suite_restart_pending{false};
    std::uint64_t next_suite_restart_ns{0};
    std::uint64_t start_mono_ns{0};
    std::vector<RuntimeApp> apps{};
    bool dirty{false};
};

void reap_supervisors() noexcept {
    int status = 0;
    while (::waitpid(-1, &status, WNOHANG) > 0) {}
}

[[nodiscard]] bool read_exit_metadata(const std::string& instance_dir, ExitMetadata& out) noexcept {
    return read_json_into(instance_file_path(instance_dir, "exit.json"), out);
}

[[nodiscard]] bool read_resource_metadata(const std::string& instance_dir,
                                          ResourceMetadata& out) noexcept {
    return read_json_into(instance_file_path(instance_dir, "resource.json"), out);
}

void write_suite_status(RuntimeSuite& suite) {
    SuiteStatus status{};
    status.state = suite.state;
    status.suite_id = suite.manifest->suite_id;
    status.run_dir = suite.manifest->run_dir;
    status.artifacts_path = suite.manifest->artifacts_path;
    status.suite_restarts_used = suite.suite_restarts_used;
    status.apps.reserve(suite.apps.size());
    for (const RuntimeApp& app : suite.apps) {
        SuiteAppStatus entry{};
        entry.app = app.spec.app;
        entry.instance = app.spec.instance;
        entry.instance_dir = app.instance_dir;
        entry.artifact_dir = app.artifact_dir;
        entry.attempt = app.attempt;
        entry.state = app.state;
        entry.restarts_used = app.restarts_used;
        status.apps.push_back(std::move(entry));
    }
    nlohmann::json document;
    to_json(document, status);
    static_cast<void>(write_json_file(suite.state_path, document));
}

void write_suite_exit(RuntimeSuite& suite, SuiteState state, std::string_view reason) {
    suite.state = state;
    SuiteExit exit{};
    exit.state = state;
    exit.reason = std::string(reason);
    const std::uint64_t now = monotonic_now_ns();
    if (now != 0 && suite.start_mono_ns != 0 && now >= suite.start_mono_ns) {
        exit.uptime_s = (now - suite.start_mono_ns) / nanoseconds_per_second;
    }
    nlohmann::json document;
    to_json(document, exit);
    static_cast<void>(write_json_file(suite.exit_path, document));
    write_suite_status(suite);
}

[[nodiscard]] std::vector<std::string> command_vector(const AppSpec& spec) {
    std::vector<std::string> command;
    command.reserve(spec.args.size() + 1);
    command.push_back(spec.command);
    for (const std::string& argument : spec.args) {
        command.push_back(argument);
    }
    return command;
}

void stop_all(RuntimeSuite& suite) {
    for (RuntimeApp& app : suite.apps) {
        if (app.state == AppState::Running || app.state == AppState::Restarting ||
            app.child_pid > 0 || app.supervisor_pid > 0) {
            ExecutionManager execution_manager;
            static_cast<void>(execution_manager.stop(app.instance_dir, app.spec.stop_grace_s));
        }
        const std::int32_t pgid = app.child_pid;
        if (pgid > 0 && group_alive(pgid)) {
            static_cast<void>(::kill(-static_cast<pid_t>(pgid), SIGKILL));
        }
    }
    reap_supervisors();
}

[[nodiscard]] bool launch_app(RuntimeSuite& suite, RuntimeApp& app) {
    ++app.attempt;
    app.artifact_dir = attempt_dir_path(suite.manifest->artifacts_path, suite.manifest->suite_id,
                                        app.spec.app, app.spec.instance, app.attempt);

    StartOptions options{};
    options.artifact_dir = app.artifact_dir;
    options.global_settings_dir = app.spec.global_settings_path.empty()
                                      ? suite.manifest->global_settings_path
                                      : app.spec.global_settings_path;
    if (!app.spec.local_settings_path.empty()) {
        options.local_settings_dir =
            join_path(directory_of(suite.manifest->manifest_path), app.spec.local_settings_path);
    }
    options.log_level =
        app.spec.log_level.has_value() ? app.spec.log_level : suite.manifest->log_level;
    options.env = app.spec.env;
    options.cpus = app.spec.cpus;

    const app::RunIdentity identity{suite.manifest->run_id, suite.manifest->run_dir,
                                    app.spec.instance, app.instance_dir};
    const std::vector<std::string> command = command_vector(app.spec);
    ExecutionManager execution_manager;
    const core::Status started =
        execution_manager.start(identity, app.spec.app, command, app.spec.idle_timeout_s, options);
    if (!started) {
        app.state = AppState::Failed;
        app.child_pid = 0;
        app.supervisor_pid = 0;
        app.launch_failed = true;
        app.exit_consumed = false;
        return false;
    }

    ResourceMetadata resource{};
    if (read_resource_metadata(app.instance_dir, resource)) {
        app.child_pid = resource.pid;
        app.supervisor_pid = resource.ppid;
    }
    app.state = AppState::Running;
    app.exit_consumed = false;
    app.launch_failed = false;
    app.next_restart_ns = 0;
    return true;
}

void schedule_retry_or_fail(RuntimeSuite& suite, RuntimeApp& app) {
    if (app.restarts_used < app.spec.restart_attempts) {
        ++app.restarts_used;
        app.state = AppState::Restarting;
        app.next_restart_ns =
            monotonic_now_ns() + app.spec.restart_delay_s * nanoseconds_per_second;
        log_execution_manager(core::LogLevel::Info,
                              std::format("event=retry_scheduled app={} instance={} "
                                          "attempt={} impact={} delay_s={}",
                                          app.spec.app, app.spec.instance, app.attempt,
                                          nexigen::exec::to_string(app.spec.impact),
                                          app.spec.restart_delay_s));
    } else {
        app.state = AppState::Failed;
        if (app.spec.impact == ImpactOnAppSuiteOperability::Degrade) {
            suite.state = SuiteState::Degraded;
        }
        log_execution_manager(core::LogLevel::Warning,
                              std::format("event=app_failed app={} instance={} impact={} "
                                          "suite_state={}",
                                          app.spec.app, app.spec.instance,
                                          nexigen::exec::to_string(app.spec.impact),
                                          nexigen::exec::to_string(suite.state)));
    }
    suite.dirty = true;
}

void finish_failed(RuntimeSuite& suite) {
    stop_all(suite);
    for (RuntimeApp& app : suite.apps) {
        if (app.state != AppState::Failed) {
            app.state = AppState::Stopped;
        }
    }
    log_execution_manager(core::LogLevel::Error,
                          std::format("event=suite_failed suite={} reason=restart_exhausted",
                                      suite.manifest->suite_id));
    write_suite_exit(suite, SuiteState::Failed, "restart_exhausted");
    shutdown_execution_manager_logging();
    child_exit(0);
}

void trigger_suite_restart(RuntimeSuite& suite) {
    if (suite.suite_restarts_used >= suite.manifest->suite_restart_attempts) {
        finish_failed(suite);
    }
    ++suite.suite_restarts_used;
    log_execution_manager(core::LogLevel::Warning,
                          std::format("event=suite_restart trigger={} used={} limit={} "
                                      "delay_s={}",
                                      "failure", suite.suite_restarts_used,
                                      suite.manifest->suite_restart_attempts,
                                      suite.manifest->suite_restart_delay_s));
    stop_all(suite);
    for (RuntimeApp& app : suite.apps) {
        app.state = AppState::Restarting;
        app.restarts_used = 0;
        app.exit_consumed = false;
        app.launch_failed = false;
        app.child_pid = 0;
        app.supervisor_pid = 0;
        app.next_restart_ns = 0;
    }
    suite.state = SuiteState::Running;
    suite.suite_restart_pending = true;
    suite.next_suite_restart_ns =
        monotonic_now_ns() + suite.manifest->suite_restart_delay_s * nanoseconds_per_second;
    suite.dirty = true;
    write_suite_status(suite);
}

void handle_abnormal(RuntimeSuite& suite, RuntimeApp& app) {
    if (app.spec.impact == ImpactOnAppSuiteOperability::Failure) {
        trigger_suite_restart(suite);
        return;
    }
    schedule_retry_or_fail(suite, app);
}

void launch_all(RuntimeSuite& suite) {
    for (RuntimeApp& app : suite.apps) {
        static_cast<void>(launch_app(suite, app));
    }
}

void apply_launch_failures(RuntimeSuite& suite) {
    bool failure_impact_failed = false;
    for (const RuntimeApp& app : suite.apps) {
        if (app.launch_failed && app.spec.impact == ImpactOnAppSuiteOperability::Failure) {
            failure_impact_failed = true;
            break;
        }
    }
    if (failure_impact_failed) {
        trigger_suite_restart(suite);
        return;
    }
    for (RuntimeApp& app : suite.apps) {
        if (app.launch_failed) {
            app.launch_failed = false;
            schedule_retry_or_fail(suite, app);
        }
    }
}

[[noreturn]] void finish_suite_stop(RuntimeSuite& suite, const SuiteManifest& manifest) {
    log_execution_manager(core::LogLevel::Info,
                          std::format("event=stop_requested suite={}", manifest.suite_id));
    stop_all(suite);
    for (RuntimeApp& app : suite.apps) {
        app.state = AppState::Stopped;
    }
    write_suite_exit(suite, SuiteState::Stopped, "operator_stop");
    log_execution_manager(
        core::LogLevel::Info,
        std::format("event=suite_stopped suite={} reason=operator_stop", manifest.suite_id));
    shutdown_execution_manager_logging();
    child_exit(0);
}

[[nodiscard]] bool advance_suite_restart(RuntimeSuite& suite, const SuiteManifest& manifest,
                                         std::uint64_t now) {
    if (!suite.suite_restart_pending) {
        return false;
    }
    if (now >= suite.next_suite_restart_ns) {
        suite.suite_restart_pending = false;
        suspend_execution_manager_logging();
        launch_all(suite);
        static_cast<void>(resume_execution_manager_logging());
        apply_launch_failures(suite);
        log_execution_manager(core::LogLevel::Info,
                              std::format("event=suite_restart suite={} used={} limit={}",
                                          manifest.suite_id, suite.suite_restarts_used,
                                          manifest.suite_restart_attempts));
        for (const RuntimeApp& app : suite.apps) {
            log_execution_manager(core::LogLevel::Info,
                                  std::format("event=app_launch app={} instance={} "
                                              "attempt={} pid={} state={}",
                                              app.spec.app, app.spec.instance, app.attempt,
                                              app.child_pid, nexigen::exec::to_string(app.state)));
        }
        write_suite_status(suite);
    }
    return true;
}

/// Advance one application state machine. Returns true when an abnormal exit
/// scheduled a suite-wide restart, so the caller stops scanning this tick.
[[nodiscard]] bool advance_app(RuntimeSuite& suite, RuntimeApp& app, std::uint64_t now) {
    if (app.state == AppState::Running) {
        ExitMetadata exit{};
        if (read_exit_metadata(app.instance_dir, exit) && !app.exit_consumed) {
            app.exit_consumed = true;
            if (exit.exit_code.has_value()) {
                log_execution_manager(core::LogLevel::Info,
                                      std::format("event=app_exit app={} instance={} "
                                                  "attempt={} exit_code={}",
                                                  app.spec.app, app.spec.instance, app.attempt,
                                                  *exit.exit_code));
            } else if (exit.signal.has_value()) {
                log_execution_manager(core::LogLevel::Warning,
                                      std::format("event=app_exit app={} instance={} "
                                                  "attempt={} signal={}",
                                                  app.spec.app, app.spec.instance, app.attempt,
                                                  *exit.signal));
            }
            if (exit.exit_code.has_value() && *exit.exit_code == 0) {
                app.state = AppState::Exited;
                suite.dirty = true;
            } else {
                handle_abnormal(suite, app);
                return suite.suite_restart_pending;
            }
        }
    } else if (app.state == AppState::Restarting && app.next_restart_ns != 0 &&
               now >= app.next_restart_ns) {
        app.launch_failed = false;
        suspend_execution_manager_logging();
        const bool launched = launch_app(suite, app);
        static_cast<void>(resume_execution_manager_logging());
        if (!launched) {
            app.launch_failed = false;
            log_execution_manager(core::LogLevel::Warning,
                                  std::format("event=app_launch_failed app={} "
                                              "instance={} attempt={}",
                                              app.spec.app, app.spec.instance, app.attempt));
            schedule_retry_or_fail(suite, app);
        } else {
            log_execution_manager(core::LogLevel::Info,
                                  std::format("event=app_launch app={} instance={} "
                                              "attempt={} pid={} state=running",
                                              app.spec.app, app.spec.instance, app.attempt,
                                              app.child_pid));
        }
        suite.dirty = true;
    }
    return false;
}

void advance_suite_apps(RuntimeSuite& suite, std::uint64_t now) {
    for (RuntimeApp& app : suite.apps) {
        if (advance_app(suite, app, now)) {
            break;
        }
    }
}

void apply_suite_control_thread(const SuiteManifest& manifest) noexcept {
    core::ThreadApplyReport report{};
    const core::Status status = apply_execution_manager_thread_spec(
        manifest.execution_manager_control_thread, report, "suite-supervisor");
    if (!status) {
        const std::string detail = describe_thread_apply_report(report);
        std::fprintf(stderr, "nexigen-exec: control thread spec application failed: %s\n",
                     detail.c_str());
        if (manifest.execution_manager_control_thread.apply_mode() ==
            core::ThreadApplyMode::Required) {
            child_exit(1); // no application has been launched at this point
        }
    }
}

[[nodiscard]] SuiteMetadata build_suite_metadata(const SuiteManifest& manifest, std::int32_t pid,
                                                 std::uint64_t start_mono_ns) {
    SuiteMetadata metadata{};
    metadata.suite_id = manifest.suite_id;
    metadata.execution_manager_name = manifest.execution_manager_name;
    metadata.run_id = manifest.run_id;
    metadata.run_dir = manifest.run_dir;
    metadata.global_settings_path = manifest.global_settings_path;
    metadata.artifacts_path = manifest.artifacts_path;
    metadata.pid = pid;
    metadata.start_time_rfc3339 = utc_now_rfc3339();
    metadata.start_time_mono_ns = start_mono_ns;
    // Identity anchor for suite stop (PID-recycling protection): the
    // supervisor records its own starttime ticks while alive right here.
    metadata.proc_start_ticks = detail::proc_start_ticks(pid);
    metadata.manifest_path = manifest.manifest_path;
    metadata.execution_manager_control_thread = manifest.execution_manager_control_thread;
    metadata.execution_manager_logger_thread = manifest.execution_manager_logger_thread;
    metadata.apps.reserve(manifest.apps.size());
    for (const AppSpec& app : manifest.apps) {
        SuiteAppMetadata entry{};
        entry.app = app.app;
        entry.instance = app.instance;
        entry.instance_dir = instance_dir_path(manifest.run_dir, app.app, app.instance);
        entry.stop_grace_s = app.stop_grace_s;
        metadata.apps.push_back(std::move(entry));
    }
    return metadata;
}

void write_suite_metadata_file(const SuiteMetadata& metadata, const std::string& suite_dir,
                               int report_fd) {
    nlohmann::json metadata_json;
    to_json(metadata_json, metadata);
    if (!write_json_file(join_path(suite_dir, "suite.json"), metadata_json)) {
        StartReport report{0, static_cast<std::int32_t>(core::ErrorCode::SystemError)};
        static_cast<void>(write_start_report(report_fd, report));
        static_cast<void>(::close(report_fd));
        child_exit(1);
    }
}

void initialize_runtime_apps(RuntimeSuite& suite, const SuiteManifest& manifest) {
    suite.apps.reserve(manifest.apps.size());
    for (const AppSpec& app : manifest.apps) {
        RuntimeApp runtime{};
        runtime.spec = app;
        runtime.instance_dir = instance_dir_path(manifest.run_dir, app.app, app.instance);
        suite.apps.push_back(std::move(runtime));
    }
}

void init_suite_logging(const SuiteManifest& manifest) {
    logging::Settings log_settings{};
    const core::Status settings_status =
        load_execution_manager_logging_settings(manifest.global_settings_path, log_settings);
    if (!settings_status) {
        warn_execution_manager("event=logging_settings_invalid detail=using defaults");
        log_settings = logging::Settings{};
    }
    const core::LogLevel log_level = manifest.log_level.value_or(
        log_settings.global_loaded ? log_settings.options.initial_log_level : core::LogLevel::Info);
    const core::Status log_status = init_execution_manager_logging(
        join_path(manifest.artifacts_path, manifest.suite_id), log_settings, log_level,
        manifest.execution_manager_logger_thread, manifest.execution_manager_name);
    if (!log_status) {
        warn_execution_manager(
            "event=logging_unavailable detail=execution_manager.jsonl not writable");
    }
}

void log_initial_app_launches(const RuntimeSuite& suite) {
    log_execution_manager(core::LogLevel::Info,
                          std::format("event=suite_started suite={} apps={}",
                                      suite.manifest->suite_id, suite.apps.size()));
    for (const RuntimeApp& app : suite.apps) {
        log_execution_manager(app.state == AppState::Failed ? core::LogLevel::Warning
                                                            : core::LogLevel::Info,
                              std::format("event=app_launch app={} instance={} attempt={} "
                                          "pid={} state={}",
                                          app.spec.app, app.spec.instance, app.attempt,
                                          app.child_pid, nexigen::exec::to_string(app.state)));
    }
}

void supervisor_tick(RuntimeSuite& suite, const SuiteManifest& manifest, std::uint64_t now) {
    if (file_exists(suite.stop_path)) {
        finish_suite_stop(suite, manifest);
    }
    reap_supervisors();

    if (!advance_suite_restart(suite, manifest, now)) {
        advance_suite_apps(suite, now);
    }

    if (suite.dirty) {
        write_suite_status(suite);
        suite.dirty = false;
    }
}

void run_suite_supervisor(const SuiteManifest& manifest, const std::string& suite_dir,
                          int report_fd) {
    static_cast<void>(reset_execution_manager_affinity());
    apply_suite_control_thread(manifest);

    RuntimeSuite suite{};
    suite.manifest = &manifest;
    suite.suite_dir = suite_dir;
    suite.state_path = join_path(suite_dir, "suite-state.json");
    suite.exit_path = join_path(suite_dir, "suite-exit.json");
    suite.stop_path = join_path(suite_dir, "stop.request");
    suite.start_mono_ns = monotonic_now_ns();

    const SuiteMetadata metadata = build_suite_metadata(manifest, ::getpid(), suite.start_mono_ns);
    write_suite_metadata_file(metadata, suite_dir, report_fd);

    initialize_runtime_apps(suite, manifest);
    launch_all(suite);
    write_suite_status(suite);

    StartReport report{::getpid(), 0};
    static_cast<void>(write_start_report(report_fd, report));
    static_cast<void>(::close(report_fd));

    init_suite_logging(manifest);
    log_initial_app_launches(suite);

    apply_launch_failures(suite);
    write_suite_status(suite);

    for (;;) {
        const std::uint64_t now = monotonic_now_ns();
        supervisor_tick(suite, manifest, now);
        sleep_ms(suite_poll_interval_ms);
    }
}

} // namespace

[[nodiscard]] core::Status preflight_suite_threads(const SuiteManifest& manifest) noexcept {
    const auto preflight = [](const core::ThreadSpec& spec) -> core::Status {
        if (spec.apply_mode() != core::ThreadApplyMode::Required) {
            return {};
        }
        return core::probe_thread_spec(spec);
    };
    if (const core::Status control = preflight(manifest.execution_manager_control_thread);
        !control) {
        return control;
    }
    if (const core::Status logger = preflight(manifest.execution_manager_logger_thread); !logger) {
        return logger;
    }
    return {};
}

[[nodiscard]] bool suite_is_running(const SuiteManifest& manifest,
                                    std::string& existing_suite_dir) {
    const std::string suite_dir = suite_dir_path(manifest.run_dir, manifest.suite_id);
    existing_suite_dir = suite_dir;
    const std::string metadata_path = join_path(suite_dir, "suite.json");
    const std::string exit_path = join_path(suite_dir, "suite-exit.json");
    if (!file_exists(metadata_path) || file_exists(exit_path)) {
        return false;
    }
    SuiteMetadata existing{};
    if (!read_json_into(metadata_path, existing)) {
        return false;
    }
    return process_alive(existing.pid);
}

[[nodiscard]] core::Status create_suite_pipes(int pipe_fds[2]) noexcept {
    if (::pipe2(pipe_fds, O_CLOEXEC) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

[[noreturn]] void run_suite_supervisor_child(const SuiteManifest& manifest,
                                             const std::string& suite_dir, int read_fd,
                                             int report_fd) noexcept {
    static_cast<void>(::close(read_fd)); // close the read end inherited from parent
    static_cast<void>(reset_execution_manager_affinity());
    static_cast<void>(::setsid());
    const int null_fd = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    if (null_fd >= 0) {
        redirect_to_null(null_fd);
        if (null_fd > STDERR_FILENO) {
            static_cast<void>(::close(null_fd));
        }
    }
    run_suite_supervisor(manifest, suite_dir, report_fd);
    child_exit(0);
}

core::Status start_suite(const SuiteManifest& manifest, std::string& suite_dir_out) {
    if (const core::Status valid = validate_suite_manifest(manifest); !valid) {
        return valid;
    }
    if (const core::Status threads = preflight_suite_threads(manifest); !threads) {
        return threads;
    }

    std::string suite_dir;
    if (suite_is_running(manifest, suite_dir)) {
        suite_dir_out = suite_dir;
        return std::unexpected(core::ErrorCode::Conflict);
    }
    suite_dir_out = suite_dir;
    const std::string exit_path = join_path(suite_dir, "suite-exit.json");
    const std::string stop_path = join_path(suite_dir, "stop.request");

    if (const core::Status created = create_artifact_dir(suite_dir); !created) {
        return created;
    }
    static_cast<void>(::unlink(exit_path.c_str()));
    static_cast<void>(::unlink(stop_path.c_str()));

    int pipe_fds[2]{-1, -1};
    if (const core::Status pipes = create_suite_pipes(pipe_fds); !pipes) {
        return pipes;
    }

    const pid_t supervisor = ::fork();
    if (supervisor < 0) {
        static_cast<void>(::close(pipe_fds[0]));
        static_cast<void>(::close(pipe_fds[1]));
        return std::unexpected(core::ErrorCode::SystemError);
    }

    if (supervisor == 0) {
        run_suite_supervisor_child(manifest, suite_dir, pipe_fds[0], pipe_fds[1]);
    }

    static_cast<void>(::close(pipe_fds[1]));
    return read_start_report(pipe_fds[0], supervisor);
}

[[nodiscard]] bool write_stop_request(const std::string& stop_path) noexcept {
    const int fd = ::open(stop_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }
    static_cast<void>(::close(fd));
    return true;
}

[[nodiscard]] std::uint64_t
effective_stop_grace(const SuiteMetadata& metadata,
                     const std::optional<std::uint64_t>& grace_override) noexcept {
    if (grace_override.has_value()) {
        return *grace_override;
    }
    if (metadata.apps.empty()) {
        return 5;
    }
    std::uint64_t grace = metadata.apps.front().stop_grace_s;
    for (const SuiteAppMetadata& app : metadata.apps) {
        if (app.stop_grace_s > grace) {
            grace = app.stop_grace_s;
        }
    }
    return grace;
}

[[nodiscard]] bool wait_for_suite_exit(const std::string& exit_path,
                                       std::uint64_t deadline_ns) noexcept {
    while (monotonic_now_ns() < deadline_ns && !file_exists(exit_path)) {
        sleep_ms(suite_poll_interval_ms);
    }
    return file_exists(exit_path);
}

void signal_supervisor_with_grace(std::int32_t pid) noexcept {
    if (pid <= 0 || !process_alive(pid)) {
        return;
    }
    static_cast<void>(::kill(static_cast<pid_t>(pid), SIGTERM));
    const std::uint64_t kill_deadline = monotonic_now_ns() + 2ULL * nanoseconds_per_second;
    while (monotonic_now_ns() < kill_deadline && process_alive(pid)) {
        sleep_ms(suite_poll_interval_ms);
    }
    if (process_alive(pid)) {
        static_cast<void>(::kill(static_cast<pid_t>(pid), SIGKILL));
    }
}

void stop_suite_apps(const std::vector<SuiteAppMetadata>& apps,
                     const std::optional<std::uint64_t>& grace_override) noexcept {
    for (const SuiteAppMetadata& app : apps) {
        ExecutionManager execution_manager;
        static_cast<void>(
            execution_manager.stop(app.instance_dir, grace_override.value_or(app.stop_grace_s)));
    }
}

void write_terminal_suite_state(const SuiteMetadata& metadata, const std::string& state_path,
                                const std::string& exit_path) {
    SuiteStatus status{};
    if (!read_json_into(state_path, status)) {
        status.suite_id = metadata.suite_id;
        status.run_dir = metadata.run_dir;
        status.artifacts_path = metadata.artifacts_path;
        status.apps.reserve(metadata.apps.size());
        for (const SuiteAppMetadata& app : metadata.apps) {
            SuiteAppStatus entry{};
            entry.app = app.app;
            entry.instance = app.instance;
            entry.instance_dir = app.instance_dir;
            entry.state = AppState::Stopped;
            status.apps.push_back(std::move(entry));
        }
    }
    status.state = SuiteState::Stopped;
    for (SuiteAppStatus& app : status.apps) {
        app.state = AppState::Stopped;
    }
    nlohmann::json status_json;
    to_json(status_json, status);
    static_cast<void>(write_json_file(state_path, status_json));

    SuiteExit exit{};
    exit.state = SuiteState::Stopped;
    exit.reason = "forced_stop";
    nlohmann::json exit_json;
    to_json(exit_json, exit);
    static_cast<void>(write_json_file(exit_path, exit_json));
}

core::Status stop_suite(std::string_view suite_dir,
                        std::optional<std::uint64_t> grace_override) noexcept {
    if (suite_dir.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const std::string directory(suite_dir);
    const std::string metadata_path = join_path(directory, "suite.json");
    const std::string exit_path = join_path(directory, "suite-exit.json");
    const std::string state_path = join_path(directory, "suite-state.json");
    const std::string stop_path = join_path(directory, "stop.request");

    if (file_exists(exit_path)) {
        return {};
    }

    SuiteMetadata metadata{};
    if (!read_json_into(metadata_path, metadata)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    if (!write_stop_request(stop_path)) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    const std::uint64_t grace = effective_stop_grace(metadata, grace_override);
    const std::uint64_t deadline =
        monotonic_now_ns() + grace * nanoseconds_per_second + 5ULL * nanoseconds_per_second;
    if (wait_for_suite_exit(exit_path, deadline)) {
        return {};
    }

    // Identity gate (spec process-supervision "Identity is verified
    // before signals are sent"): a recycled PID recorded in suite.json
    // must not receive a fallback SIGTERM/SIGKILL; the flow continues
    // into the normal instance-stop + suite finalize path.
    if (identity_matches(metadata.pid, metadata.proc_start_ticks) && metadata.pid > 0 &&
        process_alive(metadata.pid)) {
        signal_supervisor_with_grace(metadata.pid);
    }
    stop_suite_apps(metadata.apps, grace_override);
    write_terminal_suite_state(metadata, state_path, exit_path);
    return {};
}

[[nodiscard]] bool load_suite_status_metadata(const std::string& metadata_path,
                                              SuiteMetadata& out) noexcept {
    return read_json_into(metadata_path, out);
}

[[nodiscard]] SuiteState derive_suite_state(const std::string& exit_path,
                                            const SuiteStatus& recorded,
                                            bool have_recorded) noexcept {
    SuiteExit exit{};
    if (read_json_into(exit_path, exit)) {
        return exit.state;
    }
    if (have_recorded) {
        return recorded.state;
    }
    return SuiteState::Unknown;
}

[[nodiscard]] AppState live_status_to_app_state(std::string_view state, AppState recorded,
                                                bool have_recorded) noexcept {
    if (state == "running") {
        return AppState::Running;
    }
    if (state == "stopped") {
        return AppState::Stopped;
    }
    if (state == "exited") {
        return AppState::Exited;
    }
    if (!have_recorded || recorded == AppState::Unknown) {
        return AppState::Unknown;
    }
    return recorded;
}

[[nodiscard]] SuiteAppStatus merge_app_status(const SuiteAppMetadata& app,
                                              const SuiteStatus& recorded,
                                              bool have_recorded) noexcept {
    SuiteAppStatus entry{};
    entry.app = app.app;
    entry.instance = app.instance;
    entry.instance_dir = app.instance_dir;
    for (const SuiteAppStatus& recorded_app : recorded.apps) {
        if (recorded_app.app == app.app && recorded_app.instance == app.instance) {
            entry = recorded_app;
            break;
        }
    }
    const bool sticky = entry.state == AppState::Failed || entry.state == AppState::Restarting;
    if (!sticky) {
        std::string state;
        ExecutionManager execution_manager;
        if (execution_manager.status(app.instance_dir, state).has_value()) {
            entry.state = live_status_to_app_state(state, entry.state, have_recorded);
        }
    }
    return entry;
}

core::Status suite_status(std::string_view suite_dir, SuiteStatus& out) noexcept {
    out = SuiteStatus{};
    if (suite_dir.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const std::string directory(suite_dir);
    const std::string metadata_path = join_path(directory, "suite.json");
    const std::string state_path = join_path(directory, "suite-state.json");
    const std::string exit_path = join_path(directory, "suite-exit.json");

    SuiteMetadata metadata{};
    if (!load_suite_status_metadata(metadata_path, metadata)) {
        return {};
    }

    out.suite_id = metadata.suite_id;
    out.run_dir = metadata.run_dir;
    out.artifacts_path = metadata.artifacts_path;

    SuiteStatus recorded{};
    const bool have_recorded = read_json_into(state_path, recorded);

    out.state = derive_suite_state(exit_path, recorded, have_recorded);
    if (have_recorded) {
        out.suite_restarts_used = recorded.suite_restarts_used;
    }

    out.apps.reserve(metadata.apps.size());
    for (const SuiteAppMetadata& app : metadata.apps) {
        out.apps.push_back(merge_app_status(app, recorded, have_recorded));
    }
    return {};
}

} // namespace nexigen::exec
