// nexigen-exec — command-line interface for the instance lifecycle supervisor
// (change nexigen-exec; spec execution_manager/process-supervision, design D8).
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

#include <nexigen/app/telemetry_layout.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/app_suite.hpp>
#include <nexigen/exec/instance_lifecycle.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/execution_manager_logging.hpp>

#include "detail/cli.hpp"

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace logging = nexigen::logging;
namespace exec = nexigen::exec;
namespace tools = nexigen::tools;

namespace {

constexpr int exit_usage = 2;
constexpr int exit_failure = 1;

void print_usage(std::FILE* out) {
    std::fprintf(out, R"(Usage:
  nexigen-exec start [--run-id <id>] [--app <name>] [--instance <id>]
                    [--run-dir <path>] [--instance-dir <path>]
                    [--timeout <secs>] -- <command> [args...]
  nexigen-exec start  --config <suite-manifest>
  nexigen-exec stop   <instance-dir> [--grace <secs>]
  nexigen-exec stop   --config <suite-manifest>|--suite-dir <dir> [--grace <secs>]
  nexigen-exec status <instance-dir>
  nexigen-exec status --config <suite-manifest>|--suite-dir <dir> [--json]
  nexigen-exec --help

Environment variables NEXIGEN_RUN_ID, NEXIGEN_EXEC_DIR, NEXIGEN_APP_NAME,
NEXIGEN_INSTANCE_ID, and NEXIGEN_INSTANCE_DIR are start-time defaults.
)");
}

[[nodiscard]] std::string derived_instance_dir(const app::RunIdentity& identity,
                                               std::string_view app_name) {
    if (!identity.instance_dir.empty()) {
        return identity.instance_dir;
    }
    if (identity.run_dir.empty() || app_name.empty() || identity.instance_id.empty()) {
        return {};
    }
    return identity.run_dir + "/" + std::string(app_name) + "/" + identity.instance_id;
}

struct CliLogTarget {
    std::string primary_dir{};
    logging::Settings settings{};
    core::LogLevel level{core::LogLevel::Info};
    core::ThreadSpec control_thread{};
    core::ThreadSpec logger_thread{};
};

[[nodiscard]] std::string working_directory() {
    char buffer[4096]{};
    if (::getcwd(buffer, sizeof(buffer)) == nullptr) {
        return {};
    }
    return std::string(buffer);
}

[[nodiscard]] CliLogTarget fallback_log_target() {
    CliLogTarget target{};
    const std::string cwd = working_directory();
    target.primary_dir = (cwd.empty() ? std::string("/tmp") : cwd) + "/.nexigen-exec";
    return target;
}

[[nodiscard]] CliLogTarget manifest_log_target(const exec::SuiteManifest& manifest) {
    CliLogTarget target{};
    target.primary_dir = manifest.artifacts_path + "/" + manifest.suite_id;
    target.control_thread = manifest.execution_manager_control_thread;
    target.logger_thread = manifest.execution_manager_logger_thread;
    const core::Status loaded =
        exec::load_execution_manager_logging_settings(manifest.global_settings_path, target.settings);
    if (!loaded) {
        exec::warn_execution_manager("event=logging_settings_invalid detail=using defaults");
        target.settings = logging::Settings{};
    }
    target.level = manifest.log_level.value_or(target.settings.global_loaded
                                                   ? target.settings.options.initial_log_level
                                                   : core::LogLevel::Info);
    return target;
}

[[nodiscard]] bool suite_dir_log_target(std::string_view suite_dir, CliLogTarget& target) {
    const std::string directory(suite_dir);
    const core::Result<nlohmann::json> document = exec::read_json_file(directory + "/suite.json");
    if (!document) {
        return false;
    }
    exec::SuiteMetadata metadata{};
    exec::from_json(*document, metadata);
    target.primary_dir = metadata.artifacts_path + "/" + metadata.suite_id;
    target.control_thread = metadata.execution_manager_control_thread;
    target.logger_thread = metadata.execution_manager_logger_thread;
    const core::Status loaded =
        exec::load_execution_manager_logging_settings(metadata.global_settings_path, target.settings);
    if (!loaded) {
        exec::warn_execution_manager("event=logging_settings_invalid detail=using defaults");
        target.settings = logging::Settings{};
    }
    target.level = target.settings.global_loaded ? target.settings.options.initial_log_level
                                                 : core::LogLevel::Info;
    return true;
}

[[nodiscard]] core::Status activate_logging(const CliLogTarget& target) {
    const core::Status initialized = exec::init_execution_manager_logging(
        target.primary_dir, target.settings, target.level, target.logger_thread);
    if (!initialized) {
        exec::warn_execution_manager("event=logging_unavailable detail=execution_manager.jsonl not writable");
        if (target.logger_thread.apply_mode() == core::ThreadApplyMode::Required) {
            return initialized;
        }
    }
    return {};
}

[[nodiscard]] core::Status apply_control_affinity(const CliLogTarget& target) {
    core::ThreadApplyReport report{};
    const core::Status applied =
        exec::apply_execution_manager_thread_spec(target.control_thread, report, "cli-control");
    if (!applied) {
        exec::warn_execution_manager(std::format("event=thread_spec_warning thread=control detail={}",
                                        exec::describe_thread_apply_report(report)));
        if (target.control_thread.apply_mode() == core::ThreadApplyMode::Required) {
            return applied;
        }
    }
    return {};
}

void log_command(std::string_view command, std::string_view mode, std::string_view target,
                 std::string_view result, std::string_view detail = {}) {
    if (detail.empty()) {
        exec::log_execution_manager(result == "ok" ? core::LogLevel::Info : core::LogLevel::Error,
                           std::format("event=command command={} mode={} target={} result={}",
                                       command, mode, target, result));
    } else {
        exec::log_execution_manager(result == "ok" ? core::LogLevel::Info : core::LogLevel::Error,
                           std::format("event=command command={} mode={} target={} result={} "
                                       "detail={}",
                                       command, mode, target, result, detail));
    }
}

void finish_logging() {
    static_cast<void>(exec::flush_execution_manager_logging());
    exec::shutdown_execution_manager_logging();
}

// Shared CLI report tails (dedup-sink-plumbing): the
// log_command → finish_logging → fprintf sequence every cmd_* result
// path repeated verbatim. Byte identity with the pre-helper output is
// enforced by the execution_manager suites (process-supervision CLI assertions).

/// Failure tail with a core::Status diagnostic and the stderr line's
/// user-facing phrase (e.g. "start failed"). Returns exit_failure.
[[nodiscard]] int cli_fail(const char* command, const char* mode, const std::string& target,
                           std::string_view diagnostic, const char* stderr_message) {
    log_command(command, mode, target, "error", diagnostic);
    finish_logging();
    std::fprintf(stderr, "nexigen-exec: %s: %.*s\n", stderr_message,
                 static_cast<int>(diagnostic.size()), diagnostic.data());
    return exit_failure;
}

/// Success tail for the target-line verbs ("started", "stopped").
/// Returns 0.
[[nodiscard]] int cli_ok(const char* command, const char* mode, const std::string& target,
                         const char* verb) {
    log_command(command, mode, target, "ok");
    finish_logging();
    std::fprintf(stdout, "%s: %s\n", verb, target.c_str());
    return 0;
}

/// The --config / --suite-dir mutual-exclusivity + suite-vs-instance
/// validation shared by cmd_stop and cmd_status. Returns 0 when the
/// combination is fine; exit_usage after the matching stderr text
/// otherwise.
[[nodiscard]] int suite_target_options_conflict(const std::string& config,
                                                const std::string& suite_dir, bool have_dir) {
    if (!config.empty() && !suite_dir.empty()) {
        std::fprintf(stderr, "nexigen-exec: --config and --suite-dir are mutually exclusive\n");
        print_usage(stderr);
        return exit_usage;
    }
    if ((!config.empty() || !suite_dir.empty()) && have_dir) {
        std::fprintf(stderr,
                     "nexigen-exec: suite options cannot be combined with an instance directory\n");
        print_usage(stderr);
        return exit_usage;
    }
    return 0;
}

struct Start_args {
    std::string run_id{};
    std::string app_name{};
    std::string instance_id{};
    std::string run_dir{};
    std::string instance_dir{};
    std::string config{};
    std::uint64_t timeout_s = 0;
    std::vector<std::string> command{};
};

struct Stop_args {
    std::string instance_dir{};
    std::string config{};
    std::string suite_dir{};
    std::uint64_t grace_s = 5;
    bool grace_set = false;
    bool have_dir = false;
};

struct Status_args {
    std::string instance_dir{};
    std::string config{};
    std::string suite_dir{};
    bool have_dir = false;
    bool json_output = false;
};

enum class Start_scan { Scanned, Missing_value, Unknown };

/// Consume one `--name value` start option. `--timeout` is numeric; every
/// other option writes a string field. Leaves `index` on the value.
[[nodiscard]] Start_scan scan_start_value(Start_args& args, std::string_view argument, int argc,
                                          char* argv[], int& index) {
    using String_field = std::string Start_args::*;
    static constexpr std::pair<std::string_view, String_field> string_options[] = {
        {"--run-id", &Start_args::run_id},
        {"--app", &Start_args::app_name},
        {"--instance", &Start_args::instance_id},
        {"--run-dir", &Start_args::run_dir},
        {"--instance-dir", &Start_args::instance_dir},
        {"--config", &Start_args::config},
    };
    for (const auto& [name, field] : string_options) {
        if (argument == name) {
            if (index + 1 >= argc) {
                return Start_scan::Missing_value;
            }
            args.*field = argv[++index];
            return Start_scan::Scanned;
        }
    }
    if (argument == "--timeout") {
        if (index + 1 >= argc || !tools::parse_u64(argv[index + 1], args.timeout_s)) {
            return Start_scan::Missing_value;
        }
        ++index;
        return Start_scan::Scanned;
    }
    return Start_scan::Unknown;
}

/// Final start-mode validation (the shared command/--config tail).
[[nodiscard]] int validate_start_args(const Start_args& args) {
    if (args.command.empty() && args.config.empty()) {
        std::fprintf(stderr, "nexigen-exec: missing command or --config\n");
        print_usage(stderr);
        return exit_usage;
    }
    if (!args.config.empty() && !args.command.empty()) {
        std::fprintf(stderr, "nexigen-exec: --config cannot be combined with a command\n");
        print_usage(stderr);
        return exit_usage;
    }
    return 0;
}

/// Parse `start` arguments. nullopt = parsed; a value is the process exit code.
[[nodiscard]] std::optional<int> parse_start_args(int argc, char* argv[], Start_args& args) {
    int index = 2;
    bool command_mode = false;
    while (index < argc) {
        const std::string_view argument{argv[index]};
        if (argument == "--") {
            ++index;
            command_mode = true;
            break;
        }
        if (!argument.starts_with("--")) {
            command_mode = true;
            break;
        }
        if (argument == "--help") {
            print_usage(stdout);
            return 0;
        }
        const Start_scan scanned = scan_start_value(args, argument, argc, argv, index);
        if (scanned == Start_scan::Missing_value) {
            print_usage(stderr);
            return exit_usage;
        }
        if (scanned == Start_scan::Unknown) {
            std::fprintf(stderr, "nexigen-exec: unknown option '%s'\n",
                         std::string(argument).c_str());
            print_usage(stderr);
            return exit_usage;
        }
        ++index;
    }

    if (command_mode) {
        while (index < argc) {
            args.command.emplace_back(argv[index]);
            ++index;
        }
    }
    if (const int code = validate_start_args(args); code != 0) {
        return code;
    }
    return std::nullopt;
}

/// Parse `stop` arguments. nullopt = parsed; a value is the process exit code.
[[nodiscard]] std::optional<int> parse_stop_args(int argc, char* argv[], Stop_args& args) {
    int index = 2;
    while (index < argc) {
        const std::string_view argument{argv[index]};
        if (argument == "--grace") {
            if (index + 1 >= argc) {
                print_usage(stderr);
                return exit_usage;
            }
            ++index;
            if (!tools::parse_u64(argv[index], args.grace_s)) {
                print_usage(stderr);
                return exit_usage;
            }
            args.grace_set = true;
        } else if (argument == "--config") {
            if (index + 1 >= argc) {
                print_usage(stderr);
                return exit_usage;
            }
            args.config = argv[++index];
        } else if (argument == "--suite-dir") {
            if (index + 1 >= argc) {
                print_usage(stderr);
                return exit_usage;
            }
            args.suite_dir = argv[++index];
        } else if (argument == "--help") {
            print_usage(stdout);
            return 0;
        } else if (argument.starts_with("-")) {
            std::fprintf(stderr, "nexigen-exec: unknown option '%s'\n",
                         std::string(argument).c_str());
            print_usage(stderr);
            return exit_usage;
        } else if (!args.have_dir) {
            args.instance_dir = argv[index];
            args.have_dir = true;
        } else {
            std::fprintf(stderr, "nexigen-exec: too many arguments\n");
            print_usage(stderr);
            return exit_usage;
        }
        ++index;
    }
    return std::nullopt;
}

/// Parse `status` arguments. nullopt = parsed; a value is the process exit code.
[[nodiscard]] std::optional<int> parse_status_args(int argc, char* argv[], Status_args& args) {
    for (int index = 2; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help") {
            print_usage(stdout);
            return 0;
        }
        if (argument == "--config") {
            if (index + 1 >= argc) {
                print_usage(stderr);
                return exit_usage;
            }
            args.config = argv[++index];
            continue;
        }
        if (argument == "--suite-dir") {
            if (index + 1 >= argc) {
                print_usage(stderr);
                return exit_usage;
            }
            args.suite_dir = argv[++index];
            continue;
        }
        if (argument == "--json") {
            args.json_output = true;
            continue;
        }
        if (argument.starts_with("-")) {
            std::fprintf(stderr, "nexigen-exec: unknown option '%s'\n",
                         std::string(argument).c_str());
            print_usage(stderr);
            return exit_usage;
        }
        if (args.have_dir) {
            std::fprintf(stderr, "nexigen-exec: too many arguments\n");
            print_usage(stderr);
            return exit_usage;
        }
        args.instance_dir = argv[index];
        args.have_dir = true;
    }
    return std::nullopt;
}

