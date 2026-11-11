// nexigen::exec — detached suite lifecycle control
// (change refactor-app-suite-layers; spec execution_manager/app-suite).
//
// COLD PATH ONLY. Owns start/stop request handling, the fork handshake, thread
// preflight, and the fallback finalize path. The polling loop lives in
// suite_supervisor.cpp.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/exec/detail/path_utils.hpp>
#include <nexigen/exec/detail/suite_reporting.hpp>
#include <nexigen/exec/detail/suite_supervisor.hpp>
#include <nexigen/exec/detail/sys_helpers.hpp>
#include <nexigen/exec/instance_file_io.hpp>
#include <nexigen/exec/instance_layout.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/execution_manager_logging.hpp>
#include <nexigen/exec/suite_control.hpp>
#include <nexigen/exec/suite_manifest.hpp>
#include <nexigen/exec/suite_metadata.hpp>
#include <nexigen/exec/suite_paths.hpp>

#include "coverage_flush.hpp"

namespace nexigen::exec {

using detail::child_exit;
using detail::file_exists;
using detail::identity_matches;
using detail::join_path;
using detail::monotonic_now_ns;
using detail::nanoseconds_per_second;
using detail::process_alive;
using detail::read_start_report;
using detail::redirect_to_null;
using detail::sleep_ms;
using detail::suite_poll_interval_ms;

[[nodiscard]] core::Status preflight_suite_threads(const SuiteManifest& manifest) noexcept {
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
    detail::run_suite_supervisor(manifest, suite_dir, report_fd);
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
    detail::write_terminal_suite_state(metadata, state_path, exit_path);
    return {};
}

} // namespace nexigen::exec
