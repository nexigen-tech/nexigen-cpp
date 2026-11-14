// nexigen::exec suite control host tests (change
// unify-exec-suite-control-path). Built only when NEXIGEN_ENABLE_COM=ON.
//
// Exercises the COM control host directly: the shared StateClient/transition
// engine, readiness gating through the per-application event channels, the
// suite-owned start/stop handlers, and the reset used by a whole-suite restart.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/com/client.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/application_state.hpp>
#include <nexigen/exec/control_host.hpp>
#include <nexigen/exec/execution_dependency.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/instance_file_io.hpp>
#include <nexigen/exec/process_state.hpp>
#include <nexigen/exec/process_type.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_command.hpp>
#include <nexigen/exec/state_result.hpp>
#include <nexigen/exec/suite_control.hpp>
#include <nexigen/exec/suite_manifest.hpp>
#include <nexigen/exec/suite_status.hpp>

namespace app = nexigen::app;
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

[[nodiscard]] exec::SuiteManifest make_manifest(const std::string& run_dir) {
    exec::SuiteManifest manifest;
    manifest.version = 1;
    manifest.suite_id = "control_host";
    manifest.run_id = "test";
    manifest.run_dir = run_dir;
    manifest.artifacts_path = run_dir + "/artifacts";
    exec::FunctionGroupConfig group;
    group.name = "ready_group";
    group.states = {"Off", "Run"};
    manifest.function_groups.push_back(group);

    exec::AppSpec service;
    service.app = "service";
    service.instance = "1";
    service.command = "/bin/true";
    service.function_group = "ready_group";
    service.reporting = true;
    service.type = exec::ProcessType::Daemon;
    exec::StateDependentStartupConfig service_config;
    service_config.state = "Run";
    service.startup_configs.push_back(service_config);
    manifest.apps.push_back(service);

    exec::AppSpec client;
    client.app = "client";
    client.instance = "1";
    client.command = "/bin/true";
    client.function_group = "ready_group";
    client.reporting = false;
    client.type = exec::ProcessType::Daemon;
    exec::StateDependentStartupConfig client_config;
    client_config.state = "Run";
    client_config.dependencies.push_back(
        exec::ExecutionDependency{"service", exec::ProcessState::Running});
    client.startup_configs.push_back(client_config);
    manifest.apps.push_back(client);
    return manifest;
}

