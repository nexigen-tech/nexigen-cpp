// nexigen::exec — suite process orchestration and supervisor loop
// (change refactor-app-suite-layers; spec execution_manager/app-suite).
//
// COLD PATH ONLY. The suite supervisor launches one per-instance supervisor
// per application, polls instance files, and applies restart/impact policy.
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>
#include <nexigen/exec/app_state.hpp>
#include <nexigen/exec/child_process.hpp>
#include <nexigen/exec/detail/path_utils.hpp>
#include <nexigen/exec/detail/suite_launch_plan.hpp>
#include <nexigen/exec/detail/suite_reporting.hpp>
#include <nexigen/exec/detail/suite_supervisor.hpp>
#include <nexigen/exec/detail/sys_helpers.hpp>
#include <nexigen/exec/exit_metadata.hpp>
#include <nexigen/exec/impact_on_app_suite_operability.hpp>
#include <nexigen/exec/instance_file_io.hpp>
#include <nexigen/exec/instance_layout.hpp>
#include <nexigen/exec/execution_manager_logging.hpp>
#include <nexigen/exec/suite_app_status.hpp>
#include <nexigen/exec/suite_exit.hpp>
#include <nexigen/exec/suite_manifest.hpp>
#include <nexigen/exec/suite_state.hpp>
#include <nexigen/exec/suite_status.hpp>

#include "coverage_flush.hpp"

