// nexigen-run — command-line interface for the instance lifecycle supervisor
// (change nexigen-run; spec runner/process-supervision, design D8).
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

#include <nexigen/app/telemetry_layout.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/runner/runner.hpp>
#include <nexigen/runner/runner_logging.hpp>

#include "detail/cli.hpp"

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace logging = nexigen::logging;
namespace runner = nexigen::runner;
namespace tools = nexigen::tools;

namespace {

constexpr int exit_usage = 2;
constexpr int exit_failure = 1;

void print_usage(std::FILE* out) {
    std::fprintf(out, R"(Usage:
  nexigen-run start [--run-id <id>] [--app <name>] [--instance <id>]
                    [--run-dir <path>] [--instance-dir <path>]
                    [--timeout <secs>] -- <command> [args...]
  nexigen-run start  --config <suite-manifest>
  nexigen-run stop   <instance-dir> [--grace <secs>]
  nexigen-run stop   --config <suite-manifest>|--suite-dir <dir> [--grace <secs>]
  nexigen-run status <instance-dir>
  nexigen-run status --config <suite-manifest>|--suite-dir <dir> [--json]
  nexigen-run --help

Environment variables NEXIGEN_RUN_ID, NEXIGEN_RUN_DIR, NEXIGEN_APP_NAME,
NEXIGEN_INSTANCE_ID, and NEXIGEN_INSTANCE_DIR are start-time defaults.
)");
}

[[nodiscard]] std::string derived_instance_dir(const app::RunIdentity& identity,
                                              std::string_view app_name) {
    if (!identity.instance_dir.empty()) {
        return identity.instance_dir;
    }
    if (identity.run_dir.empty() || app_name.empty() ||
        identity.instance_id.empty()) {
        return {};
    }
    return identity.run_dir + "/" + std::string(app_name) + "/" +
           identity.instance_id;
}

struct CliLogTarget {
    std::string primary_dir{};
    logging::Settings settings{};
    logging::LogLevel level{logging::LogLevel::Info};
    std::vector<std::uint32_t> control_cpus{};
    std::vector<std::uint32_t> logger_cpus{};
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
    target.primary_dir =
        (cwd.empty() ? std::string("/tmp") : cwd) + "/.nexigen-run";
    return target;
}

[[nodiscard]] CliLogTarget manifest_log_target(
    const runner::SuiteManifest& manifest) {
    CliLogTarget target{};
    target.primary_dir =
        manifest.artifacts_path + "/" + manifest.suite_id;
    target.control_cpus = manifest.runner_control_cpus;
    target.logger_cpus = manifest.runner_logger_cpus;
    const core::Status loaded = runner::load_runner_logging_settings(
        manifest.global_settings_path, target.settings);
    if (!loaded) {
        runner::warn_runner(
            "event=logging_settings_invalid detail=using defaults");
        target.settings = logging::Settings{};
    }
    target.level = manifest.log_level.value_or(
        target.settings.global_loaded
            ? target.settings.options.initial_log_level
            : logging::LogLevel::Info);
    return target;
}

[[nodiscard]] bool suite_dir_log_target(std::string_view suite_dir,
                                        CliLogTarget& target) {
    const std::string directory(suite_dir);
    const core::Result<nlohmann::json> document =
        runner::read_json_file(directory + "/suite.json");
    if (!document) {
        return false;
    }
    runner::SuiteMetadata metadata{};
    runner::from_json(*document, metadata);
    target.primary_dir = metadata.artifacts_path + "/" + metadata.suite_id;
    target.control_cpus = metadata.runner_control_cpus;
    target.logger_cpus = metadata.runner_logger_cpus;
    const core::Status loaded = runner::load_runner_logging_settings(
        metadata.global_settings_path, target.settings);
    if (!loaded) {
        runner::warn_runner(
            "event=logging_settings_invalid detail=using defaults");
        target.settings = logging::Settings{};
    }
    target.level = target.settings.global_loaded
                       ? target.settings.options.initial_log_level
                       : logging::LogLevel::Info;
    return true;
}

void activate_logging(const CliLogTarget& target) {
    const core::Status initialized = runner::init_runner_logging(
        target.primary_dir, target.settings, target.level,
        std::span<const std::uint32_t>(target.logger_cpus.data(),
                                       target.logger_cpus.size()));
    if (!initialized) {
        runner::warn_runner(
            "event=logging_unavailable detail=runner.jsonl not writable");
    }
}

void apply_control_affinity(const CliLogTarget& target) {
    const core::Status applied = runner::apply_runner_affinity(
        std::span<const std::uint32_t>(target.control_cpus.data(),
                                       target.control_cpus.size()),
        "cli-control");
    if (!applied) {
        runner::warn_runner(
            "event=affinity_warning thread=control detail=invalid or rejected");
    }
}

void log_command(std::string_view command, std::string_view mode,
                 std::string_view target, std::string_view result,
                 std::string_view detail = {}) {
    if (detail.empty()) {
        runner::log_runner(
            result == "ok" ? logging::LogLevel::Info : logging::LogLevel::Error,
            std::format("event=command command={} mode={} target={} result={}",
                        command, mode, target, result));
    } else {
        runner::log_runner(
            result == "ok" ? logging::LogLevel::Info : logging::LogLevel::Error,
            std::format("event=command command={} mode={} target={} result={} "
                        "detail={}",
                        command, mode, target, result, detail));
    }
}

void finish_logging() {
    static_cast<void>(runner::flush_runner_logging());
    runner::shutdown_runner_logging();
}

int cmd_start(int argc, char* argv[]) {
    std::string run_id;
    std::string app_name;
    std::string instance_id;
    std::string run_dir;
    std::string instance_dir;
    std::string config;
    std::uint64_t timeout_s = 0;
    std::vector<std::string> command;

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

        auto take_value = [&](std::string& target) -> bool {
            if (index + 1 >= argc) {
                return false;
            }
            ++index;
            target = argv[index];
            return true;
        };

        if (argument == "--run-id") {
            if (!take_value(run_id)) {
                print_usage(stderr);
                return exit_usage;
            }
        } else if (argument == "--app") {
            if (!take_value(app_name)) {
                print_usage(stderr);
                return exit_usage;
            }
        } else if (argument == "--instance") {
            if (!take_value(instance_id)) {
                print_usage(stderr);
                return exit_usage;
            }
        } else if (argument == "--run-dir") {
            if (!take_value(run_dir)) {
                print_usage(stderr);
                return exit_usage;
            }
        } else if (argument == "--instance-dir") {
            if (!take_value(instance_dir)) {
                print_usage(stderr);
                return exit_usage;
            }
        } else if (argument == "--config") {
            if (!take_value(config)) {
                print_usage(stderr);
                return exit_usage;
            }
        } else if (argument == "--timeout") {
            std::string value;
            if (!take_value(value) || !tools::parse_u64(value, timeout_s)) {
                print_usage(stderr);
                return exit_usage;
            }
        } else if (argument == "--help") {
            print_usage(stdout);
            return 0;
        } else {
            std::fprintf(stderr, "nexigen-run: unknown option '%s'\n",
                         std::string(argument).c_str());
            print_usage(stderr);
            return exit_usage;
        }
        ++index;
    }

