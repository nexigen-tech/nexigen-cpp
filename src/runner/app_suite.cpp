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
#include <nexigen/runner/app_suite.hpp>
#include <nexigen/runner/instance_lifecycle.hpp>
#include <nexigen/runner/runner.hpp>
#include <nexigen/runner/runner_logging.hpp>

namespace nexigen::runner {

namespace {

constexpr std::size_t max_path_bytes = 511;
constexpr std::size_t max_apps = 256;
constexpr std::uint32_t max_restart_attempts = 1000;
constexpr std::uint64_t max_delay_seconds = 86400;
constexpr std::uint64_t suite_poll_interval_ms = 100;
constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;

// --- small string/path helpers ---------------------------------------------

[[nodiscard]] bool safe_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 128U) {
        return false;
    }
    if (id == "." || id == ".." ||
        id.find("..") != std::string_view::npos) {
        return false;
    }
    for (const char c : id) {
        const bool allowed = (c >= 'A' && c <= 'Z') ||
                             (c >= 'a' && c <= 'z') ||
                             (c >= '0' && c <= '9') || c == '.' ||
                             c == '_' || c == '-';
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

[[nodiscard]] std::string join_path(std::string_view left,
                                    std::string_view right) {
    return std::string(left) + "/" + std::string(right);
}

[[nodiscard]] bool file_exists(const std::string& path) noexcept {
    return ::access(path.c_str(), F_OK) == 0;
}

[[nodiscard]] bool process_alive(std::int32_t pid) noexcept {
    if (pid <= 0) {
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM) {
        return false;
    }
    char path[64]{};
    static_cast<void>(std::snprintf(path, sizeof(path), "/proc/%d/stat", pid));
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return true;
    }
    char buffer[512]{};
    const ssize_t count = ::read(fd, buffer, sizeof(buffer) - 1);
    static_cast<void>(::close(fd));
    if (count <= 0) {
        return true;
    }
    buffer[count] = '\0';
    const char* const last_paren = std::strrchr(buffer, ')');
    if (last_paren == nullptr || last_paren[1] != ' ') {
        return true;
    }
    const char state = last_paren[2];
    return state != 'Z' && state != 'X' && state != 'x';
}

[[nodiscard]] bool group_alive(std::int32_t pgid) noexcept {
    if (pgid <= 0) {
        return false;
    }
    if (::kill(-static_cast<pid_t>(pgid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

[[nodiscard]] std::uint64_t monotonic_now_ns() noexcept {
    const core::Result<nexigen::chrono::Timestamp> now =
        nexigen::chrono::Timestamp::now();
    return now ? now->nanoseconds() : 0;
}

[[nodiscard]] std::string utc_now_rfc3339() {
    using namespace std::chrono;
    const auto since_epoch = system_clock::now().time_since_epoch();
    const std::int64_t epoch_ns =
        duration_cast<nanoseconds>(since_epoch).count();
    char buffer[31]{};
    nexigen::chrono::format_utc_iso8601_ns(epoch_ns, buffer);
    return std::string(buffer);
}

void sleep_ms(std::uint64_t milliseconds) noexcept {
    ::timespec request{};
    request.tv_sec = static_cast<time_t>(milliseconds / 1000ULL);
    request.tv_nsec =
        static_cast<long>((milliseconds % 1000ULL) * 1'000'000ULL);
    while (::nanosleep(&request, &request) != 0 && errno == EINTR) {
    }
}

[[nodiscard]] bool write_all(int fd, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t written = 0;
    while (written < size) {
        const ssize_t count = ::write(fd, bytes + written, size - written);
        if (count <= 0) {
            if (count < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        written += static_cast<std::size_t>(count);
    }
    return true;
}

[[nodiscard]] bool read_all(int fd, void* data, std::size_t size) noexcept {
    auto* bytes = static_cast<char*>(data);
    std::size_t read_bytes = 0;
    while (read_bytes < size) {
        const ssize_t count = ::read(fd, bytes + read_bytes, size - read_bytes);
        if (count == 0) {
            return false;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        read_bytes += static_cast<std::size_t>(count);
    }
    return true;
}

// --- JSON helpers ----------------------------------------------------------

[[nodiscard]] bool read_string(const nlohmann::json& document,
                               const char* key, std::string& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_string()) {
        return false;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return false;
    }
    target = *text;
    return true;
}

[[nodiscard]] bool read_u32(const nlohmann::json& document, const char* key,
                            std::uint32_t& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_number_unsigned()) {
        return false;
    }
    const std::uint64_t value = it->get<std::uint64_t>();
    if (value > UINT32_MAX) {
        return false;
    }
    target = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] bool read_u64(const nlohmann::json& document, const char* key,
                            std::uint64_t& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_number_unsigned()) {
        return false;
    }
    target = it->get<std::uint64_t>();
    return true;
}

[[nodiscard]] bool read_i32(const nlohmann::json& document, const char* key,
                            std::int32_t& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_number_integer()) {
        return false;
    }
    const std::int64_t value = it->get<std::int64_t>();
    if (value < INT32_MIN || value > INT32_MAX) {
        return false;
    }
    target = static_cast<std::int32_t>(value);
    return true;
}

[[nodiscard]] bool read_log_level(const nlohmann::json& document,
                                  const char* key,
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

[[nodiscard]] nlohmann::json log_level_json(
    const std::optional<logging::LogLevel>& level) {
    if (!level.has_value()) {
        return nullptr;
    }
    return std::string(logging::to_string(*level));
}

[[nodiscard]] bool read_string_array(const nlohmann::json& document,
                                     const char* key,
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

[[nodiscard]] bool read_cpu_array(const nlohmann::json& document,
                                  const char* key,
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

[[nodiscard]] bool read_env_object(const nlohmann::json& document,
                                   const char* key,
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

std::string App_spec::to_string() const {
    return std::format(
        "App_spec(app={}, instance={}, command={}, args={}, env={}, cpus={}, "
        "idle_timeout_s={}, stop_grace_s={}, restart_attempts={}, "
        "restart_delay_s={}, impact={}, global_settings_path={}, "
        "local_settings_path={}, log_level={})",
        app, instance, command, args.size(), env.size(), cpus.size(),
        idle_timeout_s, stop_grace_s, restart_attempts, restart_delay_s,
        nexigen::runner::to_string(impact), global_settings_path,
        local_settings_path,
        log_level.has_value() ? std::string(logging::to_string(*log_level))
                              : std::string("inherit"));
}

std::string Suite_manifest::to_string() const {
    return std::format(
        "Suite_manifest(version={}, suite_id={}, run_id={}, run_dir={}, "
        "global_settings_path={}, artifacts_path={}, log_level={}, "
        "suite_restart_attempts={}, suite_restart_delay_s={}, apps={})",
        version, suite_id, run_id, run_dir, global_settings_path,
        artifacts_path,
        log_level.has_value() ? std::string(logging::to_string(*log_level))
                              : std::string("inherit"),
        suite_restart_attempts, suite_restart_delay_s, apps.size());
}

std::string Suite_app_metadata::to_string() const {
    return std::format("Suite_app_metadata(app={}, instance={}, instance_dir={}, "
                       "stop_grace_s={})",
                       app, instance, instance_dir, stop_grace_s);
}

std::string Suite_metadata::to_string() const {
    return std::format(
        "Suite_metadata(suite_id={}, run_id={}, run_dir={}, "
        "global_settings_path={}, artifacts_path={}, pid={}, "
        "start_time_rfc3339={}, manifest_path={}, apps={})",
        suite_id, run_id, run_dir, global_settings_path, artifacts_path, pid,
        start_time_rfc3339, manifest_path, apps.size());
}

std::string Suite_exit::to_string() const {
    return std::format("Suite_exit(state={}, reason={}, uptime_s={})",
                       nexigen::runner::to_string(state), reason, uptime_s);
}

std::string Suite_app_status::to_string() const {
    return std::format(
        "Suite_app_status(app={}, instance={}, instance_dir={}, "
        "artifact_dir={}, attempt={}, state={}, restarts_used={})",
        app, instance, instance_dir, artifact_dir, attempt,
        nexigen::runner::to_string(state), restarts_used);
}

std::string Suite_status::to_string() const {
    return std::format(
        "Suite_status(state={}, suite_id={}, run_dir={}, artifacts_path={}, "
        "suite_restarts_used={}, apps={})",
        nexigen::runner::to_string(state), suite_id, run_dir, artifacts_path,
        suite_restarts_used, apps.size());
}

// --- enum JSON -------------------------------------------------------------

void to_json(nlohmann::json& j, Impact_on_app_suite_operability value) {
    j = std::string(to_string(value));
}

void from_json(const nlohmann::json& j,
               Impact_on_app_suite_operability& value) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), value));
    }
}

void to_json(nlohmann::json& j, Suite_state value) {
    j = std::string(to_string(value));
}

void from_json(const nlohmann::json& j, Suite_state& value) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), value));
    }
}

