// nexigen::runner — application suite manifest and supervision
// (change nexigen-run-app-suite; spec runner/app-suite).
//
// COLD PATH ONLY. The suite supervisor is a detached process that launches
// one existing per-instance supervisor per application, polls instance files,
// and applies restart/impact policy.
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/app/telemetry_layout.hpp>
#include <nexigen/chrono/time_format.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/log_level.hpp>
#include <nexigen/runner/detail/sys_helpers.hpp>
#include <nexigen/runner/app_suite.hpp>
#include <nexigen/runner/instance_lifecycle.hpp>
#include <nexigen/runner/runner.hpp>
#include <nexigen/runner/runner_logging.hpp>

#include "coverage_flush.hpp"

namespace nexigen::runner {

using detail::enum_from_json;
using detail::enum_to_json;
using detail::file_exists;
using detail::group_alive;
using detail::identity_matches;
using detail::monotonic_now_ns;
using detail::nanoseconds_per_second;
using detail::proc_start_ticks;
using detail::process_alive;
using detail::reap_failed_supervisor;
using detail::read_all;
using detail::read_enum;
using detail::read_i32;
using detail::read_string;
using detail::read_u32;
using detail::read_u64;
using detail::sleep_ms;
using detail::StartReport;
using detail::utc_now_rfc3339;
using detail::write_all;
using detail::write_all_no_sigpipe;

namespace {

constexpr std::size_t max_path_bytes = 511;
constexpr std::size_t max_apps = 256;
constexpr std::uint32_t max_restart_attempts = 1000;

/// Flush coverage counters before the detached supervisor leaves the process.
[[noreturn]] void child_exit(int code) noexcept {
    detail::coverage_flush();
    _exit(code);
}
constexpr std::uint64_t max_delay_seconds = 86400;
constexpr std::uint64_t suite_poll_interval_ms = 100;

// --- small string/path helpers ---------------------------------------------

[[nodiscard]] bool safe_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 128U) {
        return false;
    }
    if (id == "." || id == ".." || id.find("..") != std::string_view::npos) {
        return false;
    }
    for (const char c : id) {
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                             (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool absolute_path(std::string_view path) noexcept {
    return !path.empty() && path.front() == '/';
}

[[nodiscard]] bool has_parent_segment(std::string_view path) noexcept {
    for (std::size_t pos = 0; pos < path.size();) {
        const std::size_t slash = path.find('/', pos);
        const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
        if (path.substr(pos, end - pos) == "..") {
            return true;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        pos = slash + 1;
    }
    return false;
}

[[nodiscard]] std::string current_directory() {
    char buffer[4096]{};
    if (::getcwd(buffer, sizeof(buffer)) == nullptr) {
        return {};
    }
    return std::string(buffer);
}

[[nodiscard]] std::string absolute_from_cwd(std::string_view path) {
    if (absolute_path(path)) {
        return std::string(path);
    }
    const std::string base = current_directory();
    if (base.empty()) {
        return std::string(path);
    }
    return base + "/" + std::string(path);
}

[[nodiscard]] std::string directory_of(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos) {
        return std::string(".");
    }
    if (slash == 0) {
        return std::string("/");
    }
    return std::string(path.substr(0, slash));
}

[[nodiscard]] std::string join_path(std::string_view left, std::string_view right) {
    return std::string(left) + "/" + std::string(right);
}

// --- JSON helpers ----------------------------------------------------------

[[nodiscard]] bool read_log_level(const nlohmann::json& document, const char* key,
                                  std::optional<logging::LogLevel>& target) noexcept {
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
    logging::LogLevel level = logging::LogLevel::Info;
    if (!logging::from_string(std::string_view(*name), level)) {
        return false;
    }
    target = level;
    return true;
}

[[nodiscard]] nlohmann::json log_level_json(const std::optional<logging::LogLevel>& level) {
    if (!level.has_value()) {
        return nullptr;
    }
    return std::string(logging::to_string(*level));
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

void read_legacy_runner_cpus(const nlohmann::json& document, const char* key,
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
                       nexigen::runner::to_string(impact), global_settings_path,
                       local_settings_path,
                       log_level.has_value() ? std::string(logging::to_string(*log_level))
                                             : std::string("inherit"));
}

std::string SuiteManifest::to_string() const {
    return std::format("SuiteManifest(version={}, suite_id={}, run_id={}, run_dir={}, "
                       "global_settings_path={}, artifacts_path={}, log_level={}, "
                       "suite_restart_attempts={}, suite_restart_delay_s={}, apps={})",
                       version, suite_id, run_id, run_dir, global_settings_path, artifacts_path,
                       log_level.has_value() ? std::string(logging::to_string(*log_level))
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
                       nexigen::runner::to_string(state), reason, uptime_s);
}

std::string SuiteAppStatus::to_string() const {
    return std::format("SuiteAppStatus(app={}, instance={}, instance_dir={}, "
                       "artifact_dir={}, attempt={}, state={}, restarts_used={})",
                       app, instance, instance_dir, artifact_dir, attempt,
                       nexigen::runner::to_string(state), restarts_used);
}

std::string SuiteStatus::to_string() const {
    return std::format("SuiteStatus(state={}, suite_id={}, run_dir={}, artifacts_path={}, "
                       "suite_restarts_used={}, apps={})",
                       nexigen::runner::to_string(state), suite_id, run_dir, artifacts_path,
                       suite_restarts_used, apps.size());
}

// --- enum JSON -------------------------------------------------------------

void to_json(nlohmann::json& j, ImpactOnAppSuiteOperability value) {
    enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, ImpactOnAppSuiteOperability& value) noexcept {
    enum_from_json(j, value);
}

void to_json(nlohmann::json& j, SuiteState value) {
    enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, SuiteState& value) noexcept {
    enum_from_json(j, value);
}

void to_json(nlohmann::json& j, AppState value) {
    enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, AppState& value) noexcept {
    enum_from_json(j, value);
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
        {"runner_name", manifest.runner_name},
        {"run_id", manifest.run_id},
        {"run_dir", manifest.run_dir},
        {"global_settings_path", manifest.global_settings_path},
        {"artifacts_path", manifest.artifacts_path},
        {"log_level", log_level_json(manifest.log_level)},
        {"suite_restart_attempts", manifest.suite_restart_attempts},
        {"suite_restart_delay_s", manifest.suite_restart_delay_s},
        {"manifest_path", manifest.manifest_path},
        {"runner_control_thread", manifest.runner_control_thread},
        {"runner_logger_thread", manifest.runner_logger_thread},
        {"apps", std::move(apps)},
    };
}

void from_json(const nlohmann::json& j, SuiteManifest& manifest) noexcept {
    static_cast<void>(read_u32(j, "version", manifest.version));
    static_cast<void>(read_string(j, "suite_id", manifest.suite_id));
    static_cast<void>(read_string(j, "runner_name", manifest.runner_name));
    static_cast<void>(read_string(j, "run_id", manifest.run_id));
    static_cast<void>(read_string(j, "run_dir", manifest.run_dir));
    static_cast<void>(read_string(j, "global_settings_path", manifest.global_settings_path));
    static_cast<void>(read_string(j, "artifacts_path", manifest.artifacts_path));
    static_cast<void>(read_log_level(j, "log_level", manifest.log_level));
    static_cast<void>(read_u32(j, "suite_restart_attempts", manifest.suite_restart_attempts));
    static_cast<void>(read_u64(j, "suite_restart_delay_s", manifest.suite_restart_delay_s));
    static_cast<void>(read_string(j, "manifest_path", manifest.manifest_path));
    read_legacy_runner_cpus(j, "runner_control_cpus", manifest.runner_control_thread);
    read_legacy_runner_cpus(j, "runner_logger_cpus", manifest.runner_logger_thread);
    read_canonical_thread(j, "runner_control_thread", manifest.runner_control_thread);
    read_canonical_thread(j, "runner_logger_thread", manifest.runner_logger_thread);
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
        {"runner_name", metadata.runner_name},
        {"run_id", metadata.run_id},
        {"run_dir", metadata.run_dir},
        {"global_settings_path", metadata.global_settings_path},
        {"artifacts_path", metadata.artifacts_path},
        {"pid", metadata.pid},
        {"start_time_rfc3339", metadata.start_time_rfc3339},
        {"start_time_mono_ns", metadata.start_time_mono_ns},
        {"proc_start_ticks", metadata.proc_start_ticks},
        {"manifest_path", metadata.manifest_path},
        {"runner_control_thread", metadata.runner_control_thread},
        {"runner_logger_thread", metadata.runner_logger_thread},
        {"apps", std::move(apps)},
    };
}

void from_json(const nlohmann::json& j, SuiteMetadata& metadata) noexcept {
    static_cast<void>(read_string(j, "suite_id", metadata.suite_id));
    static_cast<void>(read_string(j, "runner_name", metadata.runner_name));
    static_cast<void>(read_string(j, "run_id", metadata.run_id));
    static_cast<void>(read_string(j, "run_dir", metadata.run_dir));
    static_cast<void>(read_string(j, "global_settings_path", metadata.global_settings_path));
    static_cast<void>(read_string(j, "artifacts_path", metadata.artifacts_path));
    static_cast<void>(read_i32(j, "pid", metadata.pid));
    static_cast<void>(read_string(j, "start_time_rfc3339", metadata.start_time_rfc3339));
    static_cast<void>(read_u64(j, "start_time_mono_ns", metadata.start_time_mono_ns));
    static_cast<void>(read_u64(j, "proc_start_ticks", metadata.proc_start_ticks));
    static_cast<void>(read_string(j, "manifest_path", metadata.manifest_path));
    read_legacy_runner_cpus(j, "runner_control_cpus", metadata.runner_control_thread);
    read_legacy_runner_cpus(j, "runner_logger_cpus", metadata.runner_logger_thread);
    read_canonical_thread(j, "runner_control_thread", metadata.runner_control_thread);
    read_canonical_thread(j, "runner_logger_thread", metadata.runner_logger_thread);
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

[[nodiscard]] bool is_valid_log_level(const std::optional<logging::LogLevel>& level) noexcept {
    if (!level.has_value()) {
        return true;
    }
    switch (*level) {
        case logging::LogLevel::Verbose:
        case logging::LogLevel::Debug:
        case logging::LogLevel::Info:
        case logging::LogLevel::Warning:
        case logging::LogLevel::Error:
        case logging::LogLevel::Fatal:
        case logging::LogLevel::Off:     return true;
        default:                         return false;
    }
}

[[nodiscard]] long online_cpu_count() noexcept {
    long online = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (online <= 0) {
        online = static_cast<long>(CPU_SETSIZE);
    }
    return online;
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

[[nodiscard]] core::Status
validate_app_environment(const std::vector<std::pair<std::string, std::string>>& env) noexcept {
    for (const auto& [key, value] : env) {
        if (key.empty() || key.find('=') != std::string::npos ||
            key.find('\0') != std::string::npos || value.find('\0') != std::string::npos) {
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

[[nodiscard]] core::Status validate_app_cpu_set(const AppSpec& app, long online) noexcept {
    for (std::uint32_t cpu = 0; cpu <= core::CpuSet::max_cpu_id; ++cpu) {
        if (app.cpus.contains(cpu) && (cpu >= static_cast<std::uint32_t>(online) ||
                                       cpu >= static_cast<std::uint32_t>(CPU_SETSIZE))) {
            return invalid_argument();
        }
    }
    return {};
}

[[nodiscard]] core::Status validate_app_paths(const AppSpec& app,
                                              const SuiteManifest& manifest) noexcept {
    const std::string instance_dir = manifest.run_dir + "/" + app.app + "/" + app.instance;
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

[[nodiscard]] core::Status validate_app_spec(const AppSpec& app, const SuiteManifest& manifest,
                                             long online) noexcept {
    if (const core::Status status = validate_app_identity_and_command(app); !status) {
        return status;
    }
    if (const core::Status status = validate_app_arguments(app); !status) {
        return status;
    }
    if (const core::Status status = validate_app_environment(app.env); !status) {
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
    if (const core::Status status = validate_app_cpu_set(app, online); !status) {
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
    const long online = online_cpu_count();
    for (const AppSpec& app : manifest.apps) {
        if (const core::Status status = validate_app_spec(app, manifest, online); !status) {
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
    logging::LogLevel level = logging::LogLevel::Info;
    return logging::from_string(std::string_view(*name), level);
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
    for (const char* key : {"runner_control_thread", "runner_logger_thread"}) {
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
    if (!valid_json_cpu_array(document, "runner_control_cpus") ||
        !valid_json_cpu_array(document, "runner_logger_cpus")) {
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

void redirect_to_null_local(int null_fd) noexcept {
    static_cast<void>(::dup2(null_fd, STDIN_FILENO));
    static_cast<void>(::dup2(null_fd, STDOUT_FILENO));
    static_cast<void>(::dup2(null_fd, STDERR_FILENO));
}

void reap_supervisors() noexcept {
    int status = 0;
    while (::waitpid(-1, &status, WNOHANG) > 0) {}
}

[[nodiscard]] bool read_exit_metadata(const std::string& instance_dir, ExitMetadata& out) noexcept {
    const core::Result<nlohmann::json> document =
        read_json_file(instance_file_path(instance_dir, "exit.json"));
    if (!document) {
        return false;
    }
    out = ExitMetadata{};
    from_json(*document, out);
    return true;
}

[[nodiscard]] bool read_resource_metadata(const std::string& instance_dir,
                                          ResourceMetadata& out) noexcept {
    const core::Result<nlohmann::json> document =
        read_json_file(instance_file_path(instance_dir, "resource.json"));
    if (!document) {
        return false;
    }
    out = ResourceMetadata{};
    from_json(*document, out);
    return true;
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
            Runner runner;
            static_cast<void>(runner.stop(app.instance_dir, app.spec.stop_grace_s));
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
    Runner runner;
    const core::Status started =
        runner.start(identity, app.spec.app, command, app.spec.idle_timeout_s, options);
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
        log_runner(logging::LogLevel::Info,
                   std::format("event=retry_scheduled app={} instance={} "
                               "attempt={} impact={} delay_s={}",
                               app.spec.app, app.spec.instance, app.attempt,
                               nexigen::runner::to_string(app.spec.impact),
                               app.spec.restart_delay_s));
    } else {
        app.state = AppState::Failed;
        if (app.spec.impact == ImpactOnAppSuiteOperability::Degrade) {
            suite.state = SuiteState::Degraded;
        }
        log_runner(logging::LogLevel::Warning,
                   std::format("event=app_failed app={} instance={} impact={} "
                               "suite_state={}",
                               app.spec.app, app.spec.instance,
                               nexigen::runner::to_string(app.spec.impact),
                               nexigen::runner::to_string(suite.state)));
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
    log_runner(logging::LogLevel::Error,
               std::format("event=suite_failed suite={} reason=restart_exhausted",
                           suite.manifest->suite_id));
    write_suite_exit(suite, SuiteState::Failed, "restart_exhausted");
    shutdown_runner_logging();
    child_exit(0);
}

void trigger_suite_restart(RuntimeSuite& suite) {
    if (suite.suite_restarts_used >= suite.manifest->suite_restart_attempts) {
        finish_failed(suite);
    }
    ++suite.suite_restarts_used;
    log_runner(logging::LogLevel::Warning,
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
    log_runner(logging::LogLevel::Info,
               std::format("event=stop_requested suite={}", manifest.suite_id));
    stop_all(suite);
    for (RuntimeApp& app : suite.apps) {
        app.state = AppState::Stopped;
    }
    write_suite_exit(suite, SuiteState::Stopped, "operator_stop");
    log_runner(logging::LogLevel::Info,
               std::format("event=suite_stopped suite={} reason=operator_stop", manifest.suite_id));
    shutdown_runner_logging();
    child_exit(0);
}

[[nodiscard]] bool advance_suite_restart(RuntimeSuite& suite, const SuiteManifest& manifest,
                                         std::uint64_t now) {
    if (!suite.suite_restart_pending) {
        return false;
    }
    if (now >= suite.next_suite_restart_ns) {
        suite.suite_restart_pending = false;
        suspend_runner_logging();
        launch_all(suite);
        static_cast<void>(resume_runner_logging());
        apply_launch_failures(suite);
        log_runner(logging::LogLevel::Info,
                   std::format("event=suite_restart suite={} used={} limit={}", manifest.suite_id,
                               suite.suite_restarts_used, manifest.suite_restart_attempts));
        for (const RuntimeApp& app : suite.apps) {
            log_runner(logging::LogLevel::Info,
                       std::format("event=app_launch app={} instance={} "
                                   "attempt={} pid={} state={}",
                                   app.spec.app, app.spec.instance, app.attempt, app.child_pid,
                                   nexigen::runner::to_string(app.state)));
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
                log_runner(logging::LogLevel::Info, std::format("event=app_exit app={} instance={} "
                                                                "attempt={} exit_code={}",
                                                                app.spec.app, app.spec.instance,
                                                                app.attempt, *exit.exit_code));
            } else if (exit.signal.has_value()) {
                log_runner(logging::LogLevel::Warning,
                           std::format("event=app_exit app={} instance={} "
                                       "attempt={} signal={}",
                                       app.spec.app, app.spec.instance, app.attempt, *exit.signal));
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
        suspend_runner_logging();
        const bool launched = launch_app(suite, app);
        static_cast<void>(resume_runner_logging());
        if (!launched) {
            app.launch_failed = false;
            log_runner(logging::LogLevel::Warning,
                       std::format("event=app_launch_failed app={} "
                                   "instance={} attempt={}",
                                   app.spec.app, app.spec.instance, app.attempt));
            schedule_retry_or_fail(suite, app);
        } else {
            log_runner(logging::LogLevel::Info,
                       std::format("event=app_launch app={} instance={} "
                                   "attempt={} pid={} state=running",
                                   app.spec.app, app.spec.instance, app.attempt, app.child_pid));
        }
        suite.dirty = true;
    }
    return false;
}

void run_suite_supervisor(const SuiteManifest& manifest, const std::string& suite_dir,
                          int report_fd) {
    static_cast<void>(reset_runner_affinity());
    core::ThreadApplyReport control_report{};
    const core::Status control_pin = apply_runner_thread_spec(manifest.runner_control_thread,
                                                              control_report, "suite-supervisor");
    if (!control_pin) {
        const std::string detail = describe_thread_apply_report(control_report);
        std::fprintf(stderr, "nexigen-run: control thread spec application failed: %s\n",
                     detail.c_str());
        if (manifest.runner_control_thread.apply_mode() == core::ThreadApplyMode::Required) {
            child_exit(1); // no application has been launched at this point
        }
    }

    RuntimeSuite suite{};
    suite.manifest = &manifest;
    suite.suite_dir = suite_dir;
    suite.state_path = join_path(suite_dir, "suite-state.json");
    suite.exit_path = join_path(suite_dir, "suite-exit.json");
    suite.stop_path = join_path(suite_dir, "stop.request");
    suite.start_mono_ns = monotonic_now_ns();

    SuiteMetadata metadata{};
    metadata.suite_id = manifest.suite_id;
    metadata.runner_name = manifest.runner_name;
    metadata.run_id = manifest.run_id;
    metadata.run_dir = manifest.run_dir;
    metadata.global_settings_path = manifest.global_settings_path;
    metadata.artifacts_path = manifest.artifacts_path;
    metadata.pid = ::getpid();
    metadata.start_time_rfc3339 = utc_now_rfc3339();
    metadata.start_time_mono_ns = suite.start_mono_ns;
    // Identity anchor for suite stop (PID-recycling protection): the
    // supervisor records its own starttime ticks while alive right here.
    metadata.proc_start_ticks = detail::proc_start_ticks(metadata.pid);
    metadata.manifest_path = manifest.manifest_path;
    metadata.runner_control_thread = manifest.runner_control_thread;
    metadata.runner_logger_thread = manifest.runner_logger_thread;
    metadata.apps.reserve(manifest.apps.size());
    for (const AppSpec& app : manifest.apps) {
        SuiteAppMetadata entry{};
        entry.app = app.app;
        entry.instance = app.instance;
        entry.instance_dir = manifest.run_dir + "/" + app.app + "/" + app.instance;
        entry.stop_grace_s = app.stop_grace_s;
        metadata.apps.push_back(std::move(entry));
    }
    nlohmann::json metadata_json;
    to_json(metadata_json, metadata);
    if (!write_json_file(join_path(suite_dir, "suite.json"), metadata_json)) {
        StartReport report{0, static_cast<std::int32_t>(core::ErrorCode::SystemError)};
        static_cast<void>(write_all_no_sigpipe(report_fd, &report, sizeof(report)));
        static_cast<void>(::close(report_fd));
        child_exit(1);
    }

    suite.apps.reserve(manifest.apps.size());
    for (const AppSpec& app : manifest.apps) {
        RuntimeApp runtime{};
        runtime.spec = app;
        runtime.instance_dir = manifest.run_dir + "/" + app.app + "/" + app.instance;
        suite.apps.push_back(std::move(runtime));
    }

    launch_all(suite);
    write_suite_status(suite);

    StartReport report{::getpid(), 0};
    static_cast<void>(write_all_no_sigpipe(report_fd, &report, sizeof(report)));
    static_cast<void>(::close(report_fd));

    logging::Settings log_settings{};
    const core::Status settings_status =
        load_runner_logging_settings(manifest.global_settings_path, log_settings);
    if (!settings_status) {
        warn_runner("event=logging_settings_invalid detail=using defaults");
        log_settings = logging::Settings{};
    }
    const logging::LogLevel log_level = manifest.log_level.value_or(
        log_settings.global_loaded ? log_settings.options.initial_log_level
                                   : logging::LogLevel::Info);
    const core::Status log_status =
        init_runner_logging(join_path(manifest.artifacts_path, manifest.suite_id), log_settings,
                            log_level, manifest.runner_logger_thread, manifest.runner_name);
    if (!log_status) {
        warn_runner("event=logging_unavailable detail=runner.jsonl not writable");
    }
    log_runner(logging::LogLevel::Info, std::format("event=suite_started suite={} apps={}",
                                                    manifest.suite_id, suite.apps.size()));
    for (const RuntimeApp& app : suite.apps) {
        log_runner(app.state == AppState::Failed ? logging::LogLevel::Warning
                                                 : logging::LogLevel::Info,
                   std::format("event=app_launch app={} instance={} attempt={} "
                               "pid={} state={}",
                               app.spec.app, app.spec.instance, app.attempt, app.child_pid,
                               nexigen::runner::to_string(app.state)));
    }

    apply_launch_failures(suite);
    write_suite_status(suite);

    for (;;) {
        if (file_exists(suite.stop_path)) {
            finish_suite_stop(suite, manifest);
        }
        reap_supervisors();

        const std::uint64_t now = monotonic_now_ns();
        if (advance_suite_restart(suite, manifest, now)) {
            sleep_ms(suite_poll_interval_ms);
            continue;
        }

        for (RuntimeApp& app : suite.apps) {
            if (advance_app(suite, app, now)) {
                break;
            }
        }

        if (suite.dirty) {
            write_suite_status(suite);
            suite.dirty = false;
        }
        sleep_ms(suite_poll_interval_ms);
    }
}

} // namespace

core::Status start_suite(const SuiteManifest& manifest, std::string& suite_dir_out) {
    const core::Status valid = validate_suite_manifest(manifest);
    if (!valid) {
        return valid;
    }
    // Required attributes are validated and exercised on a short-lived probe
    // before any directory, supervisor, or application is created. The actual
    // control/logger threads apply the same immutable spec again on startup.
    const auto preflight = [](const core::ThreadSpec& spec) -> core::Status {
        if (spec.apply_mode() != core::ThreadApplyMode::Required) {
            return {};
        }
        core::Status applied{};
        core::ThreadApplyReport report{};
        std::jthread probe([&] { applied = core::apply_thread_spec_self(spec, report); });
        probe.join();
        return applied;
    };
    if (const core::Status control = preflight(manifest.runner_control_thread); !control) {
        return control;
    }
    if (const core::Status logger = preflight(manifest.runner_logger_thread); !logger) {
        return logger;
    }

    const std::string suite_dir = suite_dir_path(manifest.run_dir, manifest.suite_id);
    suite_dir_out = suite_dir;

    const std::string metadata_path = join_path(suite_dir, "suite.json");
    const std::string exit_path = join_path(suite_dir, "suite-exit.json");
    const std::string stop_path = join_path(suite_dir, "stop.request");
    if (file_exists(metadata_path) && !file_exists(exit_path)) {
        SuiteMetadata existing{};
        const core::Result<nlohmann::json> document = read_json_file(metadata_path);
        if (document) {
            from_json(*document, existing);
            if (process_alive(existing.pid)) {
                return std::unexpected(core::ErrorCode::Conflict);
            }
        }
    }

    const core::Status created = create_artifact_dir(suite_dir);
    if (!created) {
        return created;
    }
    static_cast<void>(::unlink(exit_path.c_str()));
    static_cast<void>(::unlink(stop_path.c_str()));

    int pipe_fds[2]{-1, -1};
    if (::pipe2(pipe_fds, O_CLOEXEC) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    const pid_t supervisor = ::fork();
    if (supervisor < 0) {
        static_cast<void>(::close(pipe_fds[0]));
        static_cast<void>(::close(pipe_fds[1]));
        return std::unexpected(core::ErrorCode::SystemError);
    }

    if (supervisor == 0) {
        static_cast<void>(::close(pipe_fds[0]));
        static_cast<void>(reset_runner_affinity());
        static_cast<void>(::setsid());
        const int null_fd = ::open("/dev/null", O_RDWR | O_CLOEXEC);
        if (null_fd >= 0) {
            redirect_to_null_local(null_fd);
            if (null_fd > STDERR_FILENO) {
                static_cast<void>(::close(null_fd));
            }
        }
        run_suite_supervisor(manifest, suite_dir, pipe_fds[1]);
        child_exit(0);
    }

    static_cast<void>(::close(pipe_fds[1]));
    StartReport report{};
    const bool received = read_all(pipe_fds[0], &report, sizeof(report));
    static_cast<void>(::close(pipe_fds[0]));
    if (!received || report.error != 0 || report.pid <= 0) {
        // Failed start must not leave a zombie supervisor behind (spec
        // process-supervision "A failed start reaps the supervisor child":
        // bounded non-blocking drain).
        reap_failed_supervisor(supervisor);
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
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

    const core::Result<nlohmann::json> document = read_json_file(metadata_path);
    if (!document) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    SuiteMetadata metadata{};
    from_json(*document, metadata);

    const int fd = ::open(stop_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    static_cast<void>(::close(fd));

    std::uint64_t grace = 5;
    if (grace_override.has_value()) {
        grace = *grace_override;
    } else if (!metadata.apps.empty()) {
        grace = metadata.apps.front().stop_grace_s;
        for (const SuiteAppMetadata& app : metadata.apps) {
            if (app.stop_grace_s > grace) {
                grace = app.stop_grace_s;
            }
        }
    }

    const std::uint64_t deadline =
        monotonic_now_ns() + grace * nanoseconds_per_second + 5ULL * nanoseconds_per_second;
    while (monotonic_now_ns() < deadline && !file_exists(exit_path)) {
        sleep_ms(suite_poll_interval_ms);
    }

    if (!file_exists(exit_path)) {
        // Identity gate (spec process-supervision "Identity is verified
        // before signals are sent"): a recycled PID recorded in suite.json
        // must not receive a fallback SIGTERM/SIGKILL; the flow continues
        // into the normal instance-stop + suite finalize path.
        if (identity_matches(metadata.pid, metadata.proc_start_ticks) && metadata.pid > 0 &&
            process_alive(metadata.pid)) {
            static_cast<void>(::kill(metadata.pid, SIGTERM));
            const std::uint64_t kill_deadline = monotonic_now_ns() + 2ULL * nanoseconds_per_second;
            while (monotonic_now_ns() < kill_deadline && process_alive(metadata.pid)) {
                sleep_ms(suite_poll_interval_ms);
            }
            if (process_alive(metadata.pid)) {
                static_cast<void>(::kill(metadata.pid, SIGKILL));
            }
        }
        for (const SuiteAppMetadata& app : metadata.apps) {
            Runner runner;
            static_cast<void>(
                runner.stop(app.instance_dir, grace_override.value_or(app.stop_grace_s)));
        }

        SuiteStatus status{};
        const core::Result<nlohmann::json> existing = read_json_file(state_path);
        if (existing) {
            from_json(*existing, status);
        } else {
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

    return {};
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

    const core::Result<nlohmann::json> metadata_document = read_json_file(metadata_path);
    if (!metadata_document) {
        return {};
    }
    SuiteMetadata metadata{};
    from_json(*metadata_document, metadata);

    out.suite_id = metadata.suite_id;
    out.run_dir = metadata.run_dir;
    out.artifacts_path = metadata.artifacts_path;

    SuiteStatus recorded{};
    bool have_recorded = false;
    if (const core::Result<nlohmann::json> state_document = read_json_file(state_path)) {
        from_json(*state_document, recorded);
        have_recorded = true;
    }

    const core::Result<nlohmann::json> exit_document = read_json_file(exit_path);
    if (exit_document) {
        SuiteExit exit{};
        from_json(*exit_document, exit);
        out.state = exit.state;
    } else if (have_recorded) {
        out.state = recorded.state;
        out.suite_restarts_used = recorded.suite_restarts_used;
    } else {
        out.state = SuiteState::Unknown;
    }
    if (have_recorded) {
        out.suite_restarts_used = recorded.suite_restarts_used;
    }

    out.apps.reserve(metadata.apps.size());
    for (const SuiteAppMetadata& app : metadata.apps) {
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

        std::string state;
        Runner runner;
        const bool sticky = entry.state == AppState::Failed || entry.state == AppState::Restarting;
        if (!sticky && runner.status(app.instance_dir, state).has_value()) {
            if (state == "running") {
                entry.state = AppState::Running;
            } else if (state == "stopped") {
                entry.state = AppState::Stopped;
            } else if (state == "exited") {
                entry.state = AppState::Exited;
            } else if (!have_recorded || entry.state == AppState::Unknown) {
                entry.state = AppState::Unknown;
            }
        }
        out.apps.push_back(std::move(entry));
    }
    return {};
}

} // namespace nexigen::runner
