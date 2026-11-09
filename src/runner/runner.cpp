// nexigen::runner — process supervisor implementation
// (change nexigen-run; spec runner/process-supervision).
//
// COLD PATH ONLY. The runner forks a detached supervisor, which forks and
// reaps the application child. `start` returns as soon as the child PID is
// recorded; `stop` and `status` derive everything from the instance directory.
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/app/app_settings.hpp>
#include <nexigen/core/detail/fault_injection.hpp>
#include <nexigen/logging/logger_options.hpp>
#include <nexigen/runner/detail/cpu_validation.hpp>
#include <nexigen/runner/detail/path_utils.hpp>
#include <nexigen/runner/detail/sys_helpers.hpp>
#include <nexigen/runner/instance_lifecycle.hpp>
#include <nexigen/runner/runner.hpp>
#include <nexigen/runner/runner_logging.hpp>
#include <nexigen/runner/suite_control.hpp>

#include "coverage_flush.hpp"

namespace nexigen::runner {

using detail::absolute_path;
using detail::child_exit;
using detail::cpus_within_online;
using detail::file_exists;
using detail::group_alive;
using detail::identity_matches;
using detail::max_path_bytes;
using detail::monotonic_now_ns;
using detail::nanoseconds_per_second;
using detail::process_alive;
using detail::proc_start_ticks;
using detail::read_all;
using detail::read_start_report;
using detail::redirect_to_null;
using detail::sleep_ms;
using detail::StartReport;
using detail::validate_env_entries;
using detail::write_start_report;

namespace {

constexpr std::uint64_t timeout_grace_seconds = 5;
constexpr std::uint64_t stop_settle_seconds = 5;
constexpr std::uint64_t poll_interval_ms = 20;

/// Create (or truncate) then reopen a log file in append mode. The truncation
/// happens on every start, as required by the spec.
[[nodiscard]] int open_truncated_log(const std::string& path) noexcept {
    const int truncate_fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (truncate_fd < 0) {
        return -1;
    }
    static_cast<void>(::close(truncate_fd));
    return ::open(path.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC, 0644);
}

[[nodiscard]] core::Status validate_artifact_dir(std::string_view artifact_dir) noexcept {
    if (!artifact_dir.empty() &&
        (!absolute_path(artifact_dir) || artifact_dir.size() > max_path_bytes)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

[[nodiscard]] core::Status validate_settings_dirs(std::string_view global_settings_dir,
                                                  std::string_view local_settings_dir) noexcept {
    if (!global_settings_dir.empty() && !absolute_path(global_settings_dir)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!local_settings_dir.empty() && !absolute_path(local_settings_dir)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

[[nodiscard]] core::Status validate_cpu_set(const core::CpuSet& cpus) noexcept {
    return cpus_within_online(cpus) ? core::Status{}
                                    : std::unexpected(core::ErrorCode::InvalidArgument);
}

[[nodiscard]] core::Status validate_start_options(const StartOptions& options) noexcept {
    if (const core::Status status = validate_artifact_dir(options.artifact_dir); !status) {
        return status;
    }
    if (const core::Status status =
            validate_settings_dirs(options.global_settings_dir, options.local_settings_dir);
        !status) {
        return status;
    }
    if (const core::Status status = validate_env_entries(options.env); !status) {
        return status;
    }
    return validate_cpu_set(options.cpus);
}

[[nodiscard]] nlohmann::json logging_document(const logging::LoggerOptions& options,
                                              std::string_view stream_root,
                                              const std::optional<core::LogLevel> level) {
    nlohmann::json document;
    to_json(document, options);
    document["log_root_directory"] = std::string(stream_root);
    if (level.has_value()) {
        document["initial_log_level"] = std::string(core::to_string(*level));
    }
    document["sink"] =
        nlohmann::json{{"path", instance_file_path(stream_root, "app.jsonl")}, {"append", true}};
    return document;
}

[[nodiscard]] core::Status write_suite_settings(std::string_view instance_dir,
                                                std::string_view stream_root,
                                                const StartOptions& options) noexcept {
    app::Settings settings{};
    if (!options.global_settings_dir.empty() || !options.local_settings_dir.empty()) {
        app::AppSettingsPaths paths{};
        auto make_pair = [](std::string_view global, std::string_view local, const char* name) {
            core::SettingsPaths entry{};
            if (!global.empty()) {
                entry.global_file = std::string(global) + "/" + name;
            }
            if (!local.empty()) {
                entry.local_file = std::string(local) + "/" + name;
            }
            return entry;
        };
        paths.logging =
            make_pair(options.global_settings_dir, options.local_settings_dir, "logging.json");
        paths.metrics =
            make_pair(options.global_settings_dir, options.local_settings_dir, "metrics.json");
        paths.tracing =
            make_pair(options.global_settings_dir, options.local_settings_dir, "tracing.json");
        paths.chrono.global_file = options.global_settings_dir.empty()
                                       ? std::string{}
                                       : std::string(options.global_settings_dir) + "/chrono.json";
        paths.chrono.local_file = options.local_settings_dir.empty()
                                      ? std::string{}
                                      : std::string(options.local_settings_dir) + "/chrono.json";
        const core::Status loaded = app::load_app_settings(paths, settings);
        if (!loaded) {
            return loaded;
        }
    }

    settings.logging.options.log_root_directory = std::string(stream_root);
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
    metrics_json["sink"] = nlohmann::json{{"path", settings.metrics.sink.path},
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
    chrono_json["sink"] = nlohmann::json{{"path", settings.chrono.sink.path},
                                         {"append", settings.chrono.sink.append}};
    return write("chrono.json", chrono_json);
}

/// Wait for the child while enforcing the optional idle timeout. When the
/// deadline expires, run the same SIGTERM → grace → SIGKILL sequence as
/// `Runner::stop`.
struct SuperviseResult {
    bool reaped{false};
    bool runner_stopped{false};
    /// `status` carries a real waitpid outcome. Only a successful waitpid
    /// sets this: waitpid errors (ECHILD after a foreign reap, and any
    /// error ≠ EINTR) leave it false so callers never fabricate a kid exit
    /// (spec process-supervision "Unreapable child errors do not fabricate
    /// success"; analysis #6).
    bool status_valid{false};
};

[[nodiscard]] SuperviseResult wait_child_blocking(pid_t child, int& status) noexcept {
    SuperviseResult result;
    for (;;) {
        const pid_t wait_result = ::waitpid(child, &status, 0);
        if (wait_result == child) {
            result.reaped = true;
            result.status_valid = true;
            break;
        }
        if (wait_result < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    return result;
}

[[nodiscard]] bool send_process_signal(pid_t child, int signal) noexcept {
    return ::kill(-child, signal) == 0;
}

[[nodiscard]] SuperviseResult wait_child_deadline(pid_t child, int& status, std::uint64_t timeout_s,
                                                  std::uint64_t start_mono_ns) noexcept {
    SuperviseResult result;
    for (;;) {
        const pid_t wait_result = ::waitpid(child, &status, WNOHANG);
        if (wait_result == child) {
            result.reaped = true;
            result.status_valid = true;
            break;
        }
        if (wait_result < 0 && errno != EINTR) {
            break;
        }
        const std::uint64_t now = monotonic_now_ns();
        if (now != 0 && start_mono_ns != 0 &&
            now - start_mono_ns >= timeout_s * nanoseconds_per_second) {
            result.runner_stopped = true;
            static_cast<void>(send_process_signal(child, SIGTERM));
            const std::uint64_t grace_end =
                monotonic_now_ns() + timeout_grace_seconds * nanoseconds_per_second;
            while (monotonic_now_ns() < grace_end) {
                const pid_t grace_result = ::waitpid(child, &status, WNOHANG);
                if (grace_result == child) {
                    result.reaped = true;
                    result.status_valid = true;
                    break;
                }
                sleep_ms(poll_interval_ms);
            }
            if (!result.reaped) {
                static_cast<void>(send_process_signal(child, SIGKILL));
                // Honest-outcome rule (spec process-supervision
                // "Unreapable child errors do not fabricate success"):
                // an error here (ECHILD — reaped elsewhere; or anything
                // ≠ EINTR) must NOT be presented as a reap with the old
                // standing status. Only a real waitpid success validates.
                const pid_t final_wait = ::waitpid(child, &status, 0);
                if (final_wait == child) {
                    result.reaped = true;
                    result.status_valid = true;
                }
            }
            break;
        }
        sleep_ms(poll_interval_ms);
    }
    return result;
}

[[nodiscard]] SuperviseResult supervise_child(pid_t child, std::uint64_t timeout_s,
                                              std::uint64_t start_mono_ns, int& status) noexcept {
    if (timeout_s == 0) {
        return wait_child_blocking(child, status);
    }
    return wait_child_deadline(child, status, timeout_s, start_mono_ns);
}

[[nodiscard]] TerminatedBy attribution(bool runner_stopped, bool reaped, int status) noexcept {
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

void rewrite_terminated_by(const std::string& exit_path, TerminatedBy value) noexcept {
    ExitMetadata metadata{};
    if (!read_json_into(exit_path, metadata)) {
        return;
    }
    metadata.terminated_by = value;
    nlohmann::json updated;
    to_json(updated, metadata);
    static_cast<void>(write_json_file(exit_path, updated));
}

void write_fallback_exit(const std::string& exit_path, const ResourceMetadata& resource,
                         std::optional<std::int32_t> signal_number,
                         TerminatedBy attribution_value) noexcept {
    const std::uint64_t now = monotonic_now_ns();
    std::uint64_t uptime = 0;
    if (now != 0 && resource.start_time_mono_ns != 0 && now >= resource.start_time_mono_ns) {
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
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
}

struct StartLayout {
    app::RunIdentity resolved{};
    std::string instance_dir{};
    std::string stream_root{};
    std::string exit_path{};
};

[[nodiscard]] core::Status resolve_start_layout(const app::RunIdentity& identity,
                                                std::string_view app_name,
                                                const StartOptions& options,
                                                StartLayout& out) noexcept {
    app::RunIdentity resolved = identity;
    if (resolved.instance_dir.empty()) {
        if (resolved.run_dir.empty() || app_name.empty() || resolved.instance_id.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        resolved.instance_dir =
            resolved.run_dir + "/" + std::string(app_name) + "/" + resolved.instance_id;
    }
    while (resolved.instance_dir.size() > 1 && resolved.instance_dir.back() == '/') {
        resolved.instance_dir.pop_back();
    }
    if (resolved.instance_dir.empty() || resolved.instance_dir.size() > max_path_bytes) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const core::Status valid = app::validate_run_identity(resolved);
    if (!valid) {
        return valid;
    }

    out.instance_dir = resolved.instance_dir;
    out.stream_root = options.artifact_dir.empty() ? out.instance_dir : options.artifact_dir;
    if (out.stream_root.size() > max_path_bytes) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    // One live instance per directory: refuse to clobber an unfinished run.
    const std::string resource_path = instance_file_path(out.instance_dir, "resource.json");
    out.exit_path = instance_file_path(out.instance_dir, "exit.json");
    if (file_exists(resource_path) && !file_exists(out.exit_path)) {
        return std::unexpected(core::ErrorCode::Conflict);
    }
    out.resolved = std::move(resolved);
    return {};
}

[[nodiscard]] core::Status prepare_start_layout(const StartLayout& layout,
                                                const StartOptions& options) noexcept {
    const core::Status created = create_instance_dir(layout.instance_dir);
    if (!created) {
        return created;
    }
    if (layout.stream_root != layout.instance_dir) {
        const core::Status artifact_created = create_artifact_dir(layout.stream_root);
        if (!artifact_created) {
            return artifact_created;
        }
    }

    app::TelemetryPaths resolved_paths{};
    const core::Status paths_status =
        app::resolve_telemetry_paths(layout.resolved, "", "", "", resolved_paths);
    if (!paths_status) {
        return paths_status;
    }
    const bool suite_settings =
        !options.artifact_dir.empty() || !options.global_settings_dir.empty() ||
        !options.local_settings_dir.empty() || options.log_level.has_value();
    const core::Status settings_status =
        suite_settings ? write_suite_settings(layout.instance_dir, layout.stream_root, options)
                       : generate_settings(layout.instance_dir, resolved_paths);
    if (!settings_status) {
        return settings_status;
    }

    // The deterministic layout lists all four JSONL streams; make sure they
    // exist before the child starts (the sinks append to them).
    for (const char* stream : {"app.jsonl", "metrics.jsonl", "traces.jsonl", "chrono.jsonl"}) {
        const std::string stream_file = instance_file_path(layout.stream_root, stream);
        const int stream_fd = ::open(stream_file.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
        if (stream_fd < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        static_cast<void>(::close(stream_fd));
    }

    // A new run supersedes any previous completion record.
    static_cast<void>(::unlink(layout.exit_path.c_str()));
    return {};
}

[[nodiscard]] core::Status open_start_logs(std::string_view stream_root, int& stdout_fd,
                                           int& stderr_fd, int& null_fd) noexcept {
    stdout_fd = open_truncated_log(instance_file_path(stream_root, "stdout.log"));
    if (stdout_fd < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    stderr_fd = open_truncated_log(instance_file_path(stream_root, "stderr.log"));
    if (stderr_fd < 0) {
        static_cast<void>(::close(stdout_fd));
        stdout_fd = -1;
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (core::fault_injection::fault(core::fault_injection::FaultPoint::Runner_open_null)) {
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

[[noreturn]] void report_supervisor_failure(
    int report_fd,
    std::int32_t error_code = static_cast<std::int32_t>(core::ErrorCode::SystemError),
    int exit_code = 1) noexcept {
    StartReport report{0, error_code};
    static_cast<void>(write_start_report(report_fd, report));
    static_cast<void>(::close(report_fd));
    child_exit(exit_code);
}

[[nodiscard]] bool setup_exec_pipe(int report_fd, int setup_pipe[2]) noexcept {
    if (::pipe2(setup_pipe, O_CLOEXEC) != 0) {
        report_supervisor_failure(report_fd);
    }
    return true;
}

[[noreturn]] void launch_application_child(SupervisorContext& context, int setup_pipe[2]) {
    // --- Application child ---
    static_cast<void>(::close(context.report_fd));
    static_cast<void>(::close(setup_pipe[0]));
    static_cast<void>(reset_runner_affinity());
    auto fail_setup = [&](std::int32_t setup_status) {
        // SIGPIPE shielded (spec process-supervision; analysis #23 —
        // the parent may already have closed the pipe on a failure).
        static_cast<void>(
            detail::write_all_no_sigpipe(setup_pipe[1], &setup_status, sizeof(setup_status)));
        static_cast<void>(::close(setup_pipe[1]));
        child_exit(126);
    };
    if (::setsid() < 0) {
        fail_setup(1);
    }
    if (::dup2(context.null_fd, STDIN_FILENO) < 0 || ::dup2(context.stdout_fd, STDOUT_FILENO) < 0 ||
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
        core::thread_management_detail::fill_native_cpu_set(context.options->cpus, mask);
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
    static_cast<void>(::setenv("NEXIGEN_RUN_ID", resolved.run_id.c_str(), 1));
    static_cast<void>(::setenv("NEXIGEN_RUN_DIR", resolved.run_dir.c_str(), 1));
    static_cast<void>(::setenv("NEXIGEN_INSTANCE_ID", resolved.instance_id.c_str(), 1));
    static_cast<void>(::setenv("NEXIGEN_INSTANCE_DIR", context.instance_dir->c_str(), 1));
    if (!context.log_level.empty()) {
        static_cast<void>(::setenv("NEXIGEN_LOG_LEVEL", context.log_level.c_str(), 1));
    }

    detail::coverage_flush();
    // POSIX declares execvp's `argv` as `char* const[]`, but it never
    // mutates the argument strings. Cast away the inner const once, here,
    // instead of per-element in the argv construction above.
    ::execvp((*context.argv)[0], const_cast<char**>(context.argv->data()));
    // Exec-correct handshake (spec runner/process-supervision "Setup
    // handshake completes only after a successful exec"): only failures
    // write through the pipe; the exec'd image consumes its O_CLOEXEC fd
    // so the supervisor observes EOF as success. On exec failure the fd
    // is still ours — report status 4 (exec) before dying.
    const std::int32_t exec_status = 4;
    static_cast<void>(
        detail::write_all_no_sigpipe(setup_pipe[1], &exec_status, sizeof(exec_status)));
    static_cast<void>(::close(setup_pipe[1]));
    child_exit(127);
}

[[nodiscard]] bool await_exec_handshake(int setup_read_fd) noexcept {
    std::int32_t setup_status = 0;
    // Exec-correct handshake (spec runner/process-supervision): the child
    // writes a nonzero setup status ONLY on failure (1 setsid, 2 affinity,
    // 3 setenv, 4 exec); a successful `execvp` closes the child's O_CLOEXEC
    // fd, so EOF is the success signal. A full 4-byte read therefore always
    // means a reported setup/exec failure (the `!= 0` check stays as a
    // protocol belt; zero is never written on purpose).
    const bool failure_reported = read_all(setup_read_fd, &setup_status, sizeof(setup_status));
    static_cast<void>(::close(setup_read_fd));
    return failure_reported || setup_status != 0;
}

void close_supervisor_fds(int null_fd, int stdout_fd, int stderr_fd) noexcept {
    if (null_fd > STDERR_FILENO) {
        static_cast<void>(::close(null_fd));
    }
    if (stdout_fd > STDERR_FILENO) {
        static_cast<void>(::close(stdout_fd));
    }
    if (stderr_fd > STDERR_FILENO) {
        static_cast<void>(::close(stderr_fd));
    }
}

[[noreturn]] void finalize_supervision(SupervisorContext& context, pid_t child) noexcept {
    context.metadata.pid = child;
    context.metadata.ppid = ::getpid();
    // Identity anchor: read the child's starttime ticks while the supervisor
    // owns it (spec runner/process-supervision "Identity is verified before
    // signals are sent"; analysis #5 PID-recycling). An unreadable /proc
    // records 0 and keeps the legacy pid-only stop behavior.
    context.metadata.proc_start_ticks = detail::proc_start_ticks(child);
    const core::Status resource_status =
        write_resource_json(*context.instance_dir, context.metadata);
    if (!resource_status) {
        static_cast<void>(::kill(-child, SIGKILL));
        reap_child(child);
        report_supervisor_failure(context.report_fd);
    }

    StartReport report{context.metadata.pid, 0};
    static_cast<void>(write_start_report(context.report_fd, report));
    static_cast<void>(::close(context.report_fd));

    int child_status = 0;
    const SuperviseResult supervised = supervise_child(
        child, context.timeout_s, context.metadata.start_time_mono_ns, child_status);
    const std::uint64_t end_ns = monotonic_now_ns();
    std::uint64_t uptime = 0;
    if (supervised.reaped && end_ns != 0 && context.metadata.start_time_mono_ns != 0 &&
        end_ns >= context.metadata.start_time_mono_ns) {
        uptime = (end_ns - context.metadata.start_time_mono_ns) / nanoseconds_per_second;
    }

    if (supervised.status_valid) {
        const TerminatedBy terminated_by =
            attribution(supervised.runner_stopped, supervised.reaped, child_status);
        const ExitMetadata exit_metadata =
            ExitMetadata::from_child_status(child, child_status, uptime, terminated_by);
        static_cast<void>(write_exit_json(*context.instance_dir, exit_metadata));
    } else {
        // Unreapable child (waitpid error ≠ EINTR — analysis #6): the exit
        // record must not assert a stale/foreign status as a real outcome
        // (spec process-supervision "Unreapable child errors do not
        // fabricate success").
        ExitMetadata unknown_metadata{};
        unknown_metadata.uptime_s = uptime;
        unknown_metadata.terminated_by = TerminatedBy::Unknown;
        static_cast<void>(write_exit_json(*context.instance_dir, unknown_metadata));
    }
    child_exit(0);
}

[[noreturn]] void run_instance_supervisor(SupervisorContext& context) noexcept {
    static_cast<void>(reset_runner_affinity());
    static_cast<void>(::setsid());
    redirect_to_null(context.null_fd);

    // The child reports setup/exec failures through this pipe; a successful
    // `execvp` consumes the `O_CLOEXEC` fd and the supervisor reads EOF as
    // success. `resource.json` is written only after the handshake, so
    // `start` never returns success for a failed launch, and any pre-exec
    // setup failure is reported before the group exists.
    int setup_pipe[2]{-1, -1};
    static_cast<void>(setup_exec_pipe(context.report_fd, setup_pipe));

    const pid_t child = ::fork();
    if (child < 0) {
        static_cast<void>(::close(setup_pipe[0]));
        static_cast<void>(::close(setup_pipe[1]));
        report_supervisor_failure(context.report_fd);
    }

    if (child == 0) {
        launch_application_child(context, setup_pipe);
    }

    // --- Supervisor parent of the application child ---
    static_cast<void>(::close(setup_pipe[1]));
    const bool failure_reported = await_exec_handshake(setup_pipe[0]);
    close_supervisor_fds(context.null_fd, context.stdout_fd, context.stderr_fd);

    if (failure_reported) {
        static_cast<void>(::kill(-child, SIGKILL));
        reap_child(child);
        report_supervisor_failure(context.report_fd);
    }

    finalize_supervision(context, child);
}

struct StopSignalTarget {
    ResourceMetadata resource{};
    std::int32_t pid{0};
};

[[nodiscard]] std::optional<StopSignalTarget>
resolve_stop_state(const std::string& resource_path, const std::string& exit_path,
                   core::Status& early_status) noexcept {
    if (file_exists(exit_path)) {
        // Idempotent path: nothing to signal, but success still requires the
        // whole group to be gone, as the stop contract promises. A recycled
        // PID (identity-gate mismatch) says nothing about THIS instance —
        // foreign group liveness implies a foreign process, not a stop
        // failure (spec process-supervision "Identity is verified before
        // signals are sent").
        ResourceMetadata completed_metadata{};
        if (!read_json_into(resource_path, completed_metadata)) {
            return std::nullopt;
        }
        if (completed_metadata.pid > 0 &&
            identity_matches(completed_metadata.pid, completed_metadata.proc_start_ticks) &&
            group_alive(completed_metadata.pid)) {
            early_status = std::unexpected(core::ErrorCode::SystemError);
        }
        return std::nullopt;
    }

    core::Result<nlohmann::json> resource_document = read_json_file(resource_path);
    if (!resource_document) {
        early_status = std::unexpected(resource_document.error());
        return std::nullopt;
    }
    ResourceMetadata resource{};
    from_json(*resource_document, resource);
    if (resource.pid <= 0) {
        early_status = std::unexpected(core::ErrorCode::InvalidArgument);
        return std::nullopt;
    }
    const std::int32_t pid = resource.pid;

    // Identity gate before the first signal (spec process-supervision
    // "Identity is verified before signals are sent"; analysis #5): a live
    // process whose start-ticks differ from resource.json is a recycled PID
    // — no signal mandate. It follows the dead-instance fallback so stop
    // still finalizes the instance (terminated_by "unknown") and reports
    // success without ever touching a foreign process group.
    if (!identity_matches(pid, resource.proc_start_ticks)) {
        write_fallback_exit(exit_path, resource, std::nullopt, TerminatedBy::Unknown);
        return std::nullopt;
    }

    if (!process_alive(pid) && !group_alive(pid)) {
        // The supervisor may be in the middle of writing exit.json.
        const std::uint64_t wait_end = monotonic_now_ns() + nanoseconds_per_second;
        while (monotonic_now_ns() < wait_end && !file_exists(exit_path)) {
            sleep_ms(poll_interval_ms);
        }
        if (!file_exists(exit_path)) {
            write_fallback_exit(exit_path, resource, std::nullopt, TerminatedBy::Unknown);
        }
        return std::nullopt;
    }

    return StopSignalTarget{resource, pid};
}

[[nodiscard]] core::Status send_instance_signal(std::int32_t pid, int signal, bool& sent_signal,
                                                std::optional<std::int32_t>& last_signal) noexcept {
    if (::kill(-static_cast<pid_t>(pid), signal) == 0) {
        sent_signal = true;
        last_signal = signal;
        return {};
    }
    if (errno != ESRCH) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

[[nodiscard]] bool wait_for_exit_or_settle(std::uint64_t deadline_ns, const std::string& exit_path,
                                           std::int32_t pid) noexcept {
    while (monotonic_now_ns() < deadline_ns) {
        if (file_exists(exit_path) && !group_alive(pid)) {
            return true;
        }
        sleep_ms(poll_interval_ms);
    }
    return false;
}

[[nodiscard]] core::Status finalize_stop(const std::string& exit_path, std::int32_t pid,
                                         const ResourceMetadata& resource, bool sent_signal,
                                         std::optional<std::int32_t> last_signal) noexcept {
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
                        sent_signal ? TerminatedBy::Runner : TerminatedBy::Unknown);
    if (group_alive(pid)) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

[[nodiscard]] core::Status validate_start_inputs(const std::vector<std::string>& command,
                                                 const StartOptions& options) noexcept {
    if (command.empty() || command.front().empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return validate_start_options(options);
}

[[nodiscard]] core::Status prepare_instance_layout(const app::RunIdentity& identity,
                                                   std::string_view app_name,
                                                   const StartOptions& options,
                                                   StartLayout& layout) noexcept {
    const core::Status resolved = resolve_start_layout(identity, app_name, options, layout);
    if (!resolved) {
        return resolved;
    }
    return prepare_start_layout(layout, options);
}

[[nodiscard]] std::vector<const char*> build_argv(const std::vector<std::string>& command,
                                                  const std::string& app_name_arg) noexcept {
    std::vector<const char*> argv;
    argv.reserve(command.size() + 3);
    for (const std::string& argument : command) {
        argv.push_back(argument.c_str());
    }
    if (!app_name_arg.empty()) {
        argv.push_back("--app-name");
        argv.push_back(app_name_arg.c_str());
    }
    argv.push_back(nullptr);
    return argv;
}

} // namespace

std::string StartOptions::to_string() const {
    return std::format("StartOptions(artifact_dir={}, global_settings_dir={}, "
                       "local_settings_dir={}, log_level={}, env={}, cpus={})",
                       artifact_dir, global_settings_dir, local_settings_dir,
                       log_level.has_value() ? std::string(core::to_string(*log_level))
                                             : std::string("inherit"),
                       env.size(), cpus.count());
}

core::Status Runner::start(const app::RunIdentity& identity, std::string_view app_name,
                           const std::vector<std::string>& command, std::uint64_t timeout_s) {
    return start(identity, app_name, command, timeout_s, StartOptions{});
}

core::Status Runner::start(const app::RunIdentity& identity, std::string_view app_name,
                           const std::vector<std::string>& command, std::uint64_t timeout_s,
                           const StartOptions& options) {
    if (const core::Status valid = validate_start_inputs(command, options); !valid) {
        return valid;
    }

    StartLayout layout;
    if (const core::Status prepared = prepare_instance_layout(identity, app_name, options, layout);
        !prepared) {
        return prepared;
    }

    int stdout_fd = -1;
    int stderr_fd = -1;
    int null_fd = -1;
    const core::Status logs = open_start_logs(layout.stream_root, stdout_fd, stderr_fd, null_fd);
    if (!logs) {
        return logs;
    }

    const std::string app_name_arg{app_name};
    const std::vector<const char*> argv = build_argv(command, app_name_arg);
    ResourceMetadata metadata = ResourceMetadata::from_identity(layout.resolved, app_name, 0, 0);

    int pipe_fds[2]{-1, -1};
    if (core::fault_injection::fault(core::fault_injection::FaultPoint::Runner_pipe2) ||
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
        context.log_level = std::string(core::to_string(*options.log_level));
    }

    const pid_t supervisor =
        core::fault_injection::fault(core::fault_injection::FaultPoint::Runner_fork) ? -1
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

    return read_start_report(pipe_fds[0], supervisor);
}

core::Status Runner::start_suite(const SuiteManifest& manifest, std::string& suite_dir_out) {
    return ::nexigen::runner::start_suite(manifest, suite_dir_out);
}

core::Status Runner::stop(std::string_view instance_dir, std::uint64_t grace_s) {
    if (instance_dir.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const std::string directory(instance_dir);
    const std::string resource_path = instance_file_path(directory, "resource.json");
    const std::string exit_path = instance_file_path(directory, "exit.json");

    core::Status early_status{};
    const std::optional<StopSignalTarget> target =
        resolve_stop_state(resource_path, exit_path, early_status);
    if (!target.has_value()) {
        return early_status;
    }

    bool sent_signal = false;
    std::optional<std::int32_t> last_signal;
    if (const core::Status sig_status =
            send_instance_signal(target->pid, SIGTERM, sent_signal, last_signal);
        !sig_status) {
        return sig_status;
    }

    const std::uint64_t grace_end = monotonic_now_ns() + grace_s * nanoseconds_per_second;
    static_cast<void>(wait_for_exit_or_settle(grace_end, exit_path, target->pid));

    if (group_alive(target->pid) || !file_exists(exit_path)) {
        if (const core::Status sig_status =
                send_instance_signal(target->pid, SIGKILL, sent_signal, last_signal);
            !sig_status) {
            return sig_status;
        }
    }

    const std::uint64_t settle_end =
        monotonic_now_ns() + stop_settle_seconds * nanoseconds_per_second;
    static_cast<void>(wait_for_exit_or_settle(settle_end, exit_path, target->pid));

    return finalize_stop(exit_path, target->pid, target->resource, sent_signal, last_signal);
}

core::Status Runner::status(std::string_view instance_dir, std::string& state) noexcept {
    state = "unknown";
    if (instance_dir.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const std::string directory(instance_dir);
    ResourceMetadata resource{};
    if (!read_json_into(instance_file_path(directory, "resource.json"), resource)) {
        return {};
    }
    if (resource.pid <= 0) {
        return {};
    }

    // Identity gate (spec process-supervision "Status reports lifecycle
    // state"; analysis #5): a live pid whose /proc start-ticks differ from
    // resource.json is a recycled PID of an unrelated process — not this
    // instance running. Legacy anchors (0) keep the plain pid check.
    if (process_alive(resource.pid) && identity_matches(resource.pid, resource.proc_start_ticks)) {
        state = "running";
        return {};
    }

    ExitMetadata exit_metadata{};
    if (!read_json_into(instance_file_path(directory, "exit.json"), exit_metadata)) {
        // The process is gone and no completion record exists; a supervisor
        // may still be about to publish one, but the state is already final.
        state = "exited";
        return {};
    }
    state = exit_metadata.terminated_by == TerminatedBy::Runner ? "stopped" : "exited";
    return {};
}

} // namespace nexigen::runner