/// Suite-manifest start path.
[[nodiscard]] int start_suite_process(const std::string& config) {
    exec::SuiteManifest manifest{};
    const core::Status loaded = exec::load_suite_manifest(config, manifest);
    if (!loaded) {
        const CliLogTarget target = fallback_log_target();
        static_cast<void>(activate_logging(target));
        return cli_fail("start", "suite", config, std::string(core::to_string(loaded.error())),
                        "manifest failed");
    }
    const CliLogTarget log_target = manifest_log_target(manifest);
    if (!apply_control_affinity(log_target)) {
        return cli_fail("start", "suite", config, "required control attributes failed",
                        "affinity failed");
    }
    std::string target;
    exec::ExecutionManager suite_runner;
    const core::Status started = suite_runner.start_suite(manifest, target);
    if (!activate_logging(log_target)) {
        return cli_fail("start", "suite", target, "required logger attributes failed",
                        "logger start failed");
    }
    if (!started) {
        return cli_fail("start", "suite", target, std::string(core::to_string(started.error())),
                        "suite start failed");
    }
    return cli_ok("start", "suite", target, "started");
}

/// Single-instance start path.
[[nodiscard]] int start_instance(const Start_args& args) {
    app::RunIdentity identity = app::load_run_identity();
    if (!args.run_id.empty()) {
        identity.run_id = args.run_id;
    }
    if (!args.instance_id.empty()) {
        identity.instance_id = args.instance_id;
    }
    if (!args.run_dir.empty()) {
        identity.run_dir = args.run_dir;
    }
    if (!args.instance_dir.empty()) {
        identity.instance_dir = args.instance_dir;
    }

    const std::string target = derived_instance_dir(identity, args.app_name);
    exec::ExecutionManager execution_manager;
    const core::Status status = execution_manager.start(identity, args.app_name, args.command, args.timeout_s);
    const CliLogTarget log_target = fallback_log_target();
    static_cast<void>(activate_logging(log_target));
    if (!status) {
        return cli_fail("start", "single", target, std::string(core::to_string(status.error())),
                        "start failed");
    }
    return cli_ok("start", "single", target, "started");
}