namespace nexigen::exec::detail {

namespace {

/// Runtime state plus lifecycle actions for one child application. Replaces
/// the former local `RuntimeApp` struct (extract-child-process-class).
using RuntimeApp = ChildProcess;

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

[[nodiscard]] SuiteStatus build_suite_status(const RuntimeSuite& suite) {
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
    return status;
}

void write_suite_status(const RuntimeSuite& suite) {
    write_suite_status_file(build_suite_status(suite), suite.state_path);
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
    write_suite_exit_file(exit, suite.exit_path);
    write_suite_status(suite);
}

void stop_all(RuntimeSuite& suite) {
    for (RuntimeApp& app : suite.apps) {
        if (app.state == AppState::Running || app.state == AppState::Restarting ||
            app.child_pid > 0 || app.supervisor_pid > 0) {
            static_cast<void>(app.stop(app.spec.stop_grace_s));
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
    const SuiteLaunchPlan plan =
        resolve_suite_launch(*suite.manifest, app.spec, app.attempt, app.instance_dir);
    app.artifact_dir = plan.artifact_dir;

    const core::Status started = app.launch(plan.identity, plan.command, plan.options);
    return started.has_value();
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

/// A dependency is ready when its application is alive (state Running).
[[nodiscard]] const RuntimeApp* find_app_by_name(const RuntimeSuite& suite,
                                                std::string_view name) noexcept {
    for (const RuntimeApp& candidate : suite.apps) {
        if (candidate.spec.app == name) {
            return &candidate;
        }
    }
    return nullptr;
}

/// All declared dependencies are Running; an unknown dependency is never
/// ready (the dependent fails visibly instead of launching blind).
[[nodiscard]] bool dependencies_ready(const RuntimeSuite& suite, const RuntimeApp& app) noexcept {
    for (const std::string& dependency : app.spec.dependencies) {
        const RuntimeApp* dep = find_app_by_name(suite, dependency);
        if (dep == nullptr || dep->state != AppState::Running) {
            return false;
        }
    }
    return true;
}

/// A dependency that failed or exited can never report Running.
[[nodiscard]] bool dependency_failed(const RuntimeSuite& suite, const RuntimeApp& app) noexcept {
    for (const std::string& dependency : app.spec.dependencies) {
        const RuntimeApp* dep = find_app_by_name(suite, dependency);
        if (dep != nullptr &&
            (dep->state == AppState::Failed || dep->state == AppState::Exited)) {
            return true;
        }
    }
    return false;
}

/// The longest configured startup timeout among the dependencies (0 = unset).
[[nodiscard]] std::uint64_t max_dependency_startup_ns(const RuntimeSuite& suite,
                                                      const RuntimeApp& app) noexcept {
    std::uint64_t max_s = 0;
    for (const std::string& dependency : app.spec.dependencies) {
        const RuntimeApp* dep = find_app_by_name(suite, dependency);
        if (dep != nullptr && dep->spec.idle_timeout_s > max_s) {
            max_s = dep->spec.idle_timeout_s;
        }
    }
    return max_s * nanoseconds_per_second;
}

void launch_all(RuntimeSuite& suite) {
    const std::uint64_t now = monotonic_now_ns();
    for (RuntimeApp& app : suite.apps) {
        if (!app.spec.dependencies.empty() && !dependencies_ready(suite, app)) {
            // Dependency gating (REQ_EM_001): wait until every dependency
            // reports Running.
            app.dependencies_pending = true;
            const std::uint64_t deadline = max_dependency_startup_ns(suite, app);
            app.dependency_deadline_ns =
                deadline == 0U ? 0U : now + deadline;
            log_execution_manager(core::LogLevel::Info,
                       std::format("event=dependency_wait app={} instance={}",
                                   app.spec.app, app.spec.instance));
            suite.dirty = true;
            continue;
        }
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
    log_execution_manager(core::LogLevel::Info,
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
                   std::format("event=suite_restart suite={} used={} limit={}", manifest.suite_id,
                               suite.suite_restarts_used, manifest.suite_restart_attempts));
        for (const RuntimeApp& app : suite.apps) {
            log_execution_manager(core::LogLevel::Info,
                       std::format("event=app_launch app={} instance={} "
                                   "attempt={} pid={} state={}",
                                   app.spec.app, app.spec.instance, app.attempt, app.child_pid,
                                   nexigen::exec::to_string(app.state)));
        }
        write_suite_status(suite);
    }
    return true;
}

/// Explicit per-application state machine. Returns true when an abnormal exit
/// scheduled a suite-wide restart, so the caller stops scanning this tick.
class AppStateMachine {
public:
    AppStateMachine(RuntimeSuite& suite, RuntimeApp& app) noexcept : suite_(suite), app_(app) {}

    [[nodiscard]] bool advance(std::uint64_t now) {
        switch (app_.state) {
            case AppState::Running:    return on_running();
            case AppState::Restarting: return on_restart_timer(now);
            default:                   return false;
        }
    }

private:
    void log_exit(const ExitMetadata& exit) const {
        if (exit.exit_code.has_value()) {
            log_execution_manager(core::LogLevel::Info, std::format("event=app_exit app={} instance={} "
                                                            "attempt={} exit_code={}",
                                                            app_.spec.app, app_.spec.instance,
                                                            app_.attempt, *exit.exit_code));
        } else if (exit.signal.has_value()) {
            log_execution_manager(core::LogLevel::Warning,
                       std::format("event=app_exit app={} instance={} "
                                   "attempt={} signal={}",
                                   app_.spec.app, app_.spec.instance, app_.attempt, *exit.signal));
        }
    }

    [[nodiscard]] bool on_running() {
        if (app_.exit_consumed) {
            return false;
        }
        ExitMetadata exit{};
        if (!read_exit_metadata(app_.instance_dir, exit)) {
            return false;
        }
        app_.exit_consumed = true;
        return on_exit_consumed(exit);
    }

    [[nodiscard]] bool on_exit_consumed(const ExitMetadata& exit) {
        log_exit(exit);
        if (exit.exit_code.has_value() && *exit.exit_code == 0) {
            app_.state = AppState::Exited;
            suite_.dirty = true;
            return false;
        }
        handle_abnormal(suite_, app_);
        return suite_.suite_restart_pending;
    }

    [[nodiscard]] bool on_restart_timer(std::uint64_t now) {
        if (app_.next_restart_ns == 0 || now < app_.next_restart_ns) {
            return false;
        }
        app_.launch_failed = false;
        suspend_execution_manager_logging();
        const bool launched = launch_app(suite_, app_);
        static_cast<void>(resume_execution_manager_logging());
        if (!launched) {
            on_launch_failed();
        } else {
            log_execution_manager(core::LogLevel::Info, std::format("event=app_launch app={} instance={} "
                                                            "attempt={} pid={} state=running",
                                                            app_.spec.app, app_.spec.instance,
                                                            app_.attempt, app_.child_pid));
        }
        suite_.dirty = true;
        return false;
    }

    void on_launch_failed() {
        app_.launch_failed = false;
        log_execution_manager(core::LogLevel::Warning,
                   std::format("event=app_launch_failed app={} "
                               "instance={} attempt={}",
                               app_.spec.app, app_.spec.instance, app_.attempt));
        schedule_retry_or_fail(suite_, app_);
    }

    RuntimeSuite& suite_;
    RuntimeApp& app_;
};

void advance_suite_apps(RuntimeSuite& suite, std::uint64_t now) {
    for (RuntimeApp& app : suite.apps) {
        if (app.dependencies_pending) {
            // Dependency gating (REQ_EM_001): launch once ready, fail visibly
            // on a failed dependency or an expired deadline.
            if (dependencies_ready(suite, app)) {
                app.dependencies_pending = false;
                app.dependency_deadline_ns = 0;
                const bool ok = launch_app(suite, app);
                if (!ok) {
                    app.launch_failed = true;
                    apply_launch_failures(suite);
                    return;
                }
                suite.dirty = true;
                continue;
            }
            if (dependency_failed(suite, app) ||
                (app.dependency_deadline_ns != 0U && now >= app.dependency_deadline_ns)) {
                app.dependencies_pending = false;
                app.dependency_deadline_ns = 0;
                app.state = AppState::Failed;
                if (app.spec.impact == ImpactOnAppSuiteOperability::Degrade) {
                    suite.state = SuiteState::Degraded;
                }
                log_execution_manager(core::LogLevel::Warning,
                           std::format("event=dependency_failed app={} instance={}",
                                       app.spec.app, app.spec.instance));
                suite.dirty = true;
                continue;
            }
            continue;  // still waiting
        }
        AppStateMachine machine(suite, app);
        if (machine.advance(now)) {
            break;
        }
    }
}

void apply_suite_control_thread(const SuiteManifest& manifest) noexcept {
    core::ThreadApplyReport report{};
    const core::Status status =
        apply_execution_manager_thread_spec(manifest.execution_manager_control_thread, report, "suite-supervisor");
    if (!status) {
        const std::string detail = describe_thread_apply_report(report);
        std::fprintf(stderr, "nexigen-exec: control thread spec application failed: %s\n",
                     detail.c_str());
        if (manifest.execution_manager_control_thread.apply_mode() == core::ThreadApplyMode::Required) {
            child_exit(1); // no application has been launched at this point
        }
    }
}

void initialize_runtime_apps(RuntimeSuite& suite, const SuiteManifest& manifest) {
    suite.apps.reserve(manifest.apps.size());
    for (const AppSpec& app : manifest.apps) {
        RuntimeApp runtime(app);
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
        log_settings.global_loaded ? log_settings.options.initial_log_level
                                   : core::LogLevel::Info);
    const core::Status log_status =
        init_execution_manager_logging(join_path(manifest.artifacts_path, manifest.suite_id), log_settings,
                            log_level, manifest.execution_manager_logger_thread, manifest.execution_manager_name);
    if (!log_status) {
        warn_execution_manager("event=logging_unavailable detail=execution_manager.jsonl not writable");
    }
}

void log_initial_app_launches(const RuntimeSuite& suite) {
    log_execution_manager(core::LogLevel::Info, std::format("event=suite_started suite={} apps={}",
                                                    suite.manifest->suite_id, suite.apps.size()));
    for (const RuntimeApp& app : suite.apps) {
        log_execution_manager(app.state == AppState::Failed ? core::LogLevel::Warning
                                                 : core::LogLevel::Info,
                   std::format("event=app_launch app={} instance={} attempt={} "
                               "pid={} state={}",
                               app.spec.app, app.spec.instance, app.attempt, app.child_pid,
                               nexigen::exec::to_string(app.state)));
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

} // namespace

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
    if (!write_suite_metadata_file(metadata, suite_dir)) {
        StartReport report{0, static_cast<std::int32_t>(core::ErrorCode::SystemError)};
        static_cast<void>(write_start_report(report_fd, report));
        static_cast<void>(::close(report_fd));
        child_exit(1);
    }

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

} // namespace nexigen::exec::detail
