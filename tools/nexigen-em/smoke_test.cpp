// nexigen-em smoke test (change introduce-platformfg-process-model).
//
// Starts the real nexigen-em host with a one-shot application, sends Run
// repeatedly, and asserts that the host confirms the switch without launching
// the one-shot more than once.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/com/client.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/instance_file_io.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_command.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace com = nexigen::com;
namespace core = nexigen::core;
namespace exec = nexigen::exec;

namespace {

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

[[nodiscard]] std::string make_root() {
    char pattern[] = "/tmp/opencode/nexigen_em_smoke_XXXXXX";
    char* const created = ::mkdtemp(pattern);
    return created == nullptr ? std::string{} : std::string(created);
}

void write_marker_script(const std::string& path, const std::string& marker) {
    std::ofstream script(path, std::ios::binary | std::ios::trunc);
    script << "#!/bin/sh\n";
    script << "echo run >> " << marker << "\n";
    script << "exit 0\n";
    script.close();
    static_cast<void>(::chmod(path.c_str(), 0755));
}

[[nodiscard]] exec::StateCommand make_command(const char* group, const char* target) {
    exec::StateCommand command{};
    std::snprintf(command.function_group, sizeof(command.function_group), "%s", group);
    std::snprintf(command.target_state, sizeof(command.target_state), "%s", target);
    return command;
}

[[nodiscard]] int count_lines(const std::string& path) {
    std::ifstream input(path);
    int lines = 0;
    std::string line;
    while (std::getline(input, line)) {
        ++lines;
    }
    return lines;
}

using StateClient = com::Client<exec::StateCommand, exec::StateAck>;

[[nodiscard]] std::optional<exec::StateAck> call_run(StateClient& client) {
    const auto response =
        client.call(make_command("smoke_group", "Run"), std::chrono::milliseconds(500));
    if (!response) {
        return std::nullopt;
    }
    return *response;
}

[[nodiscard]] std::optional<exec::StateAck> wait_for_first_ack(StateClient& client) {
    std::optional<exec::StateAck> ack;
    for (int i = 0; i < 100 && !ack; ++i) {
        ack = call_run(client);
        if (!ack) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    return ack;
}

[[nodiscard]] std::optional<exec::StateAck>
wait_for_confirmation(StateClient& client, std::optional<exec::StateAck> ack) {
    for (int i = 0; i < 200 && (!ack || ack->result != exec::StateResult::Success); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ack = call_run(client);
    }
    return ack;
}

void terminate_child(pid_t child) {
    if (child <= 0) {
        return;
    }
    static_cast<void>(::kill(child, SIGTERM));
    for (int i = 0; i < 100; ++i) {
        int status = 0;
        if (::waitpid(child, &status, WNOHANG) == child) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    static_cast<void>(::kill(child, SIGKILL));
    int status = 0;
    static_cast<void>(::waitpid(child, &status, 0));
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <nexigen-em>\n", argv[0]);
        return 2;
    }
    const std::string host = argv[1];
    const std::string root = make_root();
    NX_CHECK(!root.empty());
    if (root.empty()) {
        return 1;
    }
    const std::string run_dir = root + "/run";
    const std::string artifacts = root + "/artifacts";
    const std::string marker = root + "/runs";
    const std::string script = root + "/oneshot.sh";
    std::filesystem::create_directories(run_dir);
    std::filesystem::create_directories(artifacts);
    write_marker_script(script, marker);

    exec::SuiteManifest manifest{};
    manifest.version = 1;
    manifest.suite_id = "em_smoke";
    manifest.run_id = "smoke";
    manifest.run_dir = run_dir;
    manifest.artifacts_path = artifacts;
    exec::FunctionGroupConfig group{};
    group.name = "smoke_group";
    group.states = {"Off", "Run"};
    group.initial_state = "Run";
    manifest.function_groups.push_back(group);
    exec::AppSpec app{};
    app.app = "oneshot_app";
    app.instance = "1";
    app.command = script;
    app.type = exec::ProcessType::Oneshot;
    app.function_group = "smoke_group";
    app.impact = exec::ImpactOnAppSuiteOperability::Not_impact;
    exec::StateDependentStartupConfig config;
    config.state = "Run";
    app.startup_configs.push_back(config);
    manifest.apps.push_back(app);

    const std::string manifest_path = root + "/manifest.json";
    nlohmann::json document;
    exec::to_json(document, manifest);
    const core::Status written = exec::write_json_file(manifest_path, document);
    NX_CHECK(written.has_value());
    if (!written) {
        std::filesystem::remove_all(root);
        return 1;
    }

    const std::string service_name = "Nexigen/Test/EmSmoke";
    const pid_t child = ::fork();
    NX_CHECK(child >= 0);
    if (child == 0) {
        ::execl(host.c_str(), "nexigen-em", "--config", manifest_path.c_str(), "--service",
                service_name.c_str(), "--run-dir", run_dir.c_str(), "--instance", "1",
                static_cast<char*>(nullptr));
        ::_exit(127);
    }
    if (child < 0) {
        std::filesystem::remove_all(root);
        return 1;
    }

    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create(service_name.c_str());
    NX_CHECK(name.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    std::optional<exec::StateAck> ack = wait_for_first_ack(*client);
    NX_CHECK(ack.has_value());
    NX_CHECK(ack.has_value() && ack->accepted == 1U);
    NX_CHECK(ack.has_value() && ack->result == exec::StateResult::InProgress);

    ack = wait_for_confirmation(*client, ack);
    NX_CHECK(ack.has_value() && ack->accepted == 1U);
    NX_CHECK(ack.has_value() && ack->result == exec::StateResult::Success);
    NX_CHECK(count_lines(marker) == 1);

    terminate_child(child);
    std::filesystem::remove_all(root);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
