// nexigen-run — command-line interface for the instance lifecycle supervisor
// (change nexigen-run; spec runner/process-supervision, design D8).
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/app/telemetry_layout.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/runner/runner.hpp>

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace runner = nexigen::runner;

namespace {

constexpr int exit_usage = 2;
constexpr int exit_failure = 1;

void print_usage(std::FILE* out) {
    std::fprintf(out, R"(Usage:
  nexigen-run start [--run-id <id>] [--app <name>] [--instance <id>]
                    [--run-dir <path>] [--instance-dir <path>]
                    [--timeout <secs>] -- <command> [args...]
  nexigen-run stop  <instance-dir> [--grace <secs>]
  nexigen-run status <instance-dir>
  nexigen-run --help

Environment variables NEXIGEN_RUN_ID, NEXIGEN_RUN_DIR, NEXIGEN_APP_NAME,
NEXIGEN_INSTANCE_ID, and NEXIGEN_INSTANCE_DIR are start-time defaults.
)");
}

[[nodiscard]] bool parse_uint(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const std::uint64_t digit =
            static_cast<std::uint64_t>(character - '0');
        if (value > (UINT64_MAX - digit) / 10ULL) {
            return false;
        }
        value = value * 10ULL + digit;
    }
    out = value;
    return true;
}

[[nodiscard]] std::string derived_instance_dir(const app::Run_identity& identity) {
    if (!identity.instance_dir.empty()) {
        return identity.instance_dir;
    }
    if (identity.run_dir.empty() || identity.app_name.empty() ||
        identity.instance_id.empty()) {
        return {};
    }
    return identity.run_dir + "/" + identity.app_name + "/" +
           identity.instance_id;
}

int cmd_start(int argc, char* argv[]) {
    std::string run_id;
    std::string app_name;
    std::string instance_id;
    std::string run_dir;
    std::string instance_dir;
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
        } else if (argument == "--timeout") {
            std::string value;
            if (!take_value(value) || !parse_uint(value, timeout_s)) {
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
        std::fprintf(stderr, "nexigen-run: missing command\n");
        print_usage(stderr);
        return exit_usage;
    }

    app::Run_identity identity = app::load_run_identity();
    if (!run_id.empty()) {
        identity.run_id = run_id;
    }
    if (!app_name.empty()) {
        identity.app_name = app_name;
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

    const std::string target = derived_instance_dir(identity);
    runner::Runner runner;
    const core::Status status = runner.start(identity, command, timeout_s);
    if (!status) {
        std::fprintf(stderr, "nexigen-run: start failed: %s\n",
                     std::string(core::to_string(status.error())).c_str());
        return exit_failure;
    }
    std::fprintf(stdout, "started: %s\n", target.c_str());
    return 0;
}

int cmd_stop(int argc, char* argv[]) {
    std::string instance_dir;
    std::uint64_t grace_s = 5;
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
            if (!parse_uint(value, grace_s)) {
                print_usage(stderr);
                return exit_usage;
            }
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

    if (!have_dir) {
        std::fprintf(stderr, "nexigen-run: missing instance directory\n");
        print_usage(stderr);
        return exit_usage;
    }

    runner::Runner runner;
    const core::Status status = runner.stop(instance_dir, grace_s);
    if (!status) {
        std::fprintf(stderr, "nexigen-run: stop failed: %s\n",
                     std::string(core::to_string(status.error())).c_str());
        return exit_failure;
    }
    std::fprintf(stdout, "stopped: %s\n", instance_dir.c_str());
    return 0;
}

int cmd_status(int argc, char* argv[]) {
    std::string instance_dir;
    bool have_dir = false;

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help") {
            print_usage(stdout);
            return 0;
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

    if (!have_dir) {
        std::fprintf(stderr, "nexigen-run: missing instance directory\n");
        print_usage(stderr);
        return exit_usage;
    }

    runner::Runner runner;
    std::string state;
    const core::Status status = runner.status(instance_dir, state);
    if (!status) {
        std::fprintf(stderr, "nexigen-run: status failed: %s\n",
                     std::string(core::to_string(status.error())).c_str());
        return exit_failure;
    }
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