void to_json(nlohmann::json& j, App_state value) {
    j = std::string(to_string(value));
}

void from_json(const nlohmann::json& j, App_state& value) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), value));
    }
}

// --- struct JSON -----------------------------------------------------------

void to_json(nlohmann::json& j, const App_spec& spec) {
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
        {"impact_on_app_suite_operability",
         std::string(to_string(spec.impact))},
        {"global_settings_path", spec.global_settings_path},
        {"local_settings_path", spec.local_settings_path},
        {"log_level", log_level_json(spec.log_level)},
    };
}

void from_json(const nlohmann::json& j, App_spec& spec) noexcept {
    static_cast<void>(read_string(j, "app", spec.app));
    static_cast<void>(read_string(j, "instance", spec.instance));
    static_cast<void>(read_string(j, "command", spec.command));
    static_cast<void>(read_string_array(j, "args", spec.args));
    static_cast<void>(read_env_object(j, "env", spec.env));
    static_cast<void>(read_cpu_array(j, "cpus", spec.cpus));
    static_cast<void>(read_u64(j, "idle_timeout_s", spec.idle_timeout_s));
    static_cast<void>(read_u64(j, "stop_grace_s", spec.stop_grace_s));
    static_cast<void>(read_u32(j, "restart_attempts", spec.restart_attempts));
    static_cast<void>(read_u64(j, "restart_delay_s", spec.restart_delay_s));
    if (const auto it = j.find("impact_on_app_suite_operability");
        it != j.end() && it->is_string()) {
        const auto* const name =
            it->get_ptr<const nlohmann::json::string_t*>();
        if (name != nullptr) {
            Impact_on_app_suite_operability parsed = spec.impact;
            if (from_string(std::string_view(*name), parsed)) {
                spec.impact = parsed;
            }
        }
    }
    static_cast<void>(read_string(j, "global_settings_path",
                                  spec.global_settings_path));
    static_cast<void>(read_string(j, "local_settings_path",
                                  spec.local_settings_path));
    static_cast<void>(read_log_level(j, "log_level", spec.log_level));
}

void to_json(nlohmann::json& j, const Suite_manifest& manifest) {
    nlohmann::json apps = nlohmann::json::array();
    for (const App_spec& app : manifest.apps) {
        nlohmann::json entry;
        to_json(entry, app);
        apps.push_back(std::move(entry));
    }
    j = nlohmann::json{
        {"version", manifest.version},
        {"suite_id", manifest.suite_id},
        {"run_id", manifest.run_id},
        {"run_dir", manifest.run_dir},
        {"global_settings_path", manifest.global_settings_path},
        {"artifacts_path", manifest.artifacts_path},
        {"log_level", log_level_json(manifest.log_level)},
        {"suite_restart_attempts", manifest.suite_restart_attempts},
        {"suite_restart_delay_s", manifest.suite_restart_delay_s},
        {"manifest_path", manifest.manifest_path},
        {"runner_control_cpus", manifest.runner_control_cpus},
        {"runner_logger_cpus", manifest.runner_logger_cpus},
        {"apps", std::move(apps)},
    };
}

