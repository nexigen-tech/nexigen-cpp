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
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/application_state.hpp>
#include <nexigen/exec/control_host.hpp>
#include <nexigen/exec/execution_dependency.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/instance_file_io.hpp>
#include <nexigen/exec/root_state_query.hpp>
#include <nexigen/exec/process_lifecycle.hpp>
#include <nexigen/exec/process_state.hpp>
#include <nexigen/exec/process_type.hpp>
#include <nexigen/exec/recovery_handler.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_command.hpp>
#include <nexigen/exec/state_result.hpp>
#include <nexigen/exec/suite_control.hpp>
#include <nexigen/exec/suite_manifest.hpp>
#include <nexigen/exec/suite_state.hpp>
#include <nexigen/exec/suite_status.hpp>
#include <nexigen/phm/recovery_request.hpp>
#include <nexigen/phm/recovery_response.hpp>

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

    // Recovery restart: rejected while the application is starting, allowed
    // once it is running, and never budgeted as an abnormal-termination retry.
    int restarts = 0;
    host->set_restart_handler([&restarts](std::string_view) -> core::Status {
        ++restarts;
        return {};
    });
    host->on_launched("service");
    NX_CHECK(!host->restart_process("service").has_value());
    NX_CHECK(restarts == 0);

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

    // A running application accepts the recovery restart and the callback runs
    // exactly once.
    NX_CHECK(host->restart_process("service").has_value());
    NX_CHECK(restarts == 1);

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

/// Drain every buffered lifecycle sample into a readable list.
void drain_lifecycle(com::Subscriber<exec::ProcessLifecycleEvent>& subscriber,
                     std::vector<std::string>& out) {
    for (;;) {
        auto received = subscriber.receive();
        if (!received || !received->has_value()) {
            return;
        }
        const exec::ProcessLifecycleEvent& event = (*received)->payload();
        out.push_back(std::string(event.app) + ":" + std::string(exec::to_string(event.state)));
    }
}

/// Whether one application's sample with the given state is present.
[[nodiscard]] bool lifecycle_seen(const std::vector<std::string>& events, std::string_view app,
                                  std::string_view state) {
    for (const std::string& event : events) {
        if (event == std::string(app) + ":" + std::string(state)) {
            return true;
        }
    }
    return false;
}

/// Whether one application's samples are ordered Running -> Terminating -> Terminated.
[[nodiscard]] bool lifecycle_order_ok(const std::vector<std::string>& events,
                                      std::string_view app) {
    std::optional<std::size_t> running;
    std::optional<std::size_t> terminating;
    std::optional<std::size_t> terminated;
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (events[i] == std::string(app) + ":Running") {
            running = i;
        } else if (events[i] == std::string(app) + ":Terminating") {
            terminating = i;
        } else if (events[i] == std::string(app) + ":Terminated") {
            terminated = i;
        }
    }
    return running && terminating && terminated && *running < *terminating &&
           *terminating < *terminated;
}