void test_control_host() {
    exec::install_control_host_factory();
    NX_CHECK(exec::control_host_factory() != nullptr);

    const exec::SuiteManifest manifest = make_manifest("/tmp/opencode/suite_control_host");
    app::RunIdentity identity{manifest.run_id, manifest.run_dir, "1", {}};
    auto host = exec::control_host_factory()(manifest, identity);
    NX_CHECK(host != nullptr);
    if (host == nullptr) {
        return;
    }

    std::vector<std::string> started;
    std::vector<std::string> stopped;
    host->set_start_handler([&started](std::string_view name, std::string_view) -> core::Status {
        started.emplace_back(name);
        return {};
    });
    host->set_stop_handler([&stopped](std::string_view name, std::string_view) -> core::Status {
        stopped.emplace_back(name);
        return {};
    });

    // Unknown group is rejected.
    NX_CHECK(host->request_state("missing", "Run") == exec::StateResult::InvalidArgument);

    // The reporting service starts; the dependent client waits for readiness.
    NX_CHECK(host->request_state("ready_group", "Run") == exec::StateResult::InProgress);
    NX_CHECK(started.size() == 1U && started.front() == "service");
    NX_CHECK(host->transition_active("ready_group"));
    NX_CHECK(host->group_state("ready_group") == "Off");
    NX_CHECK(!host->ready("service"));

    auto reporter_node = com::Node::create();
    NX_CHECK(reporter_node.has_value());
    auto reporter = exec::ApplicationStateReporter::create(*reporter_node, "service");
    NX_CHECK(reporter.has_value());
    if (reporter) {
        NX_CHECK(reporter->report(exec::AppState::Running).has_value());
    }
    for (int i = 0; i < 100 && !host->ready("service"); ++i) {
        host->poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    NX_CHECK(host->ready("service"));
    NX_CHECK(host->app_state("service") == exec::AppState::Running);

    // Readiness releases the dependent and confirms the target state.
    NX_CHECK(host->request_state("ready_group", "Run") == exec::StateResult::Success);
    NX_CHECK(host->group_state("ready_group") == "Run");
    NX_CHECK(!host->transition_active("ready_group"));
    NX_CHECK(started.size() == 2U && started[1] == "client");

    // Off stops in reverse dependency order: client before service.
    NX_CHECK(host->request_state("ready_group", "Off") == exec::StateResult::Success);
    NX_CHECK(host->group_state("ready_group") == "Off");
    NX_CHECK(stopped.size() == 2U);
    NX_CHECK(stopped.size() == 2U && stopped[0] == "client" && stopped[1] == "service");

    // Reset re-arms the transition, proving a whole-suite restart can re-enter
    // the configured initial state without a stale current state.
    host->reset_groups();
    NX_CHECK(!host->transition_active("ready_group"));
    NX_CHECK(host->group_state("ready_group") == "Off");
    NX_CHECK(host->request_state("ready_group", "Run") == exec::StateResult::InProgress);
    NX_CHECK(started.size() == 3U && started[2] == "service");

    exec::set_control_host_factory(nullptr);
}

/// Wait until a path exists, polling at a fixed interval.
[[nodiscard]] bool wait_for_path(const std::string& path, int attempts, int interval_ms) {
    for (int i = 0; i < attempts; ++i) {
        if (std::filesystem::exists(path)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    return std::filesystem::exists(path);
}

/// Build the end-to-end suite: a reporting provider and a dependent consumer.
[[nodiscard]] exec::SuiteManifest make_e2e_manifest(const std::string& run_dir,
                                                    const std::string& artifacts,
                                                    const std::string& marker) {
    exec::SuiteManifest manifest;
    manifest.version = 1;
    manifest.suite_id = "e2e_control";
    manifest.run_id = "test";
    manifest.run_dir = run_dir;
    manifest.artifacts_path = artifacts;
    manifest.suite_restart_attempts = 0;
    manifest.suite_restart_delay_s = 0;
    exec::FunctionGroupConfig group;
    group.name = "e2e_group";
    group.states = {"Off", "Run"};
    group.initial_state = "Run";
    manifest.function_groups.push_back(group);

    exec::AppSpec provider;
    provider.app = "provider";
    provider.instance = "1";
    provider.command = "/bin/sh";
    provider.function_group = "e2e_group";
    provider.reporting = true;
    provider.type = exec::ProcessType::Daemon;
    exec::StateDependentStartupConfig provider_config;
    provider_config.state = "Run";
    provider_config.args = {"-c", "exec sleep 60"};
    provider.startup_configs.push_back(provider_config);
    manifest.apps.push_back(provider);

    exec::AppSpec consumer;
    consumer.app = "consumer";
    consumer.instance = "1";
    consumer.command = "/bin/sh";
    consumer.function_group = "e2e_group";
    consumer.reporting = false;
    consumer.type = exec::ProcessType::Daemon;
    exec::StateDependentStartupConfig consumer_config;
    consumer_config.state = "Run";
    consumer_config.args = {"-c", "echo consumed >> " + marker + "; exec sleep 60"};
    consumer_config.dependencies.push_back(
        exec::ExecutionDependency{"provider", exec::ProcessState::Running});
    consumer.startup_configs.push_back(consumer_config);
    manifest.apps.push_back(consumer);
    return manifest;
}

/// Send SetState until the transition confirms or the attempts run out.
[[nodiscard]] std::optional<exec::StateAck>
request_state(com::Client<exec::StateCommand, exec::StateAck>& client, const char* group_name,
              const char* target) {
    std::optional<exec::StateAck> ack;
    for (int i = 0; i < 200; ++i) {
        exec::StateCommand command{};
        std::snprintf(command.function_group, sizeof(command.function_group), "%s", group_name);
        std::snprintf(command.target_state, sizeof(command.target_state), "%s", target);
        const auto response = client.call(command, std::chrono::milliseconds(500));
        if (response) {
            ack = *response;
            if (ack->result == exec::StateResult::Success) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return ack;
}

/// End-to-end through the real detached suite supervisor: a reporting daemon
/// gates its dependent, the initial state enters through the transition engine,
/// and a SetState command switches the group.
void test_suite_control_path() {
    exec::install_control_host_factory();
    NX_CHECK(exec::control_host_factory() != nullptr);

    const std::string root = "/tmp/opencode/suite_control_e2e";
    const std::string run_dir = root + "/run";
    const std::string artifacts = root + "/artifacts";
    const std::string marker = root + "/consumed";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(run_dir, error);
    std::filesystem::create_directories(artifacts, error);

    const exec::SuiteManifest manifest = make_e2e_manifest(run_dir, artifacts, marker);
    std::string suite_dir;
    NX_CHECK(exec::start_suite(manifest, suite_dir).has_value());
    NX_CHECK(!suite_dir.empty());
    if (suite_dir.empty()) {
        std::filesystem::remove_all(root, error);
        return;
    }

    // The provider is launched by the transition engine; the dependent must not
    // start until the provider reports readiness.
    static_cast<void>(wait_for_path(suite_dir + "/suite-state.json", 100, 20));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    NX_CHECK(!std::filesystem::exists(marker));

    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    auto reporter = exec::ApplicationStateReporter::create(*node, "provider");
    NX_CHECK(reporter.has_value());
    for (int i = 0; i < 200 && !std::filesystem::exists(marker); ++i) {
        if (reporter) {
            NX_CHECK(reporter->report(exec::AppState::Running).has_value());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    NX_CHECK(std::filesystem::exists(marker));

    const auto service = com::ServiceName::create("Nexigen/EM/StateCommand");
    NX_CHECK(service.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *service);
    NX_CHECK(client.has_value());

    // The initial Run confirms through the same transition engine.
    const auto confirmed = request_state(*client, "e2e_group", "Run");
    NX_CHECK(confirmed.has_value() && confirmed->result == exec::StateResult::Success);

    // A state command switches the group off and stops both processes.
    const auto stopped = request_state(*client, "e2e_group", "Off");
    NX_CHECK(stopped.has_value() && stopped->result == exec::StateResult::Success);
    for (int i = 0; i < 100; ++i) {
        exec::SuiteStatus status{};
        if (exec::suite_status(suite_dir, status).has_value()) {
            bool all_stopped = !status.apps.empty();
            for (const exec::SuiteAppStatus& app : status.apps) {
                all_stopped = all_stopped && app.state == exec::AppState::Stopped;
            }
            if (all_stopped) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    exec::SuiteStatus final_status{};
    NX_CHECK(exec::suite_status(suite_dir, final_status).has_value());
    for (const exec::SuiteAppStatus& app : final_status.apps) {
        NX_CHECK(app.state == exec::AppState::Stopped);
    }

    NX_CHECK(exec::stop_suite(suite_dir, 2).has_value());
    std::filesystem::remove_all(root, error);
    exec::set_control_host_factory(nullptr);
}

} // namespace

int main() {
    test_control_host();
    test_suite_control_path();
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
