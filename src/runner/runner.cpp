// nexigen::runner — process supervisor implementation
// (change nexigen-run; spec runner/process-supervision).
//
// COLD PATH ONLY. The runner forks a detached supervisor, which forks and
// reaps the application child. `start` returns as soon as the child PID is
// recorded; `stop` and `status` derive everything from the instance directory.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/app/app_settings.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/detail/fault_injection.hpp>
#include <nexigen/logging/logger_options.hpp>
#include <nexigen/runner/detail/sys_helpers.hpp>
#include <nexigen/runner/runner.hpp>
#include <nexigen/runner/runner_logging.hpp>

#include "coverage_flush.hpp"

namespace nexigen::runner {

using detail::file_exists;
using detail::group_alive;
using detail::monotonic_now_ns;
using detail::nanoseconds_per_second;
using detail::process_alive;
using detail::read_all;
using detail::sleep_ms;
using detail::StartReport;
using detail::write_all;

namespace {

constexpr std::size_t max_instance_dir_bytes = 511;

/// Flush coverage counters before a forked child leaves the process image.
[[noreturn]] void child_exit(int code) noexcept {
    detail::coverage_flush();
    _exit(code);
}
constexpr std::uint64_t timeout_grace_seconds = 5;
constexpr std::uint64_t stop_settle_seconds = 5;
constexpr std::uint64_t poll_interval_ms = 20;

/// Create (or truncate) then reopen a log file in append mode. The truncation
/// happens on every start, as required by the spec.
[[nodiscard]] int open_truncated_log(const std::string& path) noexcept {
    const int truncate_fd =
        ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (truncate_fd < 0) {
        return -1;
    }
    static_cast<void>(::close(truncate_fd));
    return ::open(path.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC, 0644);
}

void redirect_to_null(int null_fd) noexcept {
    static_cast<void>(::dup2(null_fd, STDIN_FILENO));
    static_cast<void>(::dup2(null_fd, STDOUT_FILENO));
    static_cast<void>(::dup2(null_fd, STDERR_FILENO));
}

constexpr std::size_t max_path_bytes = 511;

[[nodiscard]] bool absolute_path(std::string_view path) noexcept {
    return !path.empty() && path.front() == '/';
}

[[nodiscard]] core::Status validate_start_options(
    const StartOptions& options) noexcept {
    if (!options.artifact_dir.empty() &&
        (!absolute_path(options.artifact_dir) ||
         options.artifact_dir.size() > max_path_bytes)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!options.global_settings_dir.empty() &&
        !absolute_path(options.global_settings_dir)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!options.local_settings_dir.empty() &&
        !absolute_path(options.local_settings_dir)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    for (const auto& [key, value] : options.env) {
        static_cast<void>(value);
        if (key.empty() || key.find('=') != std::string::npos ||
            key.find('\0') != std::string::npos) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
    }
    if (!options.cpus.empty()) {
        long online = ::sysconf(_SC_NPROCESSORS_ONLN);
        if (online <= 0) {
            online = static_cast<long>(CPU_SETSIZE);
        }
        for (std::size_t index = 0; index < options.cpus.size(); ++index) {
            if (options.cpus[index] >= static_cast<std::uint32_t>(online) ||
                options.cpus[index] >= static_cast<std::uint32_t>(CPU_SETSIZE)) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (options.cpus[previous] == options.cpus[index]) {
                    return std::unexpected(core::ErrorCode::InvalidArgument);
                }
            }
        }
    }
    return {};
}

[[nodiscard]] nlohmann::json logging_document(
    const logging::LoggerOptions& options, std::string_view stream_root,
    const std::optional<logging::LogLevel> level) {
    nlohmann::json document;
    to_json(document, options);
    document["log_root_directory"] = std::string(stream_root);
    if (level.has_value()) {
        document["initial_log_level"] = std::string(logging::to_string(*level));
    }
    document["sink"] = nlohmann::json{
        {"path", instance_file_path(stream_root, "app.jsonl")},
        {"append", true}};
    return document;
}

[[nodiscard]] core::Status write_suite_settings(
    std::string_view instance_dir, std::string_view stream_root,
    const StartOptions& options, const app::RunIdentity& identity) noexcept {
    app::Settings settings{};
    if (!options.global_settings_dir.empty() ||
        !options.local_settings_dir.empty()) {
        app::AppSettingsPaths paths{};
        auto make_pair = [](std::string_view global, std::string_view local,
                            const char* name) {
            core::SettingsPaths entry{};
            if (!global.empty()) {
                entry.global_file = std::string(global) + "/" + name;
            }
            if (!local.empty()) {
                entry.local_file = std::string(local) + "/" + name;
            }
            return entry;
        };
        paths.logging = make_pair(options.global_settings_dir,
                                  options.local_settings_dir, "logging.json");
        paths.metrics = make_pair(options.global_settings_dir,
                                  options.local_settings_dir, "metrics.json");
        paths.tracing = make_pair(options.global_settings_dir,
                                  options.local_settings_dir, "tracing.json");
        paths.chrono.global_file =
            options.global_settings_dir.empty()
                ? std::string{}
                : std::string(options.global_settings_dir) + "/chrono.json";
        paths.chrono.local_file =
            options.local_settings_dir.empty()
                ? std::string{}
                : std::string(options.local_settings_dir) + "/chrono.json";
        const core::Status loaded = app::load_app_settings(paths, settings);
        if (!loaded) {
            return loaded;
        }
    }

    settings.logging.options.log_root_directory = std::string(stream_root);
    settings.logging.options.app_name =
        (!identity.app_name.empty() && identity.app_name.size() <= 63)
            ? identity.app_name
            : "nexigen";
    if (options.log_level.has_value()) {
        settings.logging.options.initial_log_level = *options.log_level;
    }

    settings.metrics.sink.path = instance_file_path(stream_root, "metrics.jsonl");
    settings.metrics.sink.append = true;
    settings.tracing.sink.path = instance_file_path(stream_root, "traces.jsonl");
    settings.chrono.sink.path = instance_file_path(stream_root, "chrono.jsonl");
    settings.chrono.sink.append = true;

    const std::string settings_dir = instance_file_path(instance_dir, "settings");
    auto write = [&](const char* name, const nlohmann::json& document) {
        return write_json_file(instance_file_path(settings_dir, name), document);
    };

    nlohmann::json logging_json =
        logging_document(settings.logging.options, stream_root, options.log_level);
    const core::Status logging_status = write("logging.json", logging_json);
    if (!logging_status) {
        return logging_status;
    }

    nlohmann::json metrics_json;
    to_json(metrics_json, settings.metrics);
    metrics_json["sink"] = nlohmann::json{
        {"path", settings.metrics.sink.path},
        {"append", settings.metrics.sink.append}};
    const core::Status metrics_status = write("metrics.json", metrics_json);
    if (!metrics_status) {
        return metrics_status;
    }

    nlohmann::json tracing_json;
    to_json(tracing_json, settings.tracing);
    tracing_json["sink"]["path"] = settings.tracing.sink.path;
    const core::Status tracing_status = write("tracing.json", tracing_json);
    if (!tracing_status) {
        return tracing_status;
    }

    nlohmann::json chrono_json;
    to_json(chrono_json, settings.chrono);
    chrono_json["sink"] = nlohmann::json{
        {"path", settings.chrono.sink.path},
        {"append", settings.chrono.sink.append}};
    return write("chrono.json", chrono_json);
}

/// Wait for the child while enforcing the optional idle timeout. When the
/// deadline expires, run the same SIGTERM → grace → SIGKILL sequence as
/// `Runner::stop`.
struct SuperviseResult {
    bool reaped{false};
    bool runner_stopped{false};
};

[[nodiscard]] SuperviseResult supervise_child(
    pid_t child, std::uint64_t timeout_s, std::uint64_t start_mono_ns,
    int& status) noexcept {
    SuperviseResult result;
    if (timeout_s == 0) {
        // No deadline: block in waitpid until the child terminates.
        for (;;) {
            const pid_t wait_result = ::waitpid(child, &status, 0);
            if (wait_result == child) {
                result.reaped = true;
                break;
            }
            if (wait_result < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        return result;
    }

    for (;;) {
        const pid_t wait_result = ::waitpid(child, &status, WNOHANG);
        if (wait_result == child) {
            result.reaped = true;
            break;
        }
        if (wait_result < 0 && errno != EINTR) {
            break;
        }
        if (timeout_s != 0) {
            const std::uint64_t now = monotonic_now_ns();
            if (now != 0 && start_mono_ns != 0 &&
                now - start_mono_ns >= timeout_s * nanoseconds_per_second) {
                result.runner_stopped = true;
                static_cast<void>(::kill(-child, SIGTERM));
                const std::uint64_t grace_end =
                    monotonic_now_ns() + timeout_grace_seconds *
                                             nanoseconds_per_second;
                while (monotonic_now_ns() < grace_end) {
                    const pid_t grace_result =
                        ::waitpid(child, &status, WNOHANG);
                    if (grace_result == child) {
                        result.reaped = true;
                        break;
                    }
                    sleep_ms(poll_interval_ms);
                }
                if (!result.reaped) {
                    static_cast<void>(::kill(-child, SIGKILL));
                    while (::waitpid(child, &status, 0) < 0 &&
                           errno == EINTR) {
                    }
                    result.reaped = true;
                }
                break;
            }
        }
        sleep_ms(poll_interval_ms);
    }
    return result;
}

[[nodiscard]] TerminatedBy attribution(bool runner_stopped, bool reaped,
                                        int status) noexcept {
    if (runner_stopped) {
        return TerminatedBy::Runner;
    }
    if (!reaped) {
        return TerminatedBy::Unknown;
    }
    if (WIFEXITED(status)) {
        return TerminatedBy::Self;
    }
    if (WIFSIGNALED(status)) {
        const int signal_number = WTERMSIG(status);
        if (signal_number == SIGTERM || signal_number == SIGKILL) {
            return TerminatedBy::Runner;
        }
    }
    return TerminatedBy::Unknown;
}

void rewrite_terminated_by(const std::string& exit_path,
                           TerminatedBy value) noexcept {
    core::Result<nlohmann::json> document = read_json_file(exit_path);
    if (!document) {
        return;
    }
    ExitMetadata metadata{};
    from_json(*document, metadata);
    metadata.terminated_by = value;
    nlohmann::json updated;
    to_json(updated, metadata);
    static_cast<void>(write_json_file(exit_path, updated));
}

void write_fallback_exit(const std::string& exit_path,
                         const ResourceMetadata& resource,
                         std::optional<std::int32_t> signal_number,
                         TerminatedBy attribution_value) noexcept {
    const std::uint64_t now = monotonic_now_ns();
    std::uint64_t uptime = 0;
    if (now != 0 && resource.start_time_mono_ns != 0 &&
        now >= resource.start_time_mono_ns) {
        uptime = (now - resource.start_time_mono_ns) / nanoseconds_per_second;
    }
    ExitMetadata metadata;
    metadata.signal = signal_number;
    metadata.uptime_s = uptime;
    metadata.terminated_by = attribution_value;
    nlohmann::json document;
    to_json(document, metadata);
    static_cast<void>(write_json_file(exit_path, document));
}

void reap_child(pid_t child) noexcept {
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
}

struct StartLayout {
    app::RunIdentity resolved{};
    std::string instance_dir{};
    std::string stream_root{};
    std::string exit_path{};
};

[[nodiscard]] core::Status resolve_start_layout(
    const app::RunIdentity& identity, const StartOptions& options,
    StartLayout& out) noexcept {
    app::RunIdentity resolved = identity;
    if (resolved.instance_dir.empty()) {
        if (resolved.run_dir.empty() || resolved.app_name.empty() ||
            resolved.instance_id.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        resolved.instance_dir = resolved.run_dir + "/" + resolved.app_name +
                                "/" + resolved.instance_id;
    }
    while (resolved.instance_dir.size() > 1 &&
           resolved.instance_dir.back() == '/') {
        resolved.instance_dir.pop_back();
    }
    if (resolved.instance_dir.empty() ||
        resolved.instance_dir.size() > max_instance_dir_bytes) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const core::Status valid = app::validate_run_identity(resolved);
    if (!valid) {
        return valid;
    }

    out.instance_dir = resolved.instance_dir;
    out.stream_root =
        options.artifact_dir.empty() ? out.instance_dir : options.artifact_dir;
    if (out.stream_root.size() > max_instance_dir_bytes) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    // One live instance per directory: refuse to clobber an unfinished run.
    const std::string resource_path =
        instance_file_path(out.instance_dir, "resource.json");
    out.exit_path = instance_file_path(out.instance_dir, "exit.json");
    if (file_exists(resource_path) && !file_exists(out.exit_path)) {
        return std::unexpected(core::ErrorCode::Conflict);
    }
    out.resolved = std::move(resolved);
    return {};
}

[[nodiscard]] core::Status prepare_start_layout(
    const StartLayout& layout, const StartOptions& options) noexcept {
    const core::Status created = create_instance_dir(layout.instance_dir);
    if (!created) {
        return created;
    }
    if (layout.stream_root != layout.instance_dir) {
        const core::Status artifact_created =
            create_artifact_dir(layout.stream_root);
        if (!artifact_created) {
            return artifact_created;
        }
    }

    app::TelemetryPaths resolved_paths{};
    const core::Status paths_status = app::resolve_telemetry_paths(
        layout.resolved, "", "", "", resolved_paths);
    if (!paths_status) {
        return paths_status;
    }
    const bool suite_settings =
        !options.artifact_dir.empty() ||
        !options.global_settings_dir.empty() ||
        !options.local_settings_dir.empty() ||
        options.log_level.has_value();
    const core::Status settings_status =
        suite_settings
            ? write_suite_settings(layout.instance_dir, layout.stream_root,
                                   options, layout.resolved)
            : generate_settings(layout.instance_dir, resolved_paths);
    if (!settings_status) {
        return settings_status;
    }

    // The deterministic layout lists all four JSONL streams; make sure they
    // exist before the child starts (the sinks append to them).
    for (const char* stream : {"app.jsonl", "metrics.jsonl", "traces.jsonl",
                               "chrono.jsonl"}) {
        const std::string stream_file =
            instance_file_path(layout.stream_root, stream);
        const int stream_fd =
            ::open(stream_file.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
        if (stream_fd < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        static_cast<void>(::close(stream_fd));
    }

    // A new run supersedes any previous completion record.
    static_cast<void>(::unlink(layout.exit_path.c_str()));
    return {};
}

[[nodiscard]] core::Status open_start_logs(std::string_view stream_root,
                                           int& stdout_fd, int& stderr_fd,
                                           int& null_fd) noexcept {
    stdout_fd = open_truncated_log(
        instance_file_path(stream_root, "stdout.log"));
    if (stdout_fd < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    stderr_fd = open_truncated_log(
        instance_file_path(stream_root, "stderr.log"));
    if (stderr_fd < 0) {
        static_cast<void>(::close(stdout_fd));
        stdout_fd = -1;
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (core::detail::fault(core::detail::FaultPoint::Runner_open_null)) {
        static_cast<void>(::close(stdout_fd));
        static_cast<void>(::close(stderr_fd));
        stdout_fd = -1;
        stderr_fd = -1;
        return std::unexpected(core::ErrorCode::SystemError);
    }
    null_fd = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    if (null_fd < 0) {
        static_cast<void>(::close(stdout_fd));
        static_cast<void>(::close(stderr_fd));
        stdout_fd = -1;
        stderr_fd = -1;
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

struct SupervisorContext {
    int report_fd{-1};
    int stdout_fd{-1};
    int stderr_fd{-1};
    int null_fd{-1};
    const StartOptions* options{nullptr};
    const app::RunIdentity* resolved{nullptr};
    const std::string* instance_dir{nullptr};
    const std::vector<const char*>* argv{nullptr};
    std::uint64_t timeout_s{0};
    std::string log_level{};
    ResourceMetadata metadata{};
};

[[noreturn]] void run_instance_supervisor(SupervisorContext& context) noexcept {
    static_cast<void>(reset_runner_affinity());
    static_cast<void>(::setsid());
    redirect_to_null(context.null_fd);

    // The child signals completion of setsid()/dup2() through this pipe;
    // resource.json is written only afterwards, so `start` never returns
    // before the process group exists.
    int setup_pipe[2]{-1, -1};
    if (::pipe2(setup_pipe, O_CLOEXEC) != 0) {
        StartReport report{
            0, static_cast<std::int32_t>(core::ErrorCode::SystemError)};
        static_cast<void>(
            write_all(context.report_fd, &report, sizeof(report)));
        static_cast<void>(::close(context.report_fd));
        child_exit(1);
    }

    const pid_t child = ::fork();
    if (child < 0) {
        StartReport report{
            0, static_cast<std::int32_t>(core::ErrorCode::SystemError)};
        static_cast<void>(
            write_all(context.report_fd, &report, sizeof(report)));
        static_cast<void>(::close(context.report_fd));
        static_cast<void>(::close(setup_pipe[0]));
        static_cast<void>(::close(setup_pipe[1]));
        child_exit(1);
    }

    if (child == 0) {
        // --- Application child ---
        static_cast<void>(::close(context.report_fd));
        static_cast<void>(::close(setup_pipe[0]));
        static_cast<void>(reset_runner_affinity());
        auto fail_setup = [&](std::int32_t setup_status) {
            const ssize_t written = ::write(setup_pipe[1], &setup_status,
                                            sizeof(setup_status));
            static_cast<void>(written);
            static_cast<void>(::close(setup_pipe[1]));
            child_exit(126);
        };
        if (::setsid() < 0) {
            fail_setup(1);
        }
        if (::dup2(context.null_fd, STDIN_FILENO) < 0 ||
            ::dup2(context.stdout_fd, STDOUT_FILENO) < 0 ||
            ::dup2(context.stderr_fd, STDERR_FILENO) < 0) {
            fail_setup(1);
        }
        if (context.null_fd > STDERR_FILENO) {
            static_cast<void>(::close(context.null_fd));
        }
        if (context.stdout_fd > STDERR_FILENO) {
            static_cast<void>(::close(context.stdout_fd));
        }
        if (context.stderr_fd > STDERR_FILENO) {
            static_cast<void>(::close(context.stderr_fd));
        }

        if (!context.options->cpus.empty()) {
            cpu_set_t mask;
            CPU_ZERO(&mask);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
            for (const std::uint32_t cpu : context.options->cpus) {
                CPU_SET(cpu, &mask);
            }
#pragma GCC diagnostic pop
            if (::sched_setaffinity(0, sizeof(mask), &mask) != 0) {
                fail_setup(2);
            }
        }

        for (const auto& [key, value] : context.options->env) {
            if (::setenv(key.c_str(), value.c_str(), 1) != 0) {
                fail_setup(3);
            }
        }

        const app::RunIdentity& resolved = *context.resolved;
        static_cast<void>(
            ::setenv("NEXIGEN_RUN_ID", resolved.run_id.c_str(), 1));
        static_cast<void>(
            ::setenv("NEXIGEN_RUN_DIR", resolved.run_dir.c_str(), 1));
        static_cast<void>(
            ::setenv("NEXIGEN_APP_NAME", resolved.app_name.c_str(), 1));
        static_cast<void>(
            ::setenv("NEXIGEN_INSTANCE_ID", resolved.instance_id.c_str(), 1));
        static_cast<void>(::setenv("NEXIGEN_INSTANCE_DIR",
                                   context.instance_dir->c_str(), 1));
        if (!context.log_level.empty()) {
            static_cast<void>(::setenv("NEXIGEN_LOG_LEVEL",
                                       context.log_level.c_str(), 1));
        }

        const std::int32_t setup_status = 0;
        const ssize_t ready_written =
            ::write(setup_pipe[1], &setup_status, sizeof(setup_status));
        static_cast<void>(ready_written);
        static_cast<void>(::close(setup_pipe[1]));

        detail::coverage_flush();
        // POSIX declares execvp's `argv` as `char* const[]`, but it never
        // mutates the argument strings. Cast away the inner const once, here,
        // instead of per-element in the argv construction above.
        ::execvp((*context.argv)[0], const_cast<char**>(context.argv->data()));
        child_exit(127);
    }

    // --- Supervisor parent of the application child ---
    static_cast<void>(::close(setup_pipe[1]));
    std::int32_t setup_status = 0;
    const bool setup_read =
        read_all(setup_pipe[0], &setup_status, sizeof(setup_status));
    static_cast<void>(::close(setup_pipe[0]));

    if (context.null_fd > STDERR_FILENO) {
        static_cast<void>(::close(context.null_fd));
    }
    if (context.stdout_fd > STDERR_FILENO) {
        static_cast<void>(::close(context.stdout_fd));
    }
    if (context.stderr_fd > STDERR_FILENO) {
        static_cast<void>(::close(context.stderr_fd));
    }

    if (!setup_read || setup_status != 0) {
        static_cast<void>(::kill(-child, SIGKILL));
        reap_child(child);
        StartReport report{
            0, static_cast<std::int32_t>(core::ErrorCode::SystemError)};
        static_cast<void>(
            write_all(context.report_fd, &report, sizeof(report)));
        static_cast<void>(::close(context.report_fd));
        child_exit(1);
    }

    context.metadata.pid = child;
    context.metadata.ppid = ::getpid();
    const core::Status resource_status =
        write_resource_json(*context.instance_dir, context.metadata);
    if (!resource_status) {
        static_cast<void>(::kill(-child, SIGKILL));
        reap_child(child);
        StartReport report{
            0, static_cast<std::int32_t>(core::ErrorCode::SystemError)};
        static_cast<void>(
            write_all(context.report_fd, &report, sizeof(report)));
        static_cast<void>(::close(context.report_fd));
        child_exit(1);
    }

    StartReport report{context.metadata.pid, 0};
    static_cast<void>(write_all(context.report_fd, &report, sizeof(report)));
    static_cast<void>(::close(context.report_fd));

    int child_status = 0;
    const SuperviseResult supervised = supervise_child(
        child, context.timeout_s, context.metadata.start_time_mono_ns,
        child_status);
    const std::uint64_t end_ns = monotonic_now_ns();
    std::uint64_t uptime = 0;
    if (supervised.reaped && end_ns != 0 &&
        context.metadata.start_time_mono_ns != 0 &&
        end_ns >= context.metadata.start_time_mono_ns) {
        uptime = (end_ns - context.metadata.start_time_mono_ns) /
                 nanoseconds_per_second;
    }

    const TerminatedBy terminated_by = attribution(
        supervised.runner_stopped, supervised.reaped, child_status);
    const ExitMetadata exit_metadata = ExitMetadata::from_child_status(
        child, child_status, uptime, terminated_by);
    static_cast<void>(write_exit_json(*context.instance_dir, exit_metadata));
    child_exit(0);
}

} // namespace

std::string StartOptions::to_string() const {
    return std::format(
        "StartOptions(artifact_dir={}, global_settings_dir={}, "
        "local_settings_dir={}, log_level={}, env={}, cpus={})",
        artifact_dir, global_settings_dir, local_settings_dir,
        log_level.has_value() ? std::string(logging::to_string(*log_level))
                              : std::string("inherit"),
        env.size(), cpus.size());
}

core::Status Runner::start(const app::RunIdentity& identity,
                           const std::vector<std::string>& command,
                           std::uint64_t timeout_s) {
    return start(identity, command, timeout_s, StartOptions{});
}

core::Status Runner::start(const app::RunIdentity& identity,
                           const std::vector<std::string>& command,
                           std::uint64_t timeout_s,
                           const StartOptions& options) {
    if (command.empty() || command.front().empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const core::Status options_valid = validate_start_options(options);
    if (!options_valid) {
        return options_valid;
    }

    StartLayout layout;
    const core::Status resolved_status =
        resolve_start_layout(identity, options, layout);
    if (!resolved_status) {
        return resolved_status;
    }
    const core::Status prepared = prepare_start_layout(layout, options);
    if (!prepared) {
        return prepared;
    }

    int stdout_fd = -1;
    int stderr_fd = -1;
    int null_fd = -1;
    const core::Status logs =
        open_start_logs(layout.stream_root, stdout_fd, stderr_fd, null_fd);
    if (!logs) {
        return logs;
    }

    std::vector<const char*> argv;
    argv.reserve(command.size() + 1);
    for (const std::string& argument : command) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    ResourceMetadata metadata =
        ResourceMetadata::from_identity(layout.resolved, 0, 0);

    int pipe_fds[2]{-1, -1};
    if (core::detail::fault(core::detail::FaultPoint::Runner_pipe2) ||
        ::pipe2(pipe_fds, O_CLOEXEC) != 0) {
        static_cast<void>(::close(stdout_fd));
        static_cast<void>(::close(stderr_fd));
        static_cast<void>(::close(null_fd));
        return std::unexpected(core::ErrorCode::SystemError);
    }

    SupervisorContext context;
    context.report_fd = pipe_fds[1];
    context.stdout_fd = stdout_fd;
    context.stderr_fd = stderr_fd;
    context.null_fd = null_fd;
    context.options = &options;
    context.resolved = &layout.resolved;
    context.instance_dir = &layout.instance_dir;
    context.argv = &argv;
    context.timeout_s = timeout_s;
    context.metadata = metadata;
    if (options.log_level.has_value()) {
        context.log_level = std::string(logging::to_string(*options.log_level));
    }

    const pid_t supervisor =
        core::detail::fault(core::detail::FaultPoint::Runner_fork)
            ? -1
            : ::fork();
    if (supervisor < 0) {
        static_cast<void>(::close(pipe_fds[0]));
        static_cast<void>(::close(pipe_fds[1]));
        static_cast<void>(::close(stdout_fd));
        static_cast<void>(::close(stderr_fd));
        static_cast<void>(::close(null_fd));
        return std::unexpected(core::ErrorCode::SystemError);
    }

    if (supervisor == 0) {
        static_cast<void>(::close(pipe_fds[0]));
        run_instance_supervisor(context);
    }

    // --- Start caller ---
    static_cast<void>(::close(pipe_fds[1]));
    static_cast<void>(::close(stdout_fd));
    static_cast<void>(::close(stderr_fd));
    static_cast<void>(::close(null_fd));

    StartReport report{};
    const bool received = read_all(pipe_fds[0], &report, sizeof(report));
    static_cast<void>(::close(pipe_fds[0]));

    if (!received || report.error != 0 || report.pid <= 0) {
        int supervisor_status = 0;
        static_cast<void>(::waitpid(supervisor, &supervisor_status, WNOHANG));
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

core::Status Runner::start_suite(const SuiteManifest& manifest,
                                 std::string& suite_dir_out) {
    return ::nexigen::runner::start_suite(manifest, suite_dir_out);
}

core::Status Runner::stop(std::string_view instance_dir,
                          std::uint64_t grace_s) {
    if (instance_dir.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const std::string directory(instance_dir);
    const std::string resource_path =
        instance_file_path(directory, "resource.json");
    const std::string exit_path =
        instance_file_path(directory, "exit.json");

    if (file_exists(exit_path)) {
        // Idempotent path: nothing to signal, but success still requires the
        // whole group to be gone, as the stop contract promises.
        const core::Result<nlohmann::json> completed =
            read_json_file(resource_path);
        if (!completed) {
            return {};
        }
        ResourceMetadata completed_metadata{};
        from_json(*completed, completed_metadata);
        if (completed_metadata.pid > 0 &&
            group_alive(completed_metadata.pid)) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    core::Result<nlohmann::json> resource_document =
        read_json_file(resource_path);
    if (!resource_document) {
        return std::unexpected(resource_document.error());
    }
    ResourceMetadata resource{};
    from_json(*resource_document, resource);
    if (resource.pid <= 0) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const std::int32_t pid = resource.pid;

    if (!process_alive(pid) && !group_alive(pid)) {
        // The supervisor may be in the middle of writing exit.json.
        const std::uint64_t wait_end =
            monotonic_now_ns() + nanoseconds_per_second;
        while (monotonic_now_ns() < wait_end && !file_exists(exit_path)) {
            sleep_ms(poll_interval_ms);
        }
        if (!file_exists(exit_path)) {
            write_fallback_exit(exit_path, resource, std::nullopt,
                                TerminatedBy::Unknown);
        }
        return {};
    }

    bool sent_signal = false;
    std::optional<std::int32_t> last_signal;
    if (::kill(-static_cast<pid_t>(pid), SIGTERM) == 0) {
        sent_signal = true;
        last_signal = SIGTERM;
    } else if (errno != ESRCH) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    const std::uint64_t grace_end =
        monotonic_now_ns() + grace_s * nanoseconds_per_second;
    while (monotonic_now_ns() < grace_end) {
        if (file_exists(exit_path) && !group_alive(pid)) {
            break;
        }
        sleep_ms(poll_interval_ms);
    }

    if (group_alive(pid) || !file_exists(exit_path)) {
        if (::kill(-static_cast<pid_t>(pid), SIGKILL) == 0) {
            sent_signal = true;
            last_signal = SIGKILL;
        } else if (errno != ESRCH) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
    }

    const std::uint64_t settle_end =
        monotonic_now_ns() + stop_settle_seconds * nanoseconds_per_second;
    while (monotonic_now_ns() < settle_end) {
        if (file_exists(exit_path) && !group_alive(pid)) {
            break;
        }
        sleep_ms(poll_interval_ms);
    }

    if (file_exists(exit_path)) {
        if (sent_signal) {
            rewrite_terminated_by(exit_path, TerminatedBy::Runner);
        }
        if (group_alive(pid)) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    write_fallback_exit(exit_path, resource, last_signal,
                        sent_signal ? TerminatedBy::Runner
                                    : TerminatedBy::Unknown);
    if (group_alive(pid)) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

core::Status Runner::status(std::string_view instance_dir,
                            std::string& state) noexcept {
    state = "unknown";
    if (instance_dir.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const std::string directory(instance_dir);
    core::Result<nlohmann::json> resource_document =
        read_json_file(instance_file_path(directory, "resource.json"));
    if (!resource_document) {
        return {};
    }
    ResourceMetadata resource{};
    from_json(*resource_document, resource);
    if (resource.pid <= 0) {
        return {};
    }

    if (process_alive(resource.pid)) {
        state = "running";
        return {};
    }

    core::Result<nlohmann::json> exit_document =
        read_json_file(instance_file_path(directory, "exit.json"));
    if (!exit_document) {
        // The process is gone and no completion record exists; a supervisor
        // may still be about to publish one, but the state is already final.
        state = "exited";
        return {};
    }
    ExitMetadata exit_metadata{};
    from_json(*exit_document, exit_metadata);
    state = exit_metadata.terminated_by == TerminatedBy::Runner ? "stopped"
                                                                 : "exited";
    return {};
}

} // namespace nexigen::runner
