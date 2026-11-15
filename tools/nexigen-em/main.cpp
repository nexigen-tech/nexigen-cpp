// nexigen-em — Execution Manager suite front-end (change
// unify-exec-suite-control-path; spec exec/process-types).
//
// `nexigen-em --config` starts the same detached suite supervisor as
// `nexigen-exec start --config` and returns once the supervisor owns the
// suite. The supervisor child hosts the EM-side StateClient receiver and the
// per-application readiness listeners through the COM control host, so the
// Function Group transition path is identical to `nexigen-exec`.
//
// Built only when NEXIGEN_ENABLE_COM=ON.
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/core/result.hpp>
#include <nexigen/exec/control_host.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/machine_manifest.hpp>
#include <nexigen/exec/manifest_accessor.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace core = nexigen::core;
namespace exec = nexigen::exec;

namespace {

constexpr int exit_usage = 2;
constexpr int exit_failure = 1;

void print_usage(std::FILE* out) {
    std::fprintf(out, R"(Usage:
  nexigen-em --config <suite-manifest> [--manifest-sha256 <hex>]
              [--machine <machine-manifest>] [--run-id <id>] [--run-dir <path>]
  nexigen-em --help

Starts a detached suite supervisor. The supervisor serves SetState commands on
Nexigen/EM/StateCommand, feeds the readiness tracker from
Nexigen/EM/State/<app>, and runs until the suite is stopped.
)");
}

struct Options {
    std::string config{};
    std::string manifest_sha256{};
    std::string machine{};
    std::string run_id{};
    std::string run_dir{};
};

/// @brief Options that take one string value.
[[nodiscard]] bool is_string_option(std::string_view argument) noexcept {
    return argument == "--config" || argument == "--manifest-sha256" ||
           argument == "--machine" || argument == "--run-id" || argument == "--run-dir" ||
           argument == "--service" || argument == "--instance";
}

/// @brief Store a parsed string option.
void apply_string_option(std::string_view argument, std::string value, Options& options) {
    if (argument == "--config") {
        options.config = std::move(value);
    } else if (argument == "--manifest-sha256") {
        options.manifest_sha256 = std::move(value);
    } else if (argument == "--machine") {
        options.machine = std::move(value);
    } else if (argument == "--run-id") {
        options.run_id = std::move(value);
    } else if (argument == "--run-dir") {
        options.run_dir = std::move(value);
    }
}

[[nodiscard]] bool parse_args(int argc, char* argv[], Options& options, int& exit_code) {
    exit_code = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            print_usage(stdout);
            return false;
        }
        // `--service` and `--instance` were accepted by the former direct host.
        // They are parsed for CLI compatibility; the suite path uses the
        // standard `Nexigen/EM/StateCommand` channel and per-app instances.
        if (is_string_option(argument)) {
            if (index + 1 >= argc) {
                print_usage(stderr);
                exit_code = exit_usage;
                return false;
            }
            apply_string_option(argument, argv[++index], options);
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
    exec::install_control_host_factory();

    Options options{};
    int exit_code = 0;
    if (!parse_args(argc, argv, options, exit_code)) {
        return exit_code;
    }

    exec::SuiteManifest manifest{};
    const core::Status loaded =
        exec::load_suite_manifest_auto(options.config, options.manifest_sha256, manifest);
    if (!loaded) {
        std::fprintf(stderr, "nexigen-em: manifest failed: %s\n",
                     std::string(core::to_string(loaded.error())).c_str());
        return exit_failure;
    }
    if (!options.machine.empty()) {
        exec::MachineManifest machine{};
        const core::Status machine_loaded =
            exec::load_machine_manifest_auto(options.machine, machine);
        if (!machine_loaded) {
            std::fprintf(stderr, "nexigen-em: machine manifest failed: %s\n",
                         std::string(core::to_string(machine_loaded.error())).c_str());
            return exit_failure;
        }
        if (!exec::validate_execution_against_machine(manifest, machine).has_value()) {
            std::fprintf(stderr, "nexigen-em: machine reference mismatch\n");
            return exit_failure;
        }
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

    exec::ExecutionManager manager;
    std::string suite_dir;
    const core::Status started = manager.start_suite(manifest, suite_dir);
    if (!started) {
        std::fprintf(stderr, "nexigen-em: start failed: %s\n",
                     std::string(core::to_string(started.error())).c_str());
        return exit_failure;
    }
    std::printf("nexigen-em: started suite %s at %s\n", manifest.suite_id.c_str(),
                suite_dir.c_str());
    return 0;
}
