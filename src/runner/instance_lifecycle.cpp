// nexigen::runner — instance lifecycle metadata implementation
// (change nexigen-run; spec runner/instance-lifecycle).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nexigen/chrono/time_format.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/runner/detail/sys_helpers.hpp>
#include <nexigen/runner/instance_lifecycle.hpp>
#include <nexigen/runner/runner_logging.hpp>

namespace nexigen::runner {

using detail::enum_from_json;
using detail::enum_to_json;
using detail::monotonic_now_ns;
using detail::read_i32;
using detail::read_optional_i32;
using detail::read_string;
using detail::read_u64;
using detail::utc_now_rfc3339;

namespace {

/// Maximum length accepted for the instance directory. The logging settings
/// loader caps `log_root_directory` at 511 bytes, so the directory must fit.
constexpr std::size_t max_instance_dir_bytes = 511;

void report_write_failure(std::string_view path, int error) noexcept {
    warn_runner(std::format("event=json_write_failed path={} errno={}", path,
                            error));
}

/// mkdir -p for absolute paths. Every existing component must be a directory.
[[nodiscard]] bool make_directories(std::string_view path) noexcept {
    if (path.empty() || path.front() != '/') {
        return false;
    }
    std::string current;
    current.reserve(path.size());
    for (std::size_t index = 1; index <= path.size(); ++index) {
        if (index != path.size() && path[index] != '/') {
            continue;
        }
        current.assign(path.substr(0, index));
        if (current.empty()) {
            continue;
        }
        if (::mkdir(current.c_str(), 0700) != 0 && errno != EEXIST) {
            return false;
        }
        struct stat status {};
        if (::stat(current.c_str(), &status) != 0 || !S_ISDIR(status.st_mode)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool inside_stream_root(std::string_view path,
                                      std::string_view stream_root) noexcept {
    if (path.empty() || path.front() != '/') {
        return false;
    }
    if (path.size() <= stream_root.size()) {
        return false;
    }
    return path.compare(0, stream_root.size(), stream_root) == 0 &&
           path[stream_root.size()] == '/';
}

[[nodiscard]] std::string stream_path(std::string_view stream_root,
                                      std::string_view resolved,
                                      std::string_view file_name) {
    if (inside_stream_root(resolved, stream_root)) {
        return std::string(resolved);
    }
    return instance_file_path(stream_root, file_name);
}

} // namespace

// --- TerminatedBy ---
void to_json(nlohmann::json& j, TerminatedBy value) {
    enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, TerminatedBy& value) noexcept {
    enum_from_json(j, value);
}

// --- ResourceMetadata ---
std::string ResourceMetadata::to_string() const {
    return std::format(
        "ResourceMetadata(run_id={}, app_name={}, instance_id={}, "
        "instance_dir={}, pid={}, ppid={}, start_time_rfc3339={}, "
        "start_time_mono_ns={}, version={})",
        run_id, app_name, instance_id, instance_dir, pid, ppid,
        start_time_rfc3339, start_time_mono_ns, version);
}

ResourceMetadata ResourceMetadata::from_identity(
    const app::RunIdentity& identity, std::int32_t pid, std::int32_t ppid) {
    ResourceMetadata metadata;
    metadata.run_id = identity.run_id;
    metadata.app_name = identity.app_name;
    metadata.instance_id = identity.instance_id;
    metadata.instance_dir = identity.instance_dir;
    metadata.pid = pid;
    metadata.ppid = ppid;
    metadata.start_time_rfc3339 = utc_now_rfc3339();
    metadata.start_time_mono_ns = monotonic_now_ns();
    return metadata;
}

void to_json(nlohmann::json& j, const ResourceMetadata& metadata) {
    j = nlohmann::json{
        {"run_id", metadata.run_id},
        {"app_name", metadata.app_name},
        {"instance_id", metadata.instance_id},
        {"instance_dir", metadata.instance_dir},
        {"pid", metadata.pid},
        {"ppid", metadata.ppid},
        {"start_time_rfc3339", metadata.start_time_rfc3339},
        {"start_time_mono_ns", metadata.start_time_mono_ns},
        {"version", metadata.version},
    };
}

void from_json(const nlohmann::json& j, ResourceMetadata& metadata) noexcept {
    static_cast<void>(read_string(j, "run_id", metadata.run_id));
    static_cast<void>(read_string(j, "app_name", metadata.app_name));
    static_cast<void>(read_string(j, "instance_id", metadata.instance_id));
    static_cast<void>(read_string(j, "instance_dir", metadata.instance_dir));
    static_cast<void>(read_i32(j, "pid", metadata.pid));
    static_cast<void>(read_i32(j, "ppid", metadata.ppid));
    static_cast<void>(read_string(j, "start_time_rfc3339",
                                  metadata.start_time_rfc3339));
    static_cast<void>(read_u64(j, "start_time_mono_ns",
                                  metadata.start_time_mono_ns));
    static_cast<void>(read_string(j, "version", metadata.version));
}

// --- ExitMetadata ---
std::string ExitMetadata::to_string() const {
    const std::string exit_text =
        exit_code.has_value() ? std::to_string(*exit_code) : "null";
    const std::string signal_text =
        signal.has_value() ? std::to_string(*signal) : "null";
    return std::format(
        "ExitMetadata(exit_code={}, signal={}, uptime_s={}, terminated_by={})",
        exit_text, signal_text, uptime_s,
        nexigen::runner::to_string(terminated_by));
}

ExitMetadata ExitMetadata::from_child_status(
    std::int32_t pid, std::int32_t status, std::uint64_t uptime_s,
    TerminatedBy terminated_by) noexcept {
    static_cast<void>(pid);
    ExitMetadata metadata;
    metadata.uptime_s = uptime_s;
    metadata.terminated_by = terminated_by;
    if (WIFEXITED(status)) {
        metadata.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        metadata.signal = WTERMSIG(status);
    }
    return metadata;
}

void to_json(nlohmann::json& j, const ExitMetadata& metadata) {
    nlohmann::json exit_code = nlohmann::json();
    nlohmann::json signal = nlohmann::json();
    if (metadata.exit_code.has_value()) {
        exit_code = *metadata.exit_code;
    }
    if (metadata.signal.has_value()) {
        signal = *metadata.signal;
    }
    j = nlohmann::json{
        {"exit_code", exit_code},
        {"signal", signal},
        {"uptime_s", metadata.uptime_s},
        {"terminated_by", std::string(to_string(metadata.terminated_by))},
    };
}

void from_json(const nlohmann::json& j, ExitMetadata& metadata) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(
        read_optional_i32(j, "exit_code", metadata.exit_code));
    static_cast<void>(read_optional_i32(j, "signal", metadata.signal));
    static_cast<void>(read_u64(j, "uptime_s", metadata.uptime_s));
    if (const auto it = j.find("terminated_by");
        it != j.end() && it->is_string()) {
        const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
        if (name != nullptr) {
            TerminatedBy parsed = metadata.terminated_by;
            if (from_string(std::string_view(*name), parsed)) {
                metadata.terminated_by = parsed;
            }
        }
    }
}

// --- JSON file I/O ---
core::Status write_json_file(std::string_view path,
                             const nlohmann::json& document) noexcept {
    const std::string text = document.dump(2);
    const std::string target(path);
    const std::string temporary =
        target + ".tmp." + std::to_string(static_cast<long long>(::getpid()));

    const int fd = ::open(temporary.c_str(),
                          O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        report_write_failure(target, errno);
        return std::unexpected(core::ErrorCode::SystemError);
    }

    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t count =
            ::write(fd, text.data() + written, text.size() - written);
        if (count <= 0) {
            if (count < 0 && errno == EINTR) {
                continue;
            }
            const int error = errno;
            static_cast<void>(::close(fd));
            static_cast<void>(::unlink(temporary.c_str()));
            report_write_failure(target, error);
            return std::unexpected(core::ErrorCode::SystemError);
        }
        written += static_cast<std::size_t>(count);
    }
    if (::close(fd) != 0) {
        const int error = errno;
        static_cast<void>(::unlink(temporary.c_str()));
        report_write_failure(target, error);
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (::rename(temporary.c_str(), target.c_str()) != 0) {
        const int error = errno;
        static_cast<void>(::unlink(temporary.c_str()));
        report_write_failure(target, error);
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

core::Result<nlohmann::json> read_json_file(std::string_view path) noexcept {
    const std::string name(path);
    const int fd = ::open(name.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    std::string text;
    char buffer[4096];
    for (;;) {
        const ssize_t count = ::read(fd, buffer, sizeof(buffer));
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            static_cast<void>(::close(fd));
            return std::unexpected(core::ErrorCode::SystemError);
        }
        text.append(buffer, static_cast<std::size_t>(count));
    }
    static_cast<void>(::close(fd));

    nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return document;
}

core::Status write_resource_json(
    std::string_view instance_dir,
    const ResourceMetadata& metadata) noexcept {
    nlohmann::json document;
    to_json(document, metadata);
    return write_json_file(instance_file_path(instance_dir, "resource.json"),
                           document);
}

core::Status write_exit_json(std::string_view instance_dir,
                             const ExitMetadata& metadata) noexcept {
    nlohmann::json document;
    to_json(document, metadata);
    return write_json_file(instance_file_path(instance_dir, "exit.json"),
                           document);
}

// --- Instance directory ---
std::string instance_file_path(std::string_view instance_dir,
                               std::string_view file_name) {
    std::string path(instance_dir);
    path.push_back('/');
    path.append(file_name);
    return path;
}

core::Status create_instance_dir(std::string_view instance_dir) noexcept {
    if (instance_dir.empty() || instance_dir.front() != '/' ||
        instance_dir.size() > max_instance_dir_bytes) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!make_directories(instance_dir)) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    const std::string settings_dir =
        instance_file_path(instance_dir, "settings");
    if (!make_directories(settings_dir)) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

core::Status create_artifact_dir(std::string_view artifact_dir) noexcept {
    if (artifact_dir.empty() || artifact_dir.front() != '/' ||
        artifact_dir.size() > max_instance_dir_bytes) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!make_directories(artifact_dir)) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

// --- Settings generation ---
core::Status generate_settings(
    std::string_view instance_dir,
    const app::TelemetryPaths& resolved) noexcept {
    return generate_settings(instance_dir, instance_dir, resolved);
}

core::Status generate_settings(
    std::string_view instance_dir,
    std::string_view stream_root,
    const app::TelemetryPaths& resolved) noexcept {
    if (instance_dir.empty() || instance_dir.front() != '/' ||
        instance_dir.size() > max_instance_dir_bytes ||
        stream_root.empty() || stream_root.front() != '/' ||
        stream_root.size() > max_instance_dir_bytes) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    auto write = [&](std::string_view file_name,
                     nlohmann::json& document) -> core::Status {
        const std::string path =
            instance_file_path(instance_file_path(instance_dir, "settings"),
                               file_name);
        return write_json_file(path, document);
    };

    const std::string app_log =
        instance_file_path(stream_root, "app.jsonl");
    const std::string metrics_log =
        stream_path(stream_root, resolved.metrics, "metrics.jsonl");
    const std::string traces_log =
        stream_path(stream_root, resolved.traces, "traces.jsonl");
    const std::string chrono_log =
        stream_path(stream_root, resolved.chrono, "chrono.jsonl");

    {
        nlohmann::json document = {
            {"log_root_directory", std::string(stream_root)},
            {"app_name", "nexigen"},
            {"initial_log_level", "info"},
            {"sink", {{"path", app_log}, {"append", true}}},
        };
        const core::Status status = write("logging.json", document);
        if (!status) {
            return status;
        }
    }
    {
        nlohmann::json document = {
            {"collect_interval_ns", 1'000'000'000},
            {"sink", {{"path", metrics_log}, {"append", false}}},
        };
        const core::Status status = write("metrics.json", document);
        if (!status) {
            return status;
        }
    }
    {
        nlohmann::json document = {
            {"max_tracers", 4},
            {"sink",
             {{"path", traces_log},
              {"rotation", "None"},
              {"max_spans_per_window", 0},
              {"window_ns", 0}}},
        };
        const core::Status status = write("tracing.json", document);
        if (!status) {
            return status;
        }
    }
    {
        nlohmann::json document = {
            {"collect_interval_ns", 1'000'000'000},
            {"sink", {{"path", chrono_log}, {"append", true}}},
        };
        const core::Status status = write("chrono.json", document);
        if (!status) {
            return status;
        }
    }
    return {};
}

static_assert(max_instance_dir_bytes == 511,
              "logging settings cap log_root_directory at 511 bytes");

} // namespace nexigen::runner