    if (command_mode) {
        while (index < argc) {
            command.emplace_back(argv[index]);
            ++index;
        }
    }
    if (command.empty()) {
        if (config.empty()) {
            std::fprintf(stderr, "nexigen-run: missing command or --config\n");
            print_usage(stderr);
            return exit_usage;
        }
    }

    if (!config.empty()) {
        if (!command.empty()) {
            std::fprintf(stderr,
                         "nexigen-run: --config cannot be combined with a command\n");
            print_usage(stderr);
            return exit_usage;
        }
        runner::SuiteManifest manifest{};
        const core::Status loaded = runner::load_suite_manifest(config, manifest);
        if (!loaded) {
            const CliLogTarget target = fallback_log_target();
            activate_logging(target);
            log_command("start", "suite", config, "error",
                        std::string(core::to_string(loaded.error())));
            finish_logging();
            std::fprintf(stderr, "nexigen-run: manifest failed: %s\n",
                         std::string(core::to_string(loaded.error())).c_str());
            return exit_failure;
        }
        const CliLogTarget log_target = manifest_log_target(manifest);
        apply_control_affinity(log_target);
        std::string target;
        runner::Runner suite_runner;
        const core::Status started = suite_runner.start_suite(manifest, target);
        activate_logging(log_target);
        if (!started) {
            log_command("start", "suite", target, "error",
                        std::string(core::to_string(started.error())));
            finish_logging();
            std::fprintf(stderr, "nexigen-run: suite start failed: %s\n",
                         std::string(core::to_string(started.error())).c_str());
            return exit_failure;
        }
        log_command("start", "suite", target, "ok");
        finish_logging();
        std::fprintf(stdout, "started: %s\n", target.c_str());
        return 0;
    }

