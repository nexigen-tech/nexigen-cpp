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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/runner/runner.hpp>

namespace nexigen::runner {

namespace {

constexpr std::size_t max_instance_dir_bytes = 511;
constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;
constexpr std::uint64_t timeout_grace_seconds = 5;
constexpr std::uint64_t stop_settle_seconds = 5;
constexpr std::uint64_t poll_interval_ms = 20;

/// Handshake message written by the supervisor before it starts reaping.
struct Start_report {
    std::int32_t pid{0};
    std::int32_t error{0};
};

[[nodiscard]] std::uint64_t monotonic_now_ns() noexcept {
    const core::Result<nexigen::chrono::Timestamp> now =
        nexigen::chrono::Timestamp::now();
    return now ? now->nanoseconds() : 0;
}

void sleep_ms(std::uint64_t milliseconds) noexcept {
    ::timespec request{};
    request.tv_sec = static_cast<time_t>(milliseconds / 1000ULL);
    request.tv_nsec =
        static_cast<long>((milliseconds % 1000ULL) * 1'000'000ULL);
    while (::nanosleep(&request, &request) != 0 && errno == EINTR) {
    }
}

[[nodiscard]] bool write_all(int fd, const void* data,
                             std::size_t size) noexcept {
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

[[nodiscard]] bool file_exists(std::string_view path) noexcept {
    const std::string name(path);
    return ::access(name.c_str(), F_OK) == 0;
}

/// True when the PID exists and is not a zombie. `/proc/<pid>/stat` is used
/// after the kill probe so a reaped child is not mistaken for a live one.
[[nodiscard]] bool process_alive(std::int32_t pid) noexcept {
    if (pid <= 0) {
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM) {
        return false;
    }
    char path[64]{};
    static_cast<void>(
        std::snprintf(path, sizeof(path), "/proc/%d/stat", pid));
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

/// Wait for the child while enforcing the optional idle timeout. When the
/// deadline expires, run the same SIGTERM → grace → SIGKILL sequence as
/// `Runner::stop`.
struct Supervise_result {
    bool reaped{false};
    bool runner_stopped{false};
};

[[nodiscard]] Supervise_result supervise_child(
    pid_t child, std::uint64_t timeout_s, std::uint64_t start_mono_ns,
    int& status) noexcept {
    Supervise_result result;
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

[[nodiscard]] Terminated_by attribution(bool runner_stopped, bool reaped,
                                        int status) noexcept {
    if (runner_stopped) {
        return Terminated_by::Runner;
    }
    if (!reaped) {
        return Terminated_by::Unknown;
    }
    if (WIFEXITED(status)) {
        return Terminated_by::Self;
    }
    if (WIFSIGNALED(status)) {
        const int signal_number = WTERMSIG(status);
        if (signal_number == SIGTERM || signal_number == SIGKILL) {
            return Terminated_by::Runner;
        }
    }
    return Terminated_by::Unknown;
}

void rewrite_terminated_by(const std::string& exit_path,
                           Terminated_by value) noexcept {
    core::Result<nlohmann::json> document = read_json_file(exit_path);
    if (!document) {
        return;
    }
    Exit_metadata metadata{};
    from_json(*document, metadata);
    metadata.terminated_by = value;
    nlohmann::json updated;
    to_json(updated, metadata);
    static_cast<void>(write_json_file(exit_path, updated));
}

void write_fallback_exit(const std::string& exit_path,
                         const Resource_metadata& resource,
                         std::optional<std::int32_t> signal_number,
                         Terminated_by attribution_value) noexcept {
    const std::uint64_t now = monotonic_now_ns();
    std::uint64_t uptime = 0;
    if (now != 0 && resource.start_time_mono_ns != 0 &&
        now >= resource.start_time_mono_ns) {
        uptime = (now - resource.start_time_mono_ns) / nanoseconds_per_second;
    }
    Exit_metadata metadata;
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

} // namespace

core::Status Runner::start(const app::Run_identity& identity,
                           const std::vector<std::string>& command,
                           std::uint64_t timeout_s) {
    if (command.empty() || command.front().empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    app::Run_identity resolved = identity;
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

    const std::string instance_dir = resolved.instance_dir;

    // One live instance per directory: refuse to clobber an unfinished run.
    const std::string resource_path =
        instance_file_path(instance_dir, "resource.json");
    const std::string exit_path = instance_file_path(instance_dir, "exit.json");
    if (file_exists(resource_path) && !file_exists(exit_path)) {
        return std::unexpected(core::ErrorCode::Conflict);
    }

    const core::Status created = create_instance_dir(instance_dir);
    if (!created) {
        return created;
    }

    app::Telemetry_paths resolved_paths{};
    const core::Status paths_status = app::resolve_telemetry_paths(
        resolved, "", "", "", resolved_paths);
    if (!paths_status) {
        return paths_status;
    }
    const core::Status settings_status =
        generate_settings(instance_dir, resolved_paths);
    if (!settings_status) {
        return settings_status;
    }

    // The deterministic layout lists all four JSONL streams; make sure they
    // exist before the child starts (the sinks append to them).
    for (const char* stream : {"app.jsonl", "metrics.jsonl", "traces.jsonl",
                               "chrono.jsonl"}) {
        const std::string stream_file = instance_file_path(instance_dir, stream);
        const int stream_fd =
            ::open(stream_file.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
        if (stream_fd < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        static_cast<void>(::close(stream_fd));
    }

    // A new run supersedes any previous completion record.
    static_cast<void>(::unlink(exit_path.c_str()));

    const std::string stdout_path =
        instance_file_path(instance_dir, "stdout.log");
    const std::string stderr_path =
        instance_file_path(instance_dir, "stderr.log");
    const int stdout_fd = open_truncated_log(stdout_path);
    if (stdout_fd < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    const int stderr_fd = open_truncated_log(stderr_path);
    if (stderr_fd < 0) {
        static_cast<void>(::close(stdout_fd));
        return std::unexpected(core::ErrorCode::SystemError);
    }
    const int null_fd = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    if (null_fd < 0) {
        static_cast<void>(::close(stdout_fd));
        static_cast<void>(::close(stderr_fd));
        return std::unexpected(core::ErrorCode::SystemError);
    }

    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for (const std::string& argument : command) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    Resource_metadata metadata =
        Resource_metadata::from_identity(resolved, 0, 0);

    int pipe_fds[2]{-1, -1};
    if (::pipe2(pipe_fds, O_CLOEXEC) != 0) {
        static_cast<void>(::close(stdout_fd));
        static_cast<void>(::close(stderr_fd));
        static_cast<void>(::close(null_fd));
        return std::unexpected(core::ErrorCode::SystemError);
    }

    const pid_t supervisor = ::fork();
    if (supervisor < 0) {
        static_cast<void>(::close(pipe_fds[0]));
        static_cast<void>(::close(pipe_fds[1]));
        static_cast<void>(::close(stdout_fd));
        static_cast<void>(::close(stderr_fd));
        static_cast<void>(::close(null_fd));
        return std::unexpected(core::ErrorCode::SystemError);
    }

    if (supervisor == 0) {
        // --- Supervisor process ---
        static_cast<void>(::close(pipe_fds[0]));
        static_cast<void>(::setsid());
        redirect_to_null(null_fd);

        // The child signals completion of setsid()/dup2() through this pipe;
        // resource.json is written only afterwards, so `start` never returns
        // before the process group exists.
        int setup_pipe[2]{-1, -1};
        if (::pipe2(setup_pipe, O_CLOEXEC) != 0) {
            Start_report report{0,
                                static_cast<std::int32_t>(
                                    core::ErrorCode::SystemError)};
            static_cast<void>(write_all(pipe_fds[1], &report, sizeof(report)));
            static_cast<void>(::close(pipe_fds[1]));
            _exit(1);
        }

        const pid_t child = ::fork();
        if (child < 0) {
            Start_report report{0,
                                static_cast<std::int32_t>(
                                    core::ErrorCode::SystemError)};
            static_cast<void>(write_all(pipe_fds[1], &report, sizeof(report)));
            static_cast<void>(::close(pipe_fds[1]));
            static_cast<void>(::close(setup_pipe[0]));
            static_cast<void>(::close(setup_pipe[1]));
            _exit(1);
        }

        if (child == 0) {
            // --- Application child ---
            static_cast<void>(::close(pipe_fds[1]));
            static_cast<void>(::close(setup_pipe[0]));
            if (::setsid() < 0) {
                _exit(126);
            }
            static_cast<void>(::dup2(null_fd, STDIN_FILENO));
            static_cast<void>(::dup2(stdout_fd, STDOUT_FILENO));
            static_cast<void>(::dup2(stderr_fd, STDERR_FILENO));
            if (null_fd > STDERR_FILENO) {
                static_cast<void>(::close(null_fd));
            }
            if (stdout_fd > STDERR_FILENO) {
                static_cast<void>(::close(stdout_fd));
            }
            if (stderr_fd > STDERR_FILENO) {
                static_cast<void>(::close(stderr_fd));
            }

            const char ready = 1;
            const ssize_t ready_written =
                ::write(setup_pipe[1], &ready, sizeof(ready));
            static_cast<void>(ready_written);
            static_cast<void>(::close(setup_pipe[1]));

            static_cast<void>(
                ::setenv("NEXIGEN_RUN_ID", resolved.run_id.c_str(), 1));
            static_cast<void>(
                ::setenv("NEXIGEN_RUN_DIR", resolved.run_dir.c_str(), 1));
            static_cast<void>(
                ::setenv("NEXIGEN_APP_NAME", resolved.app_name.c_str(), 1));
            static_cast<void>(
                ::setenv("NEXIGEN_INSTANCE_ID", resolved.instance_id.c_str(), 1));
            static_cast<void>(::setenv("NEXIGEN_INSTANCE_DIR",
                                       instance_dir.c_str(), 1));

            ::execvp(argv[0], argv.data());
            _exit(127);
        }

        // --- Supervisor parent of the application child ---
        static_cast<void>(::close(setup_pipe[1]));
        char ready = 0;
        const ssize_t ready_read =
            ::read(setup_pipe[0], &ready, sizeof(ready));
        static_cast<void>(ready_read);
        static_cast<void>(::close(setup_pipe[0]));

        if (null_fd > STDERR_FILENO) {
            static_cast<void>(::close(null_fd));
        }
        if (stdout_fd > STDERR_FILENO) {
            static_cast<void>(::close(stdout_fd));
        }
        if (stderr_fd > STDERR_FILENO) {
            static_cast<void>(::close(stderr_fd));
        }

        metadata.pid = child;
        metadata.ppid = ::getpid();
        const core::Status resource_status =
            write_resource_json(instance_dir, metadata);
        if (!resource_status) {
            static_cast<void>(::kill(-child, SIGKILL));
            reap_child(child);
            Start_report report{0,
                                static_cast<std::int32_t>(
                                    core::ErrorCode::SystemError)};
            static_cast<void>(write_all(pipe_fds[1], &report, sizeof(report)));
            static_cast<void>(::close(pipe_fds[1]));
            _exit(1);
        }

        Start_report report{metadata.pid, 0};
        static_cast<void>(write_all(pipe_fds[1], &report, sizeof(report)));
        static_cast<void>(::close(pipe_fds[1]));

        int child_status = 0;
        const Supervise_result supervised =
            supervise_child(child, timeout_s, metadata.start_time_mono_ns,
                            child_status);
        const std::uint64_t end_ns = monotonic_now_ns();
        std::uint64_t uptime = 0;
        if (supervised.reaped && end_ns != 0 &&
            metadata.start_time_mono_ns != 0 &&
            end_ns >= metadata.start_time_mono_ns) {
            uptime = (end_ns - metadata.start_time_mono_ns) /
                     nanoseconds_per_second;
        }

        const Terminated_by terminated_by = attribution(
            supervised.runner_stopped, supervised.reaped, child_status);
        const Exit_metadata exit_metadata = Exit_metadata::from_child_status(
            child, child_status, uptime, terminated_by);
        static_cast<void>(write_exit_json(instance_dir, exit_metadata));
        _exit(0);
    }

    // --- Start caller ---
    static_cast<void>(::close(pipe_fds[1]));
    static_cast<void>(::close(stdout_fd));
    static_cast<void>(::close(stderr_fd));
    static_cast<void>(::close(null_fd));

    Start_report report{};
    const bool received =
        read_all(pipe_fds[0], &report, sizeof(report));
    static_cast<void>(::close(pipe_fds[0]));

    if (!received || report.error != 0 || report.pid <= 0) {
        int supervisor_status = 0;
        static_cast<void>(
            ::waitpid(supervisor, &supervisor_status, WNOHANG));
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
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
        Resource_metadata completed_metadata{};
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
    Resource_metadata resource{};
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
                                Terminated_by::Unknown);
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
            rewrite_terminated_by(exit_path, Terminated_by::Runner);
        }
        if (group_alive(pid)) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    write_fallback_exit(exit_path, resource, last_signal,
                        sent_signal ? Terminated_by::Runner
                                    : Terminated_by::Unknown);
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
    Resource_metadata resource{};
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
    Exit_metadata exit_metadata{};
    from_json(*exit_document, exit_metadata);
    state = exit_metadata.terminated_by == Terminated_by::Runner ? "stopped"
                                                                 : "exited";
    return {};
}

} // namespace nexigen::runner