/// Suite stop path (`--config` or `--suite-dir`).
[[nodiscard]] int stop_suite_process(const Stop_args& args) {
    std::string target = args.suite_dir;
    CliLogTarget log_target = fallback_log_target();
    if (!args.config.empty()) {
        exec::SuiteManifest manifest{};
        const core::Status loaded = exec::load_suite_manifest(args.config, manifest);
        if (!loaded) {
            static_cast<void>(activate_logging(log_target));
            return cli_fail("stop", "suite", args.config,
                            std::string(core::to_string(loaded.error())), "manifest failed");
        }
        target = exec::suite_dir_path(manifest.run_dir, manifest.suite_id);
        log_target = manifest_log_target(manifest);
    } else if (!suite_dir_log_target(target, log_target)) {
        static_cast<void>(activate_logging(log_target));
        log_command("stop", "suite", target, "error", "suite metadata unreadable");
        finish_logging();
        std::fprintf(stderr, "nexigen-exec: suite metadata unreadable: %s\n", target.c_str());
        return exit_failure;
    }
    if (target.empty()) {
        std::fprintf(stderr, "nexigen-exec: missing suite directory\n");
        print_usage(stderr);
        return exit_usage;
    }
    static_cast<void>(activate_logging(log_target));
    static_cast<void>(apply_control_affinity(log_target));
    const std::optional<std::uint64_t> override =
        args.grace_set ? std::optional<std::uint64_t>(args.grace_s) : std::nullopt;
    const core::Status status = exec::stop_suite(target, override);
    if (!status) {
        return cli_fail("stop", "suite", target, std::string(core::to_string(status.error())),
                        "suite stop failed");
    }
    return cli_ok("stop", "suite", target, "stopped");
}

