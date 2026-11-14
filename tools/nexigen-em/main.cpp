// nexigen-em — Execution Manager host (change
// introduce-platformfg-process-model; spec exec/process-types).
//
// Hosts the EM-side StateClient receiver plus the per-application readiness
// event listeners. The listeners feed an ExecutionStateTracker, so a
// Function Group switch to Run is confirmed only after every bound daemon has
// reported readiness and every bound oneshot has exited 0.
//
// Built only when NEXIGEN_ENABLE_COM=ON.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/execution_state_tracker.hpp>
#include <nexigen/exec/function_group_registry.hpp>
#include <nexigen/exec/state_client.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace app = nexigen::app;
namespace com = nexigen::com;
namespace core = nexigen::core;
namespace exec = nexigen::exec;

namespace {

constexpr int exit_usage = 2;
constexpr int exit_failure = 1;
constexpr std::string_view default_service = "Nexigen/EM/StateCommand";
constexpr auto poll_interval = std::chrono::milliseconds(5);

std::atomic<bool> g_stop{false};

void request_stop(int) {
    g_stop.store(true, std::memory_order_release);
}

void print_usage(std::FILE* out) {
    std::fprintf(out, R"(Usage:
  nexigen-em --config <suite-manifest> [--service <name>]
              [--run-id <id>] [--instance <id>] [--run-dir <path>]
  nexigen-em --help

The EM host serves SetState commands on <name> and <name>Emergency, feeds the
readiness tracker from Nexigen/EM/State/<app>, and runs until SIGINT/SIGTERM.
)");
}

struct Options {
    std::string config{};
    std::string service{default_service};
    std::string run_id{};
    std::string instance_id{"1"};
    std::string run_dir{};
};

[[nodiscard]] bool parse_args(int argc, char* argv[], Options& options, int& exit_code) {
    exit_code = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            print_usage(stdout);
            return false;
        }
        if (argument == "--config" || argument == "--service" || argument == "--run-id" ||
            argument == "--instance" || argument == "--run-dir") {
            if (index + 1 >= argc) {
                print_usage(stderr);
                exit_code = exit_usage;
                return false;
            }
            const std::string value{argv[++index]};
            if (argument == "--config") {
                options.config = value;
            } else if (argument == "--service") {
                options.service = value;
            } else if (argument == "--run-id") {
                options.run_id = value;
            } else if (argument == "--instance") {
                options.instance_id = value;
            } else {
                options.run_dir = value;
            }
            continue;
        }
        std::fprintf(stderr, "nexigen-em: unknown option '%.*s'\n",
                     static_cast<int>(argument.size()), argument.data());
        print_usage(stderr);
        exit_code = exit_usage;
        return false;
    }
    if (options.config.empty()) {
        std::fprintf(stderr, "nexigen-em: missing --config\n");
        print_usage(stderr);
        exit_code = exit_usage;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    Options options{};
    int exit_code = 0;
    if (!parse_args(argc, argv, options, exit_code)) {
        return exit_code;
    }

    exec::SuiteManifest manifest{};
    const core::Status loaded = exec::load_suite_manifest(options.config, manifest);
    if (!loaded) {
        std::fprintf(stderr, "nexigen-em: manifest failed: %s\n",
                     std::string(core::to_string(loaded.error())).c_str());
        return exit_failure;
    }
    if (manifest.function_groups.empty()) {
        std::fprintf(stderr, "nexigen-em: manifest declares no function groups\n");
        return exit_failure;
    }
    if (!options.run_id.empty()) {
        manifest.run_id = options.run_id;
    }
    if (!options.run_dir.empty()) {
        manifest.run_dir = options.run_dir;
    }

    auto node = com::Node::create();
    if (!node) {
        std::fprintf(stderr, "nexigen-em: com node failed: %s\n",
                     std::string(com::to_string(node.error())).c_str());
        return exit_failure;
    }
    const auto service = com::ServiceName::create(options.service.c_str());
    if (!service) {
        std::fprintf(stderr, "nexigen-em: invalid service name: %s\n",
                     std::string(com::to_string(service.error())).c_str());
        return exit_failure;
    }

    exec::FunctionGroupRegistry registry(manifest);
    exec::ExecutionManager manager;
    exec::ExecutionStateTracker tracker;
    const app::RunIdentity identity{manifest.run_id, manifest.run_dir, options.instance_id, {}};
    auto client = exec::StateClient::create(*node, *service, registry, manager, identity, &tracker);
    if (!client) {
        std::fprintf(stderr, "nexigen-em: StateClient creation failed: %s\n",
                     std::string(com::to_string(client.error())).c_str());
        return exit_failure;
    }

    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
    std::fprintf(stdout, "nexigen-em: serving %s for suite %s\n", options.service.c_str(),
                 manifest.suite_id.c_str());
    std::fflush(stdout);

    while (!g_stop.load(std::memory_order_acquire)) {
        const auto served = client->serve_once();
        if (!served) {
            std::fprintf(stderr, "nexigen-em: serve failed: %s\n",
                         std::string(com::to_string(served.error())).c_str());
            client->stop_all();
            return exit_failure;
        }
        std::this_thread::sleep_for(poll_interval);
    }
    client->stop_all();
    std::fprintf(stdout, "nexigen-em: stopped\n");
    return 0;
}