void from_json(const nlohmann::json& j, Suite_manifest& manifest) noexcept {
    static_cast<void>(read_u32(j, "version", manifest.version));
    static_cast<void>(read_string(j, "suite_id", manifest.suite_id));
    static_cast<void>(read_string(j, "run_id", manifest.run_id));
    static_cast<void>(read_string(j, "run_dir", manifest.run_dir));
    static_cast<void>(read_string(j, "global_settings_path",
                                  manifest.global_settings_path));
    static_cast<void>(read_string(j, "artifacts_path",
                                  manifest.artifacts_path));
    static_cast<void>(read_log_level(j, "log_level", manifest.log_level));
    static_cast<void>(read_u32(j, "suite_restart_attempts",
                               manifest.suite_restart_attempts));
    static_cast<void>(read_u64(j, "suite_restart_delay_s",
                               manifest.suite_restart_delay_s));
    static_cast<void>(read_string(j, "manifest_path", manifest.manifest_path));
    static_cast<void>(read_cpu_array(j, "runner_control_cpus",
                                     manifest.runner_control_cpus));
    static_cast<void>(read_cpu_array(j, "runner_logger_cpus",
                                     manifest.runner_logger_cpus));
    if (const auto it = j.find("apps"); it != j.end() && it->is_array()) {
        std::vector<App_spec> apps;
        apps.reserve(it->size());
        for (const auto& entry : *it) {
            App_spec app;
            from_json(entry, app);
            apps.push_back(std::move(app));
        }
        manifest.apps = std::move(apps);
    }
}

void to_json(nlohmann::json& j, const Suite_app_metadata& app) {
    j = nlohmann::json{{"app", app.app},
                       {"instance", app.instance},
                       {"instance_dir", app.instance_dir},
                       {"stop_grace_s", app.stop_grace_s}};
}

void from_json(const nlohmann::json& j, Suite_app_metadata& app) noexcept {
    static_cast<void>(read_string(j, "app", app.app));
    static_cast<void>(read_string(j, "instance", app.instance));
    static_cast<void>(read_string(j, "instance_dir", app.instance_dir));
    static_cast<void>(read_u64(j, "stop_grace_s", app.stop_grace_s));
}

void to_json(nlohmann::json& j, const Suite_metadata& metadata) {
    nlohmann::json apps = nlohmann::json::array();
    for (const Suite_app_metadata& app : metadata.apps) {
        nlohmann::json entry;
        to_json(entry, app);
        apps.push_back(std::move(entry));
    }
    j = nlohmann::json{
        {"suite_id", metadata.suite_id},
        {"run_id", metadata.run_id},
        {"run_dir", metadata.run_dir},
        {"global_settings_path", metadata.global_settings_path},
        {"artifacts_path", metadata.artifacts_path},
        {"pid", metadata.pid},
        {"start_time_rfc3339", metadata.start_time_rfc3339},
        {"start_time_mono_ns", metadata.start_time_mono_ns},
        {"manifest_path", metadata.manifest_path},
        {"runner_control_cpus", metadata.runner_control_cpus},
        {"runner_logger_cpus", metadata.runner_logger_cpus},
        {"apps", std::move(apps)},
    };
}

void from_json(const nlohmann::json& j, Suite_metadata& metadata) noexcept {
    static_cast<void>(read_string(j, "suite_id", metadata.suite_id));
    static_cast<void>(read_string(j, "run_id", metadata.run_id));
    static_cast<void>(read_string(j, "run_dir", metadata.run_dir));
    static_cast<void>(read_string(j, "global_settings_path",
                                  metadata.global_settings_path));
    static_cast<void>(read_string(j, "artifacts_path",
                                  metadata.artifacts_path));
    static_cast<void>(read_i32(j, "pid", metadata.pid));
    static_cast<void>(read_string(j, "start_time_rfc3339",
                                  metadata.start_time_rfc3339));
    static_cast<void>(read_u64(j, "start_time_mono_ns",
                               metadata.start_time_mono_ns));
    static_cast<void>(read_string(j, "manifest_path", metadata.manifest_path));
    static_cast<void>(read_cpu_array(j, "runner_control_cpus",
                                     metadata.runner_control_cpus));
    static_cast<void>(read_cpu_array(j, "runner_logger_cpus",
                                     metadata.runner_logger_cpus));
    if (const auto it = j.find("apps"); it != j.end() && it->is_array()) {
        std::vector<Suite_app_metadata> apps;
        apps.reserve(it->size());
        for (const auto& entry : *it) {
            Suite_app_metadata app;
            from_json(entry, app);
            apps.push_back(std::move(app));
        }
        metadata.apps = std::move(apps);
    }
}

void to_json(nlohmann::json& j, const Suite_exit& exit) {
    j = nlohmann::json{{"state", std::string(to_string(exit.state))},
                       {"reason", exit.reason},
                       {"uptime_s", exit.uptime_s}};
}

void from_json(const nlohmann::json& j, Suite_exit& exit) noexcept {
    if (const auto it = j.find("state"); it != j.end() && it->is_string()) {
        const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
        if (name != nullptr) {
            Suite_state parsed = exit.state;
            if (from_string(std::string_view(*name), parsed)) {
                exit.state = parsed;
            }
        }
    }
    static_cast<void>(read_string(j, "reason", exit.reason));
    static_cast<void>(read_u64(j, "uptime_s", exit.uptime_s));
}

void to_json(nlohmann::json& j, const Suite_app_status& status) {
    j = nlohmann::json{{"app", status.app},
                       {"instance", status.instance},
                       {"instance_dir", status.instance_dir},
                       {"artifact_dir", status.artifact_dir},
                       {"attempt", status.attempt},
                       {"state", std::string(to_string(status.state))},
                       {"restarts_used", status.restarts_used}};
}

void from_json(const nlohmann::json& j, Suite_app_status& status) noexcept {
    static_cast<void>(read_string(j, "app", status.app));
    static_cast<void>(read_string(j, "instance", status.instance));
    static_cast<void>(read_string(j, "instance_dir", status.instance_dir));
    static_cast<void>(read_string(j, "artifact_dir", status.artifact_dir));
    static_cast<void>(read_u32(j, "attempt", status.attempt));
    if (const auto it = j.find("state"); it != j.end() && it->is_string()) {
        const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
        if (name != nullptr) {
            App_state parsed = status.state;
            if (from_string(std::string_view(*name), parsed)) {
                status.state = parsed;
            }
        }
    }
    static_cast<void>(read_u32(j, "restarts_used", status.restarts_used));
}