/// Single-instance stop path.
[[nodiscard]] int stop_instance(const Stop_args& args) {
    const CliLogTarget log_target = fallback_log_target();
    static_cast<void>(activate_logging(log_target));
    exec::ExecutionManager execution_manager;
    const core::Status status = execution_manager.stop(args.instance_dir, args.grace_s);
    if (!status) {
        return cli_fail("stop", "single", args.instance_dir,
                        std::string(core::to_string(status.error())), "stop failed");
    }
    return cli_ok("stop", "single", args.instance_dir, "stopped");
}

/// Suite status path (`--config` or `--suite-dir`).
[[nodiscard]] int status_suite_process(const Status_args& args) {
    std::string target = args.suite_dir;
    CliLogTarget log_target = fallback_log_target();
    if (!args.config.empty()) {
        exec::SuiteManifest manifest{};
        const core::Status loaded = exec::load_suite_manifest(args.config, manifest);
        if (!loaded) {
            static_cast<void>(activate_logging(log_target));
            return cli_fail("status", "suite", args.config,
                            std::string(core::to_string(loaded.error())), "manifest failed");
        }
        target = exec::suite_dir_path(manifest.run_dir, manifest.suite_id);
        log_target = manifest_log_target(manifest);
    } else if (!suite_dir_log_target(target, log_target)) {
        exec::warn_execution_manager("event=suite_metadata_unreadable detail=using fallback log");
    }
    exec::SuiteStatus status{};
    const core::Status queried = exec::suite_status(target, status);
    static_cast<void>(activate_logging(log_target));
    static_cast<void>(apply_control_affinity(log_target));
    if (!queried) {
        return cli_fail("status", "suite", target, std::string(core::to_string(queried.error())),
                        "suite status failed");
    }
    log_command("status", "suite", target, "ok",
                std::string("state=") + std::string(exec::to_string(status.state)));
    if (args.json_output) {
        nlohmann::json document;
        exec::to_json(document, status);
        std::fprintf(stdout, "%s\n", document.dump(2).c_str());
    } else {
        std::fprintf(stdout, "suite: %s (restarts used: %u)\n",
                     std::string(exec::to_string(status.state)).c_str(),
                     status.suite_restarts_used);
        for (const exec::SuiteAppStatus& app : status.apps) {
            std::fprintf(stdout, "  %s/%s: %s attempt=%u artifact=%s\n", app.app.c_str(),
                         app.instance.c_str(), std::string(exec::to_string(app.state)).c_str(),
                         app.attempt, app.artifact_dir.c_str());
        }
    }
    finish_logging();
    return 0;
}