    app::RunIdentity identity = app::load_run_identity();
    if (!run_id.empty()) {
        identity.run_id = run_id;
    }
    if (!instance_id.empty()) {
        identity.instance_id = instance_id;
    }
    if (!run_dir.empty()) {
        identity.run_dir = run_dir;
    }
    if (!instance_dir.empty()) {
        identity.instance_dir = instance_dir;
    }

    const std::string target = derived_instance_dir(identity, app_name);
    runner::Runner runner;
    const core::Status status = runner.start(identity, app_name, command, timeout_s);
    const CliLogTarget log_target = fallback_log_target();
    activate_logging(log_target);
    if (!status) {
        log_command("start", "single", target, "error",
                    std::string(core::to_string(status.error())));
        finish_logging();
        std::fprintf(stderr, "nexigen-run: start failed: %s\n",
                     std::string(core::to_string(status.error())).c_str());
        return exit_failure;
    }
    log_command("start", "single", target, "ok");
    finish_logging();
    std::fprintf(stdout, "started: %s\n", target.c_str());
    return 0;
}

int cmd_stop(int argc, char* argv[]) {
    std::string instance_dir;
    std::string config;
    std::string suite_dir;
    std::uint64_t grace_s = 5;
    bool grace_set = false;
    bool have_dir = false;

    int index = 2;
    while (index < argc) {
        const std::string_view argument{argv[index]};
        if (argument == "--grace") {
            std::string value;
            if (index + 1 >= argc) {
                print_usage(stderr);
                return exit_usage;
            }
            ++index;
            value = argv[index];
            if (!tools::parse_u64(value, grace_s)) {
                print_usage(stderr);
                return exit_usage;
            }
            grace_set = true;
        } else if (argument == "--config") {
            if (index + 1 >= argc) {
                print_usage(stderr);
                return exit_usage;
            }
            config = argv[++index];
        } else if (argument == "--suite-dir") {
            if (index + 1 >= argc) {
                print_usage(stderr);
                return exit_usage;
            }
            suite_dir = argv[++index];
        } else if (argument == "--help") {
            print_usage(stdout);
            return 0;
        } else if (argument.starts_with("-")) {
            std::fprintf(stderr, "nexigen-run: unknown option '%s'\n",
                         std::string(argument).c_str());
            print_usage(stderr);
            return exit_usage;
        } else if (!have_dir) {
            instance_dir = argv[index];
            have_dir = true;
        } else {
            std::fprintf(stderr, "nexigen-run: too many arguments\n");
            print_usage(stderr);
            return exit_usage;
        }
        ++index;
    }

    if (!config.empty() && !suite_dir.empty()) {
        std::fprintf(stderr,
                     "nexigen-run: --config and --suite-dir are mutually exclusive\n");
        print_usage(stderr);
        return exit_usage;
    }
    if ((!config.empty() || !suite_dir.empty()) && have_dir) {
        std::fprintf(stderr,
                     "nexigen-run: suite options cannot be combined with an instance directory\n");
        print_usage(stderr);
        return exit_usage;
    }

    if (!config.empty() || !suite_dir.empty()) {
        std::string target = suite_dir;
        CliLogTarget log_target = fallback_log_target();
        if (!config.empty()) {
            runner::SuiteManifest manifest{};
            const core::Status loaded =
                runner::load_suite_manifest(config, manifest);
            if (!loaded) {
                activate_logging(log_target);
                log_command("stop", "suite", config, "error",
                            std::string(core::to_string(loaded.error())));
                finish_logging();
                std::fprintf(stderr, "nexigen-run: manifest failed: %s\n",
                             std::string(core::to_string(loaded.error())).c_str());
                return exit_failure;
            }
            target = runner::suite_dir_path(manifest.run_dir, manifest.suite_id);
            log_target = manifest_log_target(manifest);
        } else if (!suite_dir_log_target(target, log_target)) {
            activate_logging(log_target);
            log_command("stop", "suite", target, "error",
                        "suite metadata unreadable");
            finish_logging();
            std::fprintf(stderr,
                         "nexigen-run: suite metadata unreadable: %s\n",
                         target.c_str());
            return exit_failure;
        }
        if (target.empty()) {
            std::fprintf(stderr, "nexigen-run: missing suite directory\n");
            print_usage(stderr);
            return exit_usage;
        }
        activate_logging(log_target);
        apply_control_affinity(log_target);
        const std::optional<std::uint64_t> override =
            grace_set ? std::optional<std::uint64_t>(grace_s) : std::nullopt;
        const core::Status status = runner::stop_suite(target, override);
        if (!status) {
            log_command("stop", "suite", target, "error",
                        std::string(core::to_string(status.error())));
            finish_logging();
            std::fprintf(stderr, "nexigen-run: suite stop failed: %s\n",
                         std::string(core::to_string(status.error())).c_str());
            return exit_failure;
        }
        log_command("stop", "suite", target, "ok");
        finish_logging();
        std::fprintf(stdout, "stopped: %s\n", target.c_str());
        return 0;
    }

    if (!have_dir) {
        std::fprintf(stderr, "nexigen-run: missing instance directory\n");
        print_usage(stderr);
        return exit_usage;
    }

    const CliLogTarget log_target = fallback_log_target();
    activate_logging(log_target);
    runner::Runner runner;
    const core::Status status = runner.stop(instance_dir, grace_s);
    if (!status) {
        log_command("stop", "single", instance_dir, "error",
                    std::string(core::to_string(status.error())));
        finish_logging();
        std::fprintf(stderr, "nexigen-run: stop failed: %s\n",
                     std::string(core::to_string(status.error())).c_str());
        return exit_failure;
    }
    log_command("stop", "single", instance_dir, "ok");
    finish_logging();
    std::fprintf(stdout, "stopped: %s\n", instance_dir.c_str());
    return 0;
}

