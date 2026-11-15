// nexigen-em smoke test (changes introduce-platformfg-process-model,
// unify-exec-suite-control-path).
//
// Starts the real nexigen-em suite front-end with a one-shot application, sends
// Run to the standard suite supervisor command channel, and asserts that the
// switch is confirmed without launching the one-shot more than once.
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
#include <nexigen/core/integrity/sha256.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/instance_file_io.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_command.hpp>
#include <nexigen/exec/suite_manifest.hpp>
#include <nexigen/phm/recovery_request.hpp>
#include <nexigen/phm/recovery_response.hpp>

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

/// Wait for the launcher to exit after handing the suite to the supervisor.
[[nodiscard]] bool wait_for_exit(pid_t child, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited += 10) {
        int status = 0;
        if (::waitpid(child, &status, WNOHANG) == child) {
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    static_cast<void>(::kill(child, SIGKILL));
    int status = 0;
    static_cast<void>(::waitpid(child, &status, 0));
    return false;
}

[[nodiscard]] bool wait_for_file(const std::string& path, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited += 20) {
        if (std::filesystem::exists(path)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

} // namespace

/// PHM recovery: the EM force-restarts the named application in a new attempt
/// and does not consume the abnormal-termination retry budget.
void verify_phm_restart(com::Node& node, const std::string& marker, const std::string& suite_dir) {
    const auto recovery_name = com::ServiceName::create("Nexigen/PHM/Recovery");
    NX_CHECK(recovery_name.has_value());
    auto recovery =
        com::Client<nexigen::phm::RecoveryRequest, nexigen::phm::RecoveryResponse>::create(
            node, *recovery_name);
    NX_CHECK(recovery.has_value());
    nexigen::phm::RecoveryRequest request{};
    request.action = nexigen::phm::RecoveryAction::RestartProcess;
    std::snprintf(request.name, sizeof(request.name), "%s", "oneshot_app");
    const auto response = recovery->call(request, std::chrono::milliseconds(2000));
    NX_CHECK(response.has_value());
    NX_CHECK(response.has_value() &&
             response->outcome == nexigen::phm::RecoveryOutcome::Accepted);
    for (int i = 0; i < 200 && count_lines(marker) < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    NX_CHECK(count_lines(marker) == 2);

    // The relaunch bumps the attempt counter asynchronously; poll the report.
    std::uint32_t attempt = 0;
    std::uint32_t restarts_used = 1;
    for (int i = 0; i < 200; ++i) {
        std::ifstream state_in(suite_dir + "/suite-state.json");
        if (state_in.is_open()) {
            const nlohmann::json state = nlohmann::json::parse(state_in, nullptr, false);
            if (!state.is_discarded() && state.contains("apps") && state["apps"].is_array() &&
                !state["apps"].empty()) {
                attempt = state["apps"][0]["attempt"].get<std::uint32_t>();
                restarts_used = state["apps"][0]["restarts_used"].get<std::uint32_t>();
            }
        }
        if (attempt >= 2U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    NX_CHECK(attempt >= 2U);
    // A recovery restart now spends one attempt of the configured budget
    // instead of leaving it untouched.
    NX_CHECK(restarts_used == 1U);
}

/// A wrong manifest digest starts nothing: the loader runs before any fork.
void verify_digest_gate(const std::string& host, const std::string& manifest_path,
                        const std::string& root) {
    const auto digest = core::integrity::sha256_file(manifest_path);
    NX_CHECK(digest.has_value());
    std::string wrong = *digest;
    wrong[0] = wrong[0] == 'a' ? 'b' : 'a';
    const std::string bad_dir = root + "/bad-run";
    std::filesystem::create_directories(bad_dir);
    const pid_t bad_child = ::fork();
    NX_CHECK(bad_child >= 0);
    if (bad_child == 0) {
        ::execl(host.c_str(), "nexigen-em", "--config", manifest_path.c_str(), "--manifest-sha256",
                wrong.c_str(), "--run-dir", bad_dir.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
    int status = 0;
    NX_CHECK(::waitpid(bad_child, &status, 0) == bad_child);
    NX_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    NX_CHECK(!std::filesystem::exists(bad_dir + "/.suite/em_smoke/suite.json"));
}

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
    // A PHM recovery restart spends one attempt of this budget, so the suite
    // must grant one for the recovery path to be admitted at all.
    config.restart_attempts = 3;
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

    // The positive path starts the suite only after the digest matches.
    const auto manifest_digest = core::integrity::sha256_file(manifest_path);
    NX_CHECK(manifest_digest.has_value());
    const pid_t child = ::fork();
    NX_CHECK(child >= 0);
    if (child == 0) {
        ::execl(host.c_str(), "nexigen-em", "--config", manifest_path.c_str(), "--manifest-sha256",
                manifest_digest->c_str(), "--run-dir", run_dir.c_str(),
                static_cast<char*>(nullptr));
        ::_exit(127);
    }
    if (child < 0) {
        std::filesystem::remove_all(root);
        return 1;
    }
    NX_CHECK(wait_for_exit(child, 20000));

    const std::string suite_dir = run_dir + "/.suite/em_smoke";
    NX_CHECK(wait_for_file(suite_dir + "/suite.json", 5000));

    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/EM/StateCommand");
    NX_CHECK(name.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    std::optional<exec::StateAck> ack;
    for (int i = 0; i < 200 && (!ack || ack->result != exec::StateResult::Success); ++i) {
        ack = call_run(*client);
        if (!ack || ack->result == exec::StateResult::InProgress) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    NX_CHECK(ack.has_value() && ack->accepted == 1U);
    NX_CHECK(ack.has_value() && ack->result == exec::StateResult::Success);
    NX_CHECK(count_lines(marker) == 1);

    verify_phm_restart(*node, marker, suite_dir);

    verify_digest_gate(host, manifest_path, root);

    // Stop through the standard suite control file and wait for the supervisor.
    { std::ofstream stop(suite_dir + "/stop.request", std::ios::binary | std::ios::trunc); }
    NX_CHECK(wait_for_file(suite_dir + "/suite-exit.json", 10000));

    std::filesystem::remove_all(root);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