/// Observe the EM→PHM lifecycle samples across one Run/Off cycle.
void test_lifecycle_publication() {
    exec::install_control_host_factory();
    const exec::SuiteManifest manifest = make_manifest("/tmp/opencode/suite_control_lifecycle");
    app::RunIdentity identity{manifest.run_id, manifest.run_dir, "1", {}};
    auto host = exec::control_host_factory()(manifest, identity);
    NX_CHECK(host != nullptr);
    if (host == nullptr) {
        return;
    }
    host->set_start_handler([](std::string_view, std::string_view) -> core::Status { return {}; });
    host->set_stop_handler([](std::string_view, std::string_view) -> core::Status { return {}; });

    auto observer_node = com::Node::create();
    NX_CHECK(observer_node.has_value());
    const auto lifecycle_name = com::ServiceName::create("Nexigen/PHM/ExecutionState");
    const auto lifecycle_event_name = com::ServiceName::create("Nexigen/PHM/ExecutionStateEvent");
    NX_CHECK(lifecycle_name.has_value() && lifecycle_event_name.has_value());
    auto subscriber = com::Subscriber<exec::ProcessLifecycleEvent>::create(*observer_node,
                                                                           *lifecycle_name, 64, 64);
    auto listener = com::EventListener::create(*observer_node, *lifecycle_event_name);
    NX_CHECK(subscriber.has_value() && listener.has_value());
    std::vector<std::string> lifecycle;

    NX_CHECK(host->request_state("ready_group", "Run") == exec::StateResult::InProgress);
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
    host->poll();
    drain_lifecycle(*subscriber, lifecycle);
    NX_CHECK(lifecycle_seen(lifecycle, "service", "Running"));

    NX_CHECK(host->request_state("ready_group", "Run") == exec::StateResult::Success);
    host->poll();
    drain_lifecycle(*subscriber, lifecycle);
    NX_CHECK(lifecycle_seen(lifecycle, "client", "Running"));

    NX_CHECK(host->request_state("ready_group", "Off") == exec::StateResult::Success);
    host->poll();
    drain_lifecycle(*subscriber, lifecycle);
    NX_CHECK(lifecycle_order_ok(lifecycle, "service"));
    NX_CHECK(lifecycle_order_ok(lifecycle, "client"));
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

    exec::FunctionGroupConfig machine;
    machine.name = "RootFG";
    machine.states = {"Off", "Startup", "Verify", "Run", "Shutdown", "Restart"};
    machine.initial_state = "Startup";
    manifest.function_groups.push_back(machine);

    exec::AppSpec platform_init;
    platform_init.app = "platform_init";
    platform_init.instance = "1";
    platform_init.command = "/bin/sh";
    platform_init.function_group = "RootFG";
    platform_init.reporting = false;
    platform_init.type = exec::ProcessType::Oneshot;
    platform_init.impact = exec::ImpactOnAppSuiteOperability::Not_impact;
    exec::StateDependentStartupConfig platform_config;
    platform_config.state = "Startup";
    platform_config.args = {"-c", "exit 0"};
    platform_init.startup_configs.push_back(platform_config);
    manifest.apps.push_back(platform_init);

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

/// A RootFG that starts Off is recorded as a successful EM-owned result.
void test_machinefg_off_initial() {
    exec::install_control_host_factory();
    exec::SuiteManifest manifest = make_manifest("/tmp/opencode/suite_control_machine_off");
    exec::FunctionGroupConfig machine;
    machine.name = "RootFG";
    machine.states = {"Off", "Startup", "Verify", "Run", "Shutdown", "Restart"};
    machine.initial_state = "Off";
    manifest.function_groups.push_back(machine);
    app::RunIdentity identity{manifest.run_id, manifest.run_dir, "1", {}};
    auto host = exec::control_host_factory()(manifest, identity);
    NX_CHECK(host != nullptr);
    if (host == nullptr) {
        return;
    }
    NX_CHECK(host->request_state("RootFG", "Off") == exec::StateResult::Success);
    NX_CHECK(host->group_state("RootFG") == "Off");

    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto query_name =
        com::ServiceName::create("Nexigen/EM/InitialRootStateTransitionResult");
    NX_CHECK(query_name.has_value());
    auto query =
        com::Client<std::monostate, exec::InitialRootStateTransitionResultResponse>::create(
            *node, *query_name);
    NX_CHECK(query.has_value());
    exec::StateResult result = exec::StateResult::InProgress;
    std::thread poller([&host] {
        for (int i = 0; i < 100; ++i) {
            host->poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    const auto response = query->call(std::monostate{}, std::chrono::milliseconds(500));
    if (response) {
        result = response->result;
    }
    poller.join();
    NX_CHECK(result == exec::StateResult::Success);
    exec::set_control_host_factory(nullptr);
}

/// Wait until the EM reports a terminal initial RootFG result.
[[nodiscard]] exec::StateResult wait_for_initial_machine_result(
    com::Client<std::monostate, exec::InitialRootStateTransitionResultResponse>& query) {
    exec::StateResult result = exec::StateResult::InProgress;
    for (int i = 0; i < 100 && result == exec::StateResult::InProgress; ++i) {
        const auto response = query.call(std::monostate{}, std::chrono::milliseconds(200));
        if (response) {
            result = response->result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return result;
}

/// Switch the application group Off and wait until both apps are Stopped.
[[nodiscard]] bool stop_group_and_wait(const std::string& suite_dir,
                                       com::Client<exec::StateCommand, exec::StateAck>& client) {
    const auto stopped = request_state(client, "e2e_group", "Off");
    if (!stopped || stopped->result != exec::StateResult::Success) {
        return false;
    }
    for (int i = 0; i < 100; ++i) {
        exec::SuiteStatus status{};
        if (!exec::suite_status(suite_dir, status).has_value()) {
            return false;
        }
        bool all_stopped = true;
        bool saw_group_app = false;
        for (const exec::SuiteAppStatus& app : status.apps) {
            if (app.app != "provider" && app.app != "consumer") {
                continue;
            }
            saw_group_app = true;
            all_stopped = all_stopped && app.state == exec::AppState::Stopped;
        }
        if (all_stopped && saw_group_app) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
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

    // The EM owns the initial RootFG transition and exposes its result.
    const auto query_name =
        com::ServiceName::create("Nexigen/EM/InitialRootStateTransitionResult");
    NX_CHECK(query_name.has_value());
    auto query =
        com::Client<std::monostate, exec::InitialRootStateTransitionResultResponse>::create(
            *node, *query_name);
    NX_CHECK(query.has_value());
    NX_CHECK(wait_for_initial_machine_result(*query) == exec::StateResult::Success);

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

    // RootFG Off is EM-owned and rejected over the public channel.
    {
        exec::StateCommand off{};
        std::snprintf(off.function_group, sizeof(off.function_group), "%s", "RootFG");
        std::snprintf(off.target_state, sizeof(off.target_state), "%s", "Off");
        const auto ack = client->call(off, std::chrono::milliseconds(500));
        NX_CHECK(ack.has_value() && ack->result == exec::StateResult::InvalidTransition);
    }

    // The initial Run confirms through the same transition engine.
    const auto confirmed = request_state(*client, "e2e_group", "Run");
    NX_CHECK(confirmed.has_value() && confirmed->result == exec::StateResult::Success);

    // A state command switches the group off and stops both processes.
    NX_CHECK(stop_group_and_wait(suite_dir, *client));

    NX_CHECK(exec::stop_suite(suite_dir, 2).has_value());
    std::filesystem::remove_all(root, error);
    exec::set_control_host_factory(nullptr);
}

void test_recovery_handler() {
    namespace phm = nexigen::phm;
    const auto name = com::ServiceName::create("Nexigen/Test/ExecRecovery");
    NX_CHECK(name.has_value());
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const exec::SuiteManifest manifest = make_manifest("/tmp/opencode/recovery_handler");
    auto handler = exec::RecoveryHandler::create(*node, *name, manifest);
    NX_CHECK(handler.has_value());
    if (!handler) {
        return;
    }

    phm::RecoveryRequest request{};
    request.action = phm::RecoveryAction::RestartProcess;
    std::snprintf(request.name, sizeof(request.name), "%s", "service");

    // No callback installed: rejected.
    NX_CHECK(handler->to_string().find("restart=unset") != std::string::npos);
    NX_CHECK(handler->handle(request).accepted == 0U);

    std::string restarted;
    handler->set_restart([&restarted](std::string_view app) -> core::Status {
        restarted = std::string(app);
        return {};
    });
    NX_CHECK(handler->handle(request).accepted == 1U);
    NX_CHECK(restarted == "service");
    NX_CHECK(handler->to_string().find("restart=set") != std::string::npos);

    // An application outside the suite is rejected.
    phm::RecoveryRequest unknown = request;
    std::snprintf(unknown.name, sizeof(unknown.name), "%s", "ghost");
    NX_CHECK(handler->handle(unknown).accepted == 0U);

    // The watchdog belongs to PHM, so the EM rejects the action.
    phm::RecoveryRequest watchdog = request;
    watchdog.action = phm::RecoveryAction::TriggerWatchdogReset;
    NX_CHECK(handler->handle(watchdog).accepted == 0U);

    // A failed restart is reported as rejected.
    handler->set_restart([](std::string_view) -> core::Status {
        return std::unexpected(core::ErrorCode::SystemError);
    });
    NX_CHECK(handler->handle(request).accepted == 0U);

    // A suite that is not operational rejects recovery (teardown race).
    NX_CHECK(exec::accepts_recovery_restart(exec::SuiteState::Running));
    NX_CHECK(exec::accepts_recovery_restart(exec::SuiteState::Degraded));
    NX_CHECK(!exec::accepts_recovery_restart(exec::SuiteState::Stopped));
    NX_CHECK(!exec::accepts_recovery_restart(exec::SuiteState::Failed));
    NX_CHECK(!exec::accepts_recovery_restart(exec::SuiteState::Unknown));

    // The real channel answers instead of timing out.
    handler->set_restart([&restarted](std::string_view app) -> core::Status {
        restarted = std::string(app);
        return {};
    });
    auto client = com::Client<phm::RecoveryRequest, phm::RecoveryResponse>::create(*node, *name);
    NX_CHECK(client.has_value());
    if (!client) {
        return;
    }
    std::atomic<bool> done{false};
    phm::RecoveryResponse answer{};
    std::thread caller([&] {
        const auto response = client->call(request, std::chrono::milliseconds(2000));
        if (response.has_value()) {
            answer = *response;
        }
        done.store(true, std::memory_order_release);
    });
    for (int i = 0; i < 1000 && !done.load(std::memory_order_acquire); ++i) {
        static_cast<void>(handler->serve_once());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    caller.join();
    NX_CHECK(done.load(std::memory_order_acquire));
    NX_CHECK(answer.accepted == 1U);
}

} // namespace

int main() {
    test_machinefg_off_initial();
    test_control_host();
    test_lifecycle_publication();
    test_recovery_handler();
    test_suite_control_path();
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