/// Single-instance status path.
[[nodiscard]] int status_instance(const Status_args& args) {
    const CliLogTarget log_target = fallback_log_target();
    static_cast<void>(activate_logging(log_target));
    exec::ExecutionManager execution_manager;
    std::string state;
    const core::Status status = execution_manager.status(args.instance_dir, state);
    if (!status) {
        return cli_fail("status", "single", args.instance_dir,
                        std::string(core::to_string(status.error())), "status failed");
    }
    log_command("status", "single", args.instance_dir, "ok", std::string("state=") + state);
    finish_logging();
    std::fprintf(stdout, "%s\n", state.c_str());
    return 0;
}

int cmd_start(int argc, char* argv[]) {
    Start_args args{};
    if (const std::optional<int> code = parse_start_args(argc, argv, args)) {
        return *code;
    }
    if (!args.config.empty()) {
        return start_suite_process(args.config);
    }
    return start_instance(args);
}

int cmd_stop(int argc, char* argv[]) {
    Stop_args args{};
    if (const std::optional<int> code = parse_stop_args(argc, argv, args)) {
        return *code;
    }

    const int stop_exclusivity =
        suite_target_options_conflict(args.config, args.suite_dir, args.have_dir);
    if (stop_exclusivity != 0) {
        return stop_exclusivity;
    }

    if (!args.config.empty() || !args.suite_dir.empty()) {
        return stop_suite_process(args);
    }

    if (!args.have_dir) {
        std::fprintf(stderr, "nexigen-exec: missing instance directory\n");
        print_usage(stderr);
        return exit_usage;
    }
    return stop_instance(args);
}

int cmd_status(int argc, char* argv[]) {
    Status_args args{};
    if (const std::optional<int> code = parse_status_args(argc, argv, args)) {
        return *code;
    }

    const int status_exclusivity =
        suite_target_options_conflict(args.config, args.suite_dir, args.have_dir);
    if (status_exclusivity != 0) {
        return status_exclusivity;
    }

    if (!args.config.empty() || !args.suite_dir.empty()) {
        return status_suite_process(args);
    }

    if (!args.have_dir) {
        std::fprintf(stderr, "nexigen-exec: missing instance directory\n");
        print_usage(stderr);
        return exit_usage;
    }
    return status_instance(args);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(stderr);
        return exit_usage;
    }
    const std::string_view command{argv[1]};
    if (command == "start") {
        return cmd_start(argc, argv);
    }
    if (command == "stop") {
        return cmd_stop(argc, argv);
    }
    if (command == "status") {
        return cmd_status(argc, argv);
    }
    if (command == "--help" || command == "-h") {
        print_usage(stdout);
        return 0;
    }
    std::fprintf(stderr, "nexigen-exec: unknown command '%s'\n", std::string(command).c_str());
    print_usage(stderr);
    return exit_usage;
}