void to_json(nlohmann::json& j, const Suite_status& status) {
    nlohmann::json apps = nlohmann::json::array();
    for (const Suite_app_status& app : status.apps) {
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

void from_json(const nlohmann::json& j, Suite_status& status) noexcept {
    if (const auto it = j.find("state"); it != j.end() && it->is_string()) {
        const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
        if (name != nullptr) {
            Suite_state parsed = status.state;
            if (from_string(std::string_view(*name), parsed)) {
                status.state = parsed;
            }
        }
    }
    static_cast<void>(read_string(j, "suite_id", status.suite_id));
    static_cast<void>(read_string(j, "run_dir", status.run_dir));
    static_cast<void>(read_string(j, "artifacts_path", status.artifacts_path));
    static_cast<void>(read_u32(j, "suite_restarts_used",
                               status.suite_restarts_used));
    if (const auto it = j.find("apps"); it != j.end() && it->is_array()) {
        std::vector<Suite_app_status> apps;
        apps.reserve(it->size());
        for (const auto& entry : *it) {
            Suite_app_status app;
            from_json(entry, app);
            apps.push_back(std::move(app));
        }
        status.apps = std::move(apps);
    }
}

// --- path contract ---------------------------------------------------------

std::string suite_dir_path(std::string_view run_dir,
                           std::string_view suite_id) {
    return std::string(run_dir) + "/.suite/" + std::string(suite_id);
}

std::string attempt_dir_path(std::string_view artifacts_path,
                             std::string_view suite_id,
                             std::string_view app,
                             std::string_view instance,
                             std::uint32_t attempt) {
    return std::string(artifacts_path) + "/" + std::string(suite_id) + "/" +
           std::string(app) + "/" + std::string(instance) + "/" +
           std::format("attempt-{:04}", attempt);
}

// --- validation ------------------------------------------------------------

core::Status validate_suite_manifest(const Suite_manifest& manifest) noexcept {
    auto valid_level = [](const std::optional<logging::LogLevel>& level) {
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
            case logging::LogLevel::Off:
                return true;
            default:
                return false;
        }
    };
    if (manifest.version != 1U) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!valid_level(manifest.log_level)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!safe_id(manifest.suite_id) || !absolute_path(manifest.run_dir) ||
        !absolute_path(manifest.artifacts_path)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (manifest.run_dir.find('\0') != std::string::npos ||
        manifest.artifacts_path.find('\0') != std::string::npos ||
        manifest.global_settings_path.find('\0') != std::string::npos) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!manifest.global_settings_path.empty() &&
        !absolute_path(manifest.global_settings_path)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (manifest.suite_restart_attempts > max_restart_attempts ||
        manifest.suite_restart_delay_s > max_delay_seconds) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (manifest.apps.empty() || manifest.apps.size() > max_apps) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    long online = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (online <= 0) {
        online = static_cast<long>(CPU_SETSIZE);
    }

    for (std::size_t index = 0; index < manifest.apps.size(); ++index) {
        const App_spec& app = manifest.apps[index];
        if (!safe_id(app.app) || !safe_id(app.instance) ||
            app.app == ".suite" || app.command.empty() ||
            app.command.find('\0') != std::string::npos) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        for (const std::string& argument : app.args) {
            if (argument.find('\0') != std::string::npos) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }
        for (const auto& [key, value] : app.env) {
            if (key.empty() || key.find('=') != std::string::npos ||
                key.find('\0') != std::string::npos ||
                value.find('\0') != std::string::npos) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }
        if (app.impact != Impact_on_app_suite_operability::Failure &&
            app.impact != Impact_on_app_suite_operability::Degrade &&
            app.impact != Impact_on_app_suite_operability::Not_impact) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (!valid_level(app.log_level)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (app.restart_attempts > max_restart_attempts ||
            app.restart_delay_s > max_delay_seconds ||
            app.idle_timeout_s > max_delay_seconds ||
            app.stop_grace_s > max_delay_seconds) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (!app.global_settings_path.empty() &&
            !absolute_path(app.global_settings_path)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (!app.local_settings_path.empty() &&
            (absolute_path(app.local_settings_path) ||
             has_parent_segment(app.local_settings_path))) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        for (std::size_t cpu_index = 0; cpu_index < app.cpus.size(); ++cpu_index) {
            if (app.cpus[cpu_index] >= static_cast<std::uint32_t>(online) ||
                app.cpus[cpu_index] >=
                    static_cast<std::uint32_t>(CPU_SETSIZE)) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            for (std::size_t previous = 0; previous < cpu_index; ++previous) {
                if (app.cpus[previous] == app.cpus[cpu_index]) {
                    return std::unexpected(core::ErrorCode::InvalidArgument);
                }
            }
        }

        const std::string instance_dir =
            manifest.run_dir + "/" + app.app + "/" + app.instance;
        if (instance_dir.size() > max_path_bytes) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        const std::string attempt_dir =
            attempt_dir_path(manifest.artifacts_path, manifest.suite_id,
                             app.app, app.instance, 1);
        if (attempt_dir.size() > max_path_bytes) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        for (std::size_t other = 0; other < index; ++other) {
            if (manifest.apps[other].app == app.app &&
                manifest.apps[other].instance == app.instance) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }
    }
    return {};
}

[[nodiscard]] bool json_enum_fields_valid(const nlohmann::json& document) {
    if (!document.is_object()) {
        return false;
    }
    auto valid_level = [](const nlohmann::json& object,
                          const char* key) -> bool {
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
    };
    if (!valid_level(document, "log_level")) {
        return false;
    }
    auto valid_cpu_array = [](const nlohmann::json& object,
                              const char* key) -> bool {
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
    };
    if (!valid_cpu_array(document, "runner_control_cpus") ||
        !valid_cpu_array(document, "runner_logger_cpus")) {
        return false;
    }
    const auto apps = document.find("apps");
    if (apps == document.end() || !apps->is_array()) {
        return false;
    }
    for (const auto& entry : *apps) {
        if (!entry.is_object()) {
            return false;
        }
        const auto impact = entry.find("impact_on_app_suite_operability");
        if (impact == entry.end() || !impact->is_string()) {
            return false;
        }
        const auto* const name =
            impact->get_ptr<const nlohmann::json::string_t*>();
        if (name == nullptr) {
            return false;
        }
        Impact_on_app_suite_operability parsed =
            Impact_on_app_suite_operability::Degrade;
        if (!from_string(std::string_view(*name), parsed)) {
            return false;
        }
        if (!valid_level(entry, "log_level")) {
            return false;
        }
    }
    return true;
}

core::Status load_suite_manifest(std::string_view path,
                                 Suite_manifest& out) noexcept {
    const core::Result<nlohmann::json> document = read_json_file(path);
    if (!document) {
        return std::unexpected(document.error());
    }
    if (!json_enum_fields_valid(*document)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    Suite_manifest parsed{};
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

struct Suite_start_report {
    std::int32_t pid{0};
    std::int32_t error{0};
};

struct Runtime_app {
    App_spec spec{};
    std::string instance_dir{};
    std::string artifact_dir{};
    App_state state{App_state::Unknown};
    std::uint32_t attempt{0};
    std::uint32_t restarts_used{0};
    std::uint64_t next_restart_ns{0};
    std::int32_t child_pid{0};
    std::int32_t supervisor_pid{0};
    bool exit_consumed{false};
    bool launch_failed{false};
};

struct Runtime_suite {
    const Suite_manifest* manifest{nullptr};
    std::string suite_dir{};
    std::string state_path{};
    std::string exit_path{};
    std::string stop_path{};
    Suite_state state{Suite_state::Running};
    std::uint32_t suite_restarts_used{0};
    bool suite_restart_pending{false};
    std::uint64_t next_suite_restart_ns{0};
    std::uint64_t start_mono_ns{0};
    std::vector<Runtime_app> apps{};
    bool dirty{false};
};

void redirect_to_null_local(int null_fd) noexcept {
    static_cast<void>(::dup2(null_fd, STDIN_FILENO));
    static_cast<void>(::dup2(null_fd, STDOUT_FILENO));
    static_cast<void>(::dup2(null_fd, STDERR_FILENO));
}

void reap_supervisors() noexcept {
    int status = 0;
    while (::waitpid(-1, &status, WNOHANG) > 0) {
    }
}

[[nodiscard]] bool read_exit_metadata(const std::string& instance_dir,
                                      Exit_metadata& out) noexcept {
    const core::Result<nlohmann::json> document =
        read_json_file(instance_file_path(instance_dir, "exit.json"));
    if (!document) {
        return false;
    }
    out = Exit_metadata{};
    from_json(*document, out);
    return true;
}

[[nodiscard]] bool read_resource_metadata(const std::string& instance_dir,
                                          Resource_metadata& out) noexcept {
    const core::Result<nlohmann::json> document =
        read_json_file(instance_file_path(instance_dir, "resource.json"));
    if (!document) {
        return false;
    }
    out = Resource_metadata{};
    from_json(*document, out);
    return true;
}

void write_suite_status(Runtime_suite& suite) {
    Suite_status status{};
    status.state = suite.state;
    status.suite_id = suite.manifest->suite_id;
    status.run_dir = suite.manifest->run_dir;
    status.artifacts_path = suite.manifest->artifacts_path;
    status.suite_restarts_used = suite.suite_restarts_used;
    status.apps.reserve(suite.apps.size());
    for (const Runtime_app& app : suite.apps) {
        Suite_app_status entry{};
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

void write_suite_exit(Runtime_suite& suite, Suite_state state,
                      std::string_view reason) {
    suite.state = state;
    Suite_exit exit{};
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

[[nodiscard]] std::vector<std::string> command_vector(const App_spec& spec) {
    std::vector<std::string> command;
    command.reserve(spec.args.size() + 1);
    command.push_back(spec.command);
    for (const std::string& argument : spec.args) {
        command.push_back(argument);
    }
    return command;
}

void stop_all(Runtime_suite& suite) {
    for (Runtime_app& app : suite.apps) {
        if (app.state == App_state::Running ||
            app.state == App_state::Restarting || app.child_pid > 0 ||
            app.supervisor_pid > 0) {
            Runner runner;
            static_cast<void>(
                runner.stop(app.instance_dir, app.spec.stop_grace_s));
        }
        const std::int32_t pgid = app.child_pid;
        if (pgid > 0 && group_alive(pgid)) {
            static_cast<void>(::kill(-static_cast<pid_t>(pgid), SIGKILL));
        }
    }
    reap_supervisors();
}

[[nodiscard]] bool launch_app(Runtime_suite& suite, Runtime_app& app) {
    ++app.attempt;
    app.artifact_dir = attempt_dir_path(
        suite.manifest->artifacts_path, suite.manifest->suite_id,
        app.spec.app, app.spec.instance, app.attempt);

    Start_options options{};
    options.artifact_dir = app.artifact_dir;
    options.global_settings_dir = app.spec.global_settings_path.empty()
                                      ? suite.manifest->global_settings_path
                                      : app.spec.global_settings_path;
    if (!app.spec.local_settings_path.empty()) {
        options.local_settings_dir =
            join_path(directory_of(suite.manifest->manifest_path),
                      app.spec.local_settings_path);
    }
    options.log_level = app.spec.log_level.has_value()
                            ? app.spec.log_level
                            : suite.manifest->log_level;
    options.env = app.spec.env;
    options.cpus = app.spec.cpus;

    const app::Run_identity identity{suite.manifest->run_id,
                                     suite.manifest->run_dir, app.spec.app,
                                     app.spec.instance, app.instance_dir};
    const std::vector<std::string> command = command_vector(app.spec);
    Runner runner;
    const core::Status started = runner.start(identity, command,
                                              app.spec.idle_timeout_s,
                                              options);
    if (!started) {
        app.state = App_state::Failed;
        app.child_pid = 0;
        app.supervisor_pid = 0;
        app.launch_failed = true;
        app.exit_consumed = false;
        return false;
    }

    Resource_metadata resource{};
    if (read_resource_metadata(app.instance_dir, resource)) {
        app.child_pid = resource.pid;
        app.supervisor_pid = resource.ppid;
    }
    app.state = App_state::Running;
    app.exit_consumed = false;
    app.launch_failed = false;
    app.next_restart_ns = 0;
    return true;
}

void schedule_retry_or_fail(Runtime_suite& suite, Runtime_app& app) {
    if (app.restarts_used < app.spec.restart_attempts) {
        ++app.restarts_used;
        app.state = App_state::Restarting;
        app.next_restart_ns =
            monotonic_now_ns() + app.spec.restart_delay_s * nanoseconds_per_second;
        log_runner(logging::LogLevel::Info,
                   std::format("event=retry_scheduled app={} instance={} "
                               "attempt={} impact={} delay_s={}",
                               app.spec.app, app.spec.instance, app.attempt,
                               nexigen::runner::to_string(app.spec.impact),
                               app.spec.restart_delay_s));
    } else {
        app.state = App_state::Failed;
        if (app.spec.impact == Impact_on_app_suite_operability::Degrade) {
            suite.state = Suite_state::Degraded;
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

void finish_failed(Runtime_suite& suite) {
    stop_all(suite);
    for (Runtime_app& app : suite.apps) {
        if (app.state != App_state::Failed) {
            app.state = App_state::Stopped;
        }
    }
    log_runner(logging::LogLevel::Error,
               std::format("event=suite_failed suite={} reason=restart_exhausted",
                           suite.manifest->suite_id));
    write_suite_exit(suite, Suite_state::Failed, "restart_exhausted");
    shutdown_runner_logging();
    _exit(0);
}

void trigger_suite_restart(Runtime_suite& suite) {
    if (suite.suite_restarts_used >=
        suite.manifest->suite_restart_attempts) {
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
    for (Runtime_app& app : suite.apps) {
        app.state = App_state::Restarting;
        app.restarts_used = 0;
        app.exit_consumed = false;
        app.launch_failed = false;
        app.child_pid = 0;
        app.supervisor_pid = 0;
        app.next_restart_ns = 0;
    }
    suite.state = Suite_state::Running;
    suite.suite_restart_pending = true;
    suite.next_suite_restart_ns =
        monotonic_now_ns() +
        suite.manifest->suite_restart_delay_s * nanoseconds_per_second;
    suite.dirty = true;
    write_suite_status(suite);
}

void handle_abnormal(Runtime_suite& suite, Runtime_app& app) {
    if (app.spec.impact == Impact_on_app_suite_operability::Failure) {
        trigger_suite_restart(suite);
        return;
    }
    schedule_retry_or_fail(suite, app);
}

void launch_all(Runtime_suite& suite) {
    for (Runtime_app& app : suite.apps) {
        static_cast<void>(launch_app(suite, app));
    }
}

void apply_launch_failures(Runtime_suite& suite) {
    bool failure_impact_failed = false;
    for (const Runtime_app& app : suite.apps) {
        if (app.launch_failed &&
            app.spec.impact == Impact_on_app_suite_operability::Failure) {
            failure_impact_failed = true;
            break;
        }
    }
    if (failure_impact_failed) {
        trigger_suite_restart(suite);
        return;
    }
    for (Runtime_app& app : suite.apps) {
        if (app.launch_failed) {
            app.launch_failed = false;
            schedule_retry_or_fail(suite, app);
        }
    }
}

void run_suite_supervisor(const Suite_manifest& manifest,
                          const std::string& suite_dir,
                          int report_fd) {
    static_cast<void>(reset_runner_affinity());
    const core::Status control_pin =
        apply_runner_affinity(manifest.runner_control_cpus, "suite-supervisor");
    if (!control_pin) {
        std::fprintf(stderr,
                     "nexigen-run: runner_control_cpus is invalid; suite "
                     "supervisor is unpinned\n");
    }

    Runtime_suite suite{};
    suite.manifest = &manifest;
    suite.suite_dir = suite_dir;
    suite.state_path = join_path(suite_dir, "suite-state.json");
    suite.exit_path = join_path(suite_dir, "suite-exit.json");
    suite.stop_path = join_path(suite_dir, "stop.request");
    suite.start_mono_ns = monotonic_now_ns();

    Suite_metadata metadata{};
    metadata.suite_id = manifest.suite_id;
    metadata.run_id = manifest.run_id;
    metadata.run_dir = manifest.run_dir;
    metadata.global_settings_path = manifest.global_settings_path;
    metadata.artifacts_path = manifest.artifacts_path;
    metadata.pid = ::getpid();
    metadata.start_time_rfc3339 = utc_now_rfc3339();
    metadata.start_time_mono_ns = suite.start_mono_ns;
    metadata.manifest_path = manifest.manifest_path;
    metadata.runner_control_cpus = manifest.runner_control_cpus;
    metadata.runner_logger_cpus = manifest.runner_logger_cpus;
    metadata.apps.reserve(manifest.apps.size());
    for (const App_spec& app : manifest.apps) {
        Suite_app_metadata entry{};
        entry.app = app.app;
        entry.instance = app.instance;
        entry.instance_dir =
            manifest.run_dir + "/" + app.app + "/" + app.instance;
        entry.stop_grace_s = app.stop_grace_s;
        metadata.apps.push_back(std::move(entry));
    }
    nlohmann::json metadata_json;
    to_json(metadata_json, metadata);
    if (!write_json_file(join_path(suite_dir, "suite.json"), metadata_json)) {
        Suite_start_report report{0, static_cast<std::int32_t>(
                                          core::ErrorCode::SystemError)};
        static_cast<void>(write_all(report_fd, &report, sizeof(report)));
        static_cast<void>(::close(report_fd));
        _exit(1);
    }

    suite.apps.reserve(manifest.apps.size());
    for (const App_spec& app : manifest.apps) {
        Runtime_app runtime{};
        runtime.spec = app;
        runtime.instance_dir =
            manifest.run_dir + "/" + app.app + "/" + app.instance;
        suite.apps.push_back(std::move(runtime));
    }

    launch_all(suite);
    write_suite_status(suite);

    Suite_start_report report{::getpid(), 0};
    static_cast<void>(write_all(report_fd, &report, sizeof(report)));
    static_cast<void>(::close(report_fd));

    logging::Logging_settings log_settings{};
    const core::Status settings_status = load_runner_logging_settings(
        manifest.global_settings_path, log_settings);
    if (!settings_status) {
        warn_runner("event=logging_settings_invalid detail=using defaults");
        log_settings = logging::Logging_settings{};
    }
    const logging::LogLevel log_level =
        manifest.log_level.value_or(
            log_settings.global_loaded
                ? log_settings.options.initial_log_level
                : logging::LogLevel::Info);
    const core::Status log_status = init_runner_logging(
        join_path(manifest.artifacts_path, manifest.suite_id), log_settings,
        log_level,
        std::span<const std::uint32_t>(manifest.runner_logger_cpus.data(),
                                       manifest.runner_logger_cpus.size()));
    if (!log_status) {
        warn_runner("event=logging_unavailable detail=runner.jsonl not writable");
    }
    log_runner(logging::LogLevel::Info,
               std::format("event=suite_started suite={} apps={}",
                           manifest.suite_id, suite.apps.size()));
    for (const Runtime_app& app : suite.apps) {
        log_runner(app.state == App_state::Failed
                       ? logging::LogLevel::Warning
                       : logging::LogLevel::Info,
                   std::format("event=app_launch app={} instance={} attempt={} "
                               "pid={} state={}",
                               app.spec.app, app.spec.instance, app.attempt,
                               app.child_pid,
                               nexigen::runner::to_string(app.state)));
    }

    apply_launch_failures(suite);
    write_suite_status(suite);

    for (;;) {
        if (file_exists(suite.stop_path)) {
            log_runner(logging::LogLevel::Info,
                       std::format("event=stop_requested suite={}",
                                   manifest.suite_id));
            stop_all(suite);
            for (Runtime_app& app : suite.apps) {
                app.state = App_state::Stopped;
            }
            write_suite_exit(suite, Suite_state::Stopped, "operator_stop");
            log_runner(logging::LogLevel::Info,
                       std::format("event=suite_stopped suite={} reason=operator_stop",
                                   manifest.suite_id));
            shutdown_runner_logging();
            _exit(0);
        }
        reap_supervisors();

        const std::uint64_t now = monotonic_now_ns();
        if (suite.suite_restart_pending) {
            if (now >= suite.next_suite_restart_ns) {
                suite.suite_restart_pending = false;
                suspend_runner_logging();
                launch_all(suite);
                static_cast<void>(resume_runner_logging());
                apply_launch_failures(suite);
                log_runner(logging::LogLevel::Info,
                           std::format("event=suite_restart suite={} used={} limit={}",
                                       manifest.suite_id,
                                       suite.suite_restarts_used,
                                       manifest.suite_restart_attempts));
                for (const Runtime_app& app : suite.apps) {
                    log_runner(logging::LogLevel::Info,
                               std::format("event=app_launch app={} instance={} "
                                           "attempt={} pid={} state={}",
                                           app.spec.app, app.spec.instance,
                                           app.attempt, app.child_pid,
                                           nexigen::runner::to_string(app.state)));
                }
                write_suite_status(suite);
            }
            sleep_ms(suite_poll_interval_ms);
            continue;
        }

        for (Runtime_app& app : suite.apps) {
            if (app.state == App_state::Running) {
                Exit_metadata exit{};
                if (read_exit_metadata(app.instance_dir, exit) &&
                    !app.exit_consumed) {
                    app.exit_consumed = true;
                    if (exit.exit_code.has_value()) {
                        log_runner(logging::LogLevel::Info,
                                   std::format("event=app_exit app={} instance={} "
                                               "attempt={} exit_code={}",
                                               app.spec.app, app.spec.instance,
                                               app.attempt, *exit.exit_code));
                    } else if (exit.signal.has_value()) {
                        log_runner(logging::LogLevel::Warning,
                                   std::format("event=app_exit app={} instance={} "
                                               "attempt={} signal={}",
                                               app.spec.app, app.spec.instance,
                                               app.attempt, *exit.signal));
                    }
                    if (exit.exit_code.has_value() &&
                        *exit.exit_code == 0) {
                        app.state = App_state::Exited;
                        suite.dirty = true;
                    } else {
                        handle_abnormal(suite, app);
                        if (suite.suite_restart_pending) {
                            break;
                        }
                    }
                }
            } else if (app.state == App_state::Restarting &&
                       app.next_restart_ns != 0 && now >= app.next_restart_ns) {
                app.launch_failed = false;
                suspend_runner_logging();
                const bool launched = launch_app(suite, app);
                static_cast<void>(resume_runner_logging());
                if (!launched) {
                    app.launch_failed = false;
                    log_runner(logging::LogLevel::Warning,
                               std::format("event=app_launch_failed app={} "
                                           "instance={} attempt={}",
                                           app.spec.app, app.spec.instance,
                                           app.attempt));
                    schedule_retry_or_fail(suite, app);
                } else {
                    log_runner(logging::LogLevel::Info,
                               std::format("event=app_launch app={} instance={} "
                                           "attempt={} pid={} state=running",
                                           app.spec.app, app.spec.instance,
                                           app.attempt, app.child_pid));
                }
                suite.dirty = true;
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

core::Status start_suite(const Suite_manifest& manifest,
                         std::string& suite_dir_out) {
    const core::Status valid = validate_suite_manifest(manifest);
    if (!valid) {
        return valid;
    }

    const std::string suite_dir =
        suite_dir_path(manifest.run_dir, manifest.suite_id);
    suite_dir_out = suite_dir;

    const std::string metadata_path = join_path(suite_dir, "suite.json");
    const std::string exit_path = join_path(suite_dir, "suite-exit.json");
    const std::string stop_path = join_path(suite_dir, "stop.request");
    if (file_exists(metadata_path) && !file_exists(exit_path)) {
        Suite_metadata existing{};
        const core::Result<nlohmann::json> document =
            read_json_file(metadata_path);
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
        _exit(0);
    }

    static_cast<void>(::close(pipe_fds[1]));
    Suite_start_report report{};
    const bool received = read_all(pipe_fds[0], &report, sizeof(report));
    static_cast<void>(::close(pipe_fds[0]));
    if (!received || report.error != 0 || report.pid <= 0) {
        int status = 0;
        static_cast<void>(::waitpid(supervisor, &status, WNOHANG));
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
    Suite_metadata metadata{};
    from_json(*document, metadata);

    const int fd = ::open(stop_path.c_str(),
                          O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    static_cast<void>(::close(fd));

    std::uint64_t grace = 5;
    if (grace_override.has_value()) {
        grace = *grace_override;
    } else if (!metadata.apps.empty()) {
        grace = metadata.apps.front().stop_grace_s;
        for (const Suite_app_metadata& app : metadata.apps) {
            if (app.stop_grace_s > grace) {
                grace = app.stop_grace_s;
            }
        }
    }

    const std::uint64_t deadline =
        monotonic_now_ns() + grace * nanoseconds_per_second +
        5ULL * nanoseconds_per_second;
    while (monotonic_now_ns() < deadline && !file_exists(exit_path)) {
        sleep_ms(suite_poll_interval_ms);
    }

    if (!file_exists(exit_path)) {
        if (metadata.pid > 0 && process_alive(metadata.pid)) {
            static_cast<void>(::kill(metadata.pid, SIGTERM));
            const std::uint64_t kill_deadline =
                monotonic_now_ns() + 2ULL * nanoseconds_per_second;
            while (monotonic_now_ns() < kill_deadline &&
                   process_alive(metadata.pid)) {
                sleep_ms(suite_poll_interval_ms);
            }
            if (process_alive(metadata.pid)) {
                static_cast<void>(::kill(metadata.pid, SIGKILL));
            }
        }
        for (const Suite_app_metadata& app : metadata.apps) {
            Runner runner;
            static_cast<void>(runner.stop(
                app.instance_dir,
                grace_override.value_or(app.stop_grace_s)));
        }

        Suite_status status{};
        const core::Result<nlohmann::json> existing =
            read_json_file(state_path);
        if (existing) {
            from_json(*existing, status);
        } else {
            status.suite_id = metadata.suite_id;
            status.run_dir = metadata.run_dir;
            status.artifacts_path = metadata.artifacts_path;
            status.apps.reserve(metadata.apps.size());
            for (const Suite_app_metadata& app : metadata.apps) {
                Suite_app_status entry{};
                entry.app = app.app;
                entry.instance = app.instance;
                entry.instance_dir = app.instance_dir;
                entry.state = App_state::Stopped;
                status.apps.push_back(std::move(entry));
            }
        }
        status.state = Suite_state::Stopped;
        for (Suite_app_status& app : status.apps) {
            app.state = App_state::Stopped;
        }
        nlohmann::json status_json;
        to_json(status_json, status);
        static_cast<void>(write_json_file(state_path, status_json));

        Suite_exit exit{};
        exit.state = Suite_state::Stopped;
        exit.reason = "forced_stop";
        nlohmann::json exit_json;
        to_json(exit_json, exit);
        static_cast<void>(write_json_file(exit_path, exit_json));
    }

    return {};
}

core::Status suite_status(std::string_view suite_dir,
                          Suite_status& out) noexcept {
    out = Suite_status{};
    if (suite_dir.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const std::string directory(suite_dir);
    const std::string metadata_path = join_path(directory, "suite.json");
    const std::string state_path = join_path(directory, "suite-state.json");
    const std::string exit_path = join_path(directory, "suite-exit.json");

    const core::Result<nlohmann::json> metadata_document =
        read_json_file(metadata_path);
    if (!metadata_document) {
        return {};
    }
    Suite_metadata metadata{};
    from_json(*metadata_document, metadata);

    out.suite_id = metadata.suite_id;
    out.run_dir = metadata.run_dir;
    out.artifacts_path = metadata.artifacts_path;

    Suite_status recorded{};
    bool have_recorded = false;
    if (const core::Result<nlohmann::json> state_document =
            read_json_file(state_path)) {
        from_json(*state_document, recorded);
        have_recorded = true;
    }

    const core::Result<nlohmann::json> exit_document =
        read_json_file(exit_path);
    if (exit_document) {
        Suite_exit exit{};
        from_json(*exit_document, exit);
        out.state = exit.state;
    } else if (have_recorded) {
        out.state = recorded.state;
        out.suite_restarts_used = recorded.suite_restarts_used;
    } else {
        out.state = Suite_state::Unknown;
    }
    if (have_recorded) {
        out.suite_restarts_used = recorded.suite_restarts_used;
    }

    out.apps.reserve(metadata.apps.size());
    for (const Suite_app_metadata& app : metadata.apps) {
        Suite_app_status entry{};
        entry.app = app.app;
        entry.instance = app.instance;
        entry.instance_dir = app.instance_dir;
        for (const Suite_app_status& recorded_app : recorded.apps) {
            if (recorded_app.app == app.app &&
                recorded_app.instance == app.instance) {
                entry = recorded_app;
                break;
            }
        }

        std::string state;
        Runner runner;
        const bool sticky = entry.state == App_state::Failed ||
                            entry.state == App_state::Restarting;
        if (!sticky && runner.status(app.instance_dir, state).has_value()) {
            if (state == "running") {
                entry.state = App_state::Running;
            } else if (state == "stopped") {
                entry.state = App_state::Stopped;
            } else if (state == "exited") {
                entry.state = App_state::Exited;
            } else if (!have_recorded || entry.state == App_state::Unknown) {
                entry.state = App_state::Unknown;
            }
        }
        out.apps.push_back(std::move(entry));
    }
    return {};
}

} // namespace nexigen::runner