int cmd_status(int argc, char* argv[]) {
    std::string instance_dir;
    std::string config;
    std::string suite_dir;
    bool have_dir = false;
    bool json_output = false;

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
            config = argv[++index];
            continue;
        }
        if (argument == "--suite-dir") {
            if (index + 1 >= argc) {
                print_usage(stderr);
                return exit_usage;
            }
            suite_dir = argv[++index];
            continue;
        }
        if (argument == "--json") {
            json_output = true;
            continue;
        }
        if (argument.starts_with("-")) {
            std::fprintf(stderr, "nexigen-run: unknown option '%s'\n",
                         std::string(argument).c_str());
            print_usage(stderr);
            return exit_usage;
        }
        if (have_dir) {
            std::fprintf(stderr, "nexigen-run: too many arguments\n");
            print_usage(stderr);
            return exit_usage;
        }
        instance_dir = argv[index];
        have_dir = true;
    }

    if (!config.empty() && !suite_dir.empty()) {
        std::fprintf(stderr,
                     "nexigen-run: --config and --suite-dir are mutually exclusive\n");
        print_usage(stderr);
        return exit_usage;
    }
    if ((!config.empty() || !suite_dir.empty()) && have_dir) {
        std::fprintf(stderr,
                     "nexigen-run: suite options cannot be combined with an instance directory\n");
        print_usage(stderr);
        return exit_usage;
    }

    if (!config.empty() || !suite_dir.empty()) {
        std::string target = suite_dir;
        CliLogTarget log_target = fallback_log_target();
        if (!config.empty()) {
            runner::SuiteManifest manifest{};
            const core::Status loaded =
                runner::load_suite_manifest(config, manifest);
            if (!loaded) {
                activate_logging(log_target);
                log_command("status", "suite", config, "error",
                            std::string(core::to_string(loaded.error())));
                finish_logging();
                std::fprintf(stderr, "nexigen-run: manifest failed: %s\n",
                             std::string(core::to_string(loaded.error())).c_str());
                return exit_failure;
            }
            target = runner::suite_dir_path(manifest.run_dir, manifest.suite_id);
            log_target = manifest_log_target(manifest);
        } else if (!suite_dir_log_target(target, log_target)) {
            runner::warn_runner(
                "event=suite_metadata_unreadable detail=using fallback log");
        }
        runner::SuiteStatus status{};
        const core::Status queried = runner::suite_status(target, status);
        activate_logging(log_target);
        apply_control_affinity(log_target);
        if (!queried) {
            log_command("status", "suite", target, "error",
                        std::string(core::to_string(queried.error())));
            finish_logging();
            std::fprintf(stderr, "nexigen-run: suite status failed: %s\n",
                         std::string(core::to_string(queried.error())).c_str());
            return exit_failure;
        }
        log_command("status", "suite", target, "ok",
                    std::string("state=") +
                        std::string(runner::to_string(status.state)));
        if (json_output) {
            nlohmann::json document;
            runner::to_json(document, status);
            std::fprintf(stdout, "%s\n", document.dump(2).c_str());
        } else {
            std::fprintf(stdout, "suite: %s (restarts used: %u)\n",
                         std::string(runner::to_string(status.state)).c_str(),
                         status.suite_restarts_used);
            for (const runner::SuiteAppStatus& app : status.apps) {
                std::fprintf(stdout, "  %s/%s: %s attempt=%u artifact=%s\n",
                             app.app.c_str(), app.instance.c_str(),
                             std::string(runner::to_string(app.state)).c_str(),
                             app.attempt, app.artifact_dir.c_str());
            }
        }
        finish_logging();
        return 0;
    }

    if (!have_dir) {
        std::fprintf(stderr, "nexigen-run: missing instance directory\n");
        print_usage(stderr);
        return exit_usage;
    }

    const CliLogTarget log_target = fallback_log_target();
    activate_logging(log_target);
    runner::Runner runner;
    std::string state;
    const core::Status status = runner.status(instance_dir, state);
    if (!status) {
        log_command("status", "single", instance_dir, "error",
                    std::string(core::to_string(status.error())));
        finish_logging();
        std::fprintf(stderr, "nexigen-run: status failed: %s\n",
                     std::string(core::to_string(status.error())).c_str());
        return exit_failure;
    }
    log_command("status", "single", instance_dir, "ok",
                std::string("state=") + state);
    finish_logging();
    std::fprintf(stdout, "%s\n", state.c_str());
    return 0;
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
    std::fprintf(stderr, "nexigen-run: unknown command '%s'\n",
                 std::string(command).c_str());
    print_usage(stderr);
    return exit_usage;
}
