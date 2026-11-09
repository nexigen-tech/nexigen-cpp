// nexigen::exec — suite scenario resolution: manifest, paths, validation
// (change refactor-app-suite-layers; spec execution_manager/app-suite).
//
// COLD PATH ONLY. This translation unit parses, validates, and resolves suite
// manifests; it never creates or signals a process.
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/exec/app_spec.hpp>
#include <nexigen/exec/app_state.hpp>
#include <nexigen/exec/detail/cpu_validation.hpp>
#include <nexigen/exec/detail/path_utils.hpp>
#include <nexigen/exec/detail/sys_helpers.hpp>
#include <nexigen/exec/impact_on_app_suite_operability.hpp>
#include <nexigen/exec/instance_file_io.hpp>
#include <nexigen/exec/suite_app_metadata.hpp>
#include <nexigen/exec/suite_app_status.hpp>
#include <nexigen/exec/suite_exit.hpp>
#include <nexigen/exec/suite_manifest.hpp>
#include <nexigen/exec/suite_metadata.hpp>
#include <nexigen/exec/suite_paths.hpp>
#include <nexigen/exec/suite_status.hpp>

namespace nexigen::exec {

using detail::absolute_from_cwd;
using detail::absolute_path;
using detail::cpus_within_online;
using detail::has_parent_segment;
using detail::instance_dir_path;
using detail::max_path_bytes;
using core::mandate::read_enum;
using detail::read_i32;
using detail::read_string;
using detail::read_u32;
using detail::read_u64;
using detail::safe_id;
using detail::validate_env_entries;

namespace {

constexpr std::size_t max_apps = 256;
constexpr std::uint32_t max_restart_attempts = 1000;
constexpr std::uint64_t max_delay_seconds = 86400;

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
    return std::format(
        "AppSpec(app={}, instance={}, command={}, args={}, env={}, cpus={}, "
        "idle_timeout_s={}, stop_grace_s={}, restart_attempts={}, "
        "restart_delay_s={}, impact={}, global_settings_path={}, "
        "local_settings_path={}, log_level={})",
        app, instance, command, args.size(), env.size(), cpus.count(), idle_timeout_s, stop_grace_s,
        restart_attempts, restart_delay_s, nexigen::exec::to_string(impact), global_settings_path,
        local_settings_path,
        log_level.has_value() ? std::string(core::to_string(*log_level)) : std::string("inherit"));
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
    read_legacy_execution_manager_cpus(j, "execution_manager_control_cpus", manifest.execution_manager_control_thread);
    read_legacy_execution_manager_cpus(j, "execution_manager_logger_cpus", manifest.execution_manager_logger_thread);
    read_canonical_thread(j, "execution_manager_control_thread", manifest.execution_manager_control_thread);
    read_canonical_thread(j, "execution_manager_logger_thread", manifest.execution_manager_logger_thread);
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
    read_legacy_execution_manager_cpus(j, "execution_manager_control_cpus", metadata.execution_manager_control_thread);
    read_legacy_execution_manager_cpus(j, "execution_manager_logger_cpus", metadata.execution_manager_logger_thread);
    read_canonical_thread(j, "execution_manager_control_thread", metadata.execution_manager_control_thread);
    read_canonical_thread(j, "execution_manager_logger_thread", metadata.execution_manager_logger_thread);
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
    for (const char* key : {"execution_manager_control_thread", "execution_manager_logger_thread"}) {
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

} // namespace nexigen::exec
