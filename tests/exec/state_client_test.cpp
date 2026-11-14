// Тесты nexigen::exec::StateClient (change add-sm; add-em-orchestration adds
// Function Groups + SafeState priority). Сборка только при NEXIGEN_ENABLE_COM=ON.
// Mock SM — com::Client, шлющий SetState; реальный ExecutionManager исполняет
// команды во временном run-dir.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/com/client.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/exec/application_state.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/function_group_registry.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_client.hpp>
#include <nexigen/exec/state_command.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace com = nexigen::com;
namespace exec = nexigen::exec;
namespace app = nexigen::app;
namespace core = nexigen::core;

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

[[nodiscard]] std::string read_text_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

exec::StateCommand make_command(const char* group, const char* target) {
    exec::StateCommand command{};
    std::snprintf(command.function_group, sizeof(command.function_group), "%s", group);
    std::snprintf(command.target_state, sizeof(command.target_state), "%s", target);
    return command;
}

exec::SuiteManifest make_suite_manifest() {
    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "motion_control";
    group.states = {"Off", "Active", "SafeState"};
    group.initial_state = "Active";
    manifest.function_groups.push_back(group);
    exec::AppSpec app;
    app.app = "true_app";
    app.command = "/bin/true";
    app.function_group = "motion_control";
    app.reporting = false;
    exec::StateDependentStartupConfig config;
    config.state = "Active";
    app.startup_configs.push_back(config);
    manifest.apps.push_back(app);
    return manifest;
}

void test_state_client() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateCommand");
    NX_CHECK(name.has_value());

    exec::SuiteManifest manifest = make_suite_manifest();
    exec::FunctionGroupRegistry registry(manifest);
    NX_CHECK(registry.group_count() == 1U);
    NX_CHECK(registry.bound_apps(0).size() == 1U);
    NX_CHECK(registry.bound_apps(0)[0] == "true_app");
    const std::string rtext = registry.to_string();
    NX_CHECK(rtext.find("motion_control=Off") != std::string::npos);
    NX_CHECK(std::format("{}", registry) == rtext);

    exec::ExecutionManager manager;
    const std::string run_dir = "/tmp/opencode/state_client_run";
    std::filesystem::create_directories(run_dir);
    app::RunIdentity identity;
    identity.run_id = "test";
    identity.run_dir = run_dir;
    identity.instance_id = "inst1";

    auto state_client = exec::StateClient::create(*node, *name, registry, manager, identity);
    NX_CHECK(state_client.has_value());

    // Mock SM clients: ordinary + emergency channels.
    const auto emergency_name = com::ServiceName::create("Nexigen/Test/StateCommandEmergency");
    NX_CHECK(emergency_name.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    auto emergency_client =
        com::Client<exec::StateCommand, exec::StateAck>::create(*node, *emergency_name);
    NX_CHECK(client.has_value());
    NX_CHECK(emergency_client.has_value());

    // Unmapped group: rejection ack.
    {
        std::thread server_loop([&] {
            for (int i = 0; i < 200; ++i) {
                auto served = state_client->serve_once();
                if (!served || !*served) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
        const auto response =
            client->call(make_command("unknown_group", "Off"), std::chrono::milliseconds(2000));
        server_loop.join();
        NX_CHECK(response.has_value());
        NX_CHECK(response->accepted == 0U);
    }

    // Mapped group, active target: the EM launches /bin/true.
    {
        std::thread server_loop([&] {
            for (int i = 0; i < 200; ++i) {
                auto served = state_client->serve_once();
                if (!served || !*served) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
        const auto response =
            client->call(make_command("motion_control", "Active"), std::chrono::milliseconds(2000));
        server_loop.join();
        NX_CHECK(response.has_value());
        NX_CHECK(response->accepted == 1U);
        NX_CHECK(registry.group_state(0) == "Active");
    }

    // SafeState priority: an emergency command overtakes a queued ordinary one.
    {
        exec::StateAck ordinary_result{0};
        exec::StateAck emergency_result{0};
        std::atomic<bool> ordinary_done{false};
        std::atomic<bool> emergency_done{false};
        std::thread ordinary_sender([&] {
            auto response = client->call(make_command("motion_control", "Active"),
                                         std::chrono::milliseconds(3000));
            if (response.has_value()) {
                ordinary_result = *response;
                ordinary_done.store(true, std::memory_order_release);
            }
        });
        std::thread emergency_sender([&] {
            auto response = emergency_client->call(make_command("motion_control", "SafeState"),
                                                   std::chrono::milliseconds(3000));
            if (response.has_value()) {
                emergency_result = *response;
                emergency_done.store(true, std::memory_order_release);
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // One serve_once must drain the EMERGENCY channel first.
        const auto served = state_client->serve_once();
        NX_CHECK(served.has_value());
        NX_CHECK(*served);
        for (int i = 0; i < 50 && !emergency_done.load(std::memory_order_acquire); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        NX_CHECK(emergency_done.load(std::memory_order_acquire));
        NX_CHECK(emergency_result.accepted == 1U);
        NX_CHECK(!ordinary_done.load(std::memory_order_acquire));
        NX_CHECK(registry.group_state(0) == "SafeState");

        // The second serve_once handles the ordinary command.
        const auto served2 = state_client->serve_once();
        NX_CHECK(served2.has_value());
        NX_CHECK(*served2);
        for (int i = 0; i < 50 && !ordinary_done.load(std::memory_order_acquire); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        NX_CHECK(ordinary_done.load(std::memory_order_acquire));
        ordinary_sender.join();
        emergency_sender.join();
    }

    // Diagnostics name the service and the last command.
    const std::string text = state_client->to_string();
    NX_CHECK(text.find("Nexigen/Test/StateCommand") != std::string::npos);
    NX_CHECK(std::format("{}", *state_client) == text);

    std::filesystem::remove_all(run_dir);
}

void test_run_confirmation() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateCommandRun");
    NX_CHECK(name.has_value());

    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "run_group";
    group.states = {"Off", "Run"};
    manifest.function_groups.push_back(group);
    exec::AppSpec app;
    app.app = "daemon_app";
    app.command = "/bin/sh";
    app.function_group = "run_group";
    app.type = exec::ProcessType::Daemon;
    exec::StateDependentStartupConfig config;
    config.state = "Run";
    config.args = {"-c", "exec sleep 60"};
    app.startup_configs.push_back(config);
    manifest.apps.push_back(app);

    exec::FunctionGroupRegistry registry(manifest);
    exec::ExecutionStateTracker tracker;
    exec::ExecutionManager manager;
    const std::string run_dir = "/tmp/opencode/state_client_run_confirm";
    std::filesystem::create_directories(run_dir);
    app::RunIdentity identity;
    identity.run_id = "test";
    identity.run_dir = run_dir;
    identity.instance_id = "inst1";

    auto state_client =
        exec::StateClient::create(*node, *name, registry, manager, identity, &tracker);
    NX_CHECK(state_client.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    // The application reports readiness over its real Nexigen/EM/State/<app>
    // event channel; the StateClient listener feeds the tracker.
    auto reporter = exec::ApplicationStateReporter::create(*node, "daemon_app");
    NX_CHECK(reporter.has_value());

    auto request = [&](const char* target) {
        std::thread server_loop([&] {
            for (int i = 0; i < 200; ++i) {
                auto served = state_client->serve_once();
                if (!served || !*served) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
        const auto response =
            client->call(make_command("run_group", target), std::chrono::milliseconds(2000));
        server_loop.join();
        return response;
    };

    // A daemon that never reports readiness blocks the Run confirmation.
    exec::StateAck blocked = request("Run").value_or(exec::StateAck{0});
    NX_CHECK(blocked.accepted == 1U);
    NX_CHECK(blocked.result == exec::StateResult::InProgress);
    NX_CHECK(registry.group_state(0) == "Off");
    NX_CHECK(tracker.state("daemon_app") != exec::AppState::Running);

    // Once every bound daemon has reported, the switch is confirmed.
    NX_CHECK(reporter->report(exec::AppState::Running).has_value());
    for (int i = 0; i < 50 && tracker.state("daemon_app") != exec::AppState::Running; ++i) {
        const auto served = state_client->serve_once();
        NX_CHECK(served.has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    NX_CHECK(tracker.state("daemon_app") == exec::AppState::Running);
    exec::StateAck confirmed = request("Run").value_or(exec::StateAck{0});
    NX_CHECK(confirmed.accepted == 1U);
    NX_CHECK(confirmed.result == exec::StateResult::Success);
    NX_CHECK(registry.group_state(0) == "Run");

    std::filesystem::remove_all(run_dir);
}

bool app_running(exec::ExecutionManager& manager, const std::string& run_dir, const char* app) {
    std::string state;
    static_cast<void>(manager.status(run_dir + "/" + app + "/1", state));
    return state == "running";
}

void test_state_transitions() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateTransitions");
    NX_CHECK(name.has_value());
    const std::string run_dir = "/tmp/opencode/state_client_transitions";
    std::filesystem::create_directories(run_dir);

    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "vehicle";
    group.states = {"Off", "Active", "Fallback", "Parking"};
    manifest.function_groups.push_back(group);

    auto add_app = [&](const char* app_name, std::vector<std::string> states) {
        exec::AppSpec app;
        app.app = app_name;
        app.instance = "1";
        app.command = "/bin/sh";
        app.function_group = "vehicle";
        app.reporting = false;
        for (const std::string& state : states) {
            exec::StateDependentStartupConfig config;
            config.state = state;
            config.args = {"-c", "exec sleep 60"};
            app.startup_configs.push_back(std::move(config));
        }
        manifest.apps.push_back(std::move(app));
    };
    add_app("both_app", {"Active", "Fallback"});
    add_app("active_app", {"Active"});
    add_app("fallback_app", {"Fallback"});

    exec::FunctionGroupRegistry registry(manifest);
    exec::ExecutionStateTracker tracker;
    exec::ExecutionManager manager;
    app::RunIdentity identity{"test", run_dir, "inst1", {}};
    auto state_client =
        exec::StateClient::create(*node, *name, registry, manager, identity, &tracker);
    NX_CHECK(state_client.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    auto request = [&](const char* target) {
        std::thread loop([&] {
            for (int i = 0; i < 100; ++i) {
                auto served = state_client->serve_once();
                if (!served || !*served) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
        const auto response =
            client->call(make_command("vehicle", target), std::chrono::milliseconds(2000));
        loop.join();
        return response;
    };

    const auto active = request("Active");
    NX_CHECK(active.has_value() && active->result == exec::StateResult::Success);
    NX_CHECK(registry.group_state(0) == "Active");
    NX_CHECK(app_running(manager, run_dir, "both_app"));
    NX_CHECK(app_running(manager, run_dir, "active_app"));

    const auto active_again = request("Active");
    NX_CHECK(active_again.has_value() && active_again->result == exec::StateResult::Success);
    NX_CHECK(registry.group_state(0) == "Active");

    const auto fallback = request("Fallback");
    NX_CHECK(fallback.has_value() && fallback->result == exec::StateResult::Success);
    NX_CHECK(registry.group_state(0) == "Fallback");
    NX_CHECK(app_running(manager, run_dir, "both_app"));
    NX_CHECK(!app_running(manager, run_dir, "active_app"));
    NX_CHECK(app_running(manager, run_dir, "fallback_app"));

    const auto parking = request("Parking");
    NX_CHECK(parking.has_value() && parking->result == exec::StateResult::Success);
    NX_CHECK(registry.group_state(0) == "Parking");
    NX_CHECK(!app_running(manager, run_dir, "both_app"));
    NX_CHECK(!app_running(manager, run_dir, "fallback_app"));

    std::filesystem::remove_all(run_dir);
}

void test_transition_undefined() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateUndefined");
    NX_CHECK(name.has_value());
    const std::string run_dir = "/tmp/opencode/state_client_undefined";
    std::filesystem::create_directories(run_dir);

    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "failure_group";
    group.states = {"Off", "Run"};
    manifest.function_groups.push_back(group);
    exec::AppSpec app;
    app.app = "failing_app";
    app.instance = "1";
    app.command = "/bin/sh";
    app.function_group = "failure_group";
    app.reporting = false;
    app.type = exec::ProcessType::Oneshot;
    exec::StateDependentStartupConfig config;
    config.state = "Run";
    config.args = {"-c", "exit 1"};
    app.startup_configs.push_back(config);
    manifest.apps.push_back(app);

    exec::FunctionGroupRegistry registry(manifest);
    exec::ExecutionStateTracker tracker;
    exec::ExecutionManager manager;
    app::RunIdentity identity{"test", run_dir, "inst1", {}};
    auto state_client =
        exec::StateClient::create(*node, *name, registry, manager, identity, &tracker);
    NX_CHECK(state_client.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    auto request = [&](const char* target) {
        std::thread loop([&] {
            for (int i = 0; i < 100; ++i) {
                auto served = state_client->serve_once();
                if (!served || !*served) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
        const auto response =
            client->call(make_command("failure_group", target), std::chrono::milliseconds(2000));
        loop.join();
        return response;
    };

    const auto started = request("Run");
    NX_CHECK(started.has_value() && started->result == exec::StateResult::InProgress);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto failed = request("Run");
    NX_CHECK(failed.has_value() && failed->result == exec::StateResult::UnexpectedTermination);
    NX_CHECK(registry.group_state(0) == "Undefined");

    std::filesystem::remove_all(run_dir);
}

void test_dependency_order() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateDependencies");
    NX_CHECK(name.has_value());
    const std::string run_dir = "/tmp/opencode/state_client_dependencies";
    std::filesystem::create_directories(run_dir);

    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "dep_group";
    group.states = {"Off", "Run"};
    manifest.function_groups.push_back(group);
    exec::AppSpec initializer;
    initializer.app = "initializer";
    initializer.instance = "1";
    initializer.command = "/bin/sh";
    initializer.function_group = "dep_group";
    initializer.reporting = false;
    initializer.type = exec::ProcessType::Oneshot;
    exec::StateDependentStartupConfig init_config;
    init_config.state = "Run";
    init_config.args = {"-c", "exit 0"};
    initializer.startup_configs.push_back(init_config);
    manifest.apps.push_back(initializer);

    exec::AppSpec service;
    service.app = "service";
    service.instance = "1";
    service.command = "/bin/sh";
    service.function_group = "dep_group";
    service.reporting = false;
    exec::StateDependentStartupConfig service_config;
    service_config.state = "Run";
    service_config.args = {"-c", "exec sleep 60"};
    service_config.dependencies.push_back(
        exec::ExecutionDependency{"initializer", exec::ProcessState::Terminated});
    service.startup_configs.push_back(service_config);
    manifest.apps.push_back(service);

    exec::FunctionGroupRegistry registry(manifest);
    exec::ExecutionStateTracker tracker;
    exec::ExecutionManager manager;
    app::RunIdentity identity{"test", run_dir, "inst1", {}};
    auto state_client =
        exec::StateClient::create(*node, *name, registry, manager, identity, &tracker);
    NX_CHECK(state_client.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    auto request = [&](const char* target) {
        std::thread loop([&] {
            for (int i = 0; i < 100; ++i) {
                auto served = state_client->serve_once();
                if (!served || !*served) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
        const auto response =
            client->call(make_command("dep_group", target), std::chrono::milliseconds(2000));
        loop.join();
        return response;
    };

    const auto started = request("Run");
    NX_CHECK(started.has_value() && started->result == exec::StateResult::InProgress);
    NX_CHECK(!app_running(manager, run_dir, "service"));
    for (int i = 0; i < 50; ++i) {
        const auto served = state_client->serve_once();
        NX_CHECK(served.has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto confirmed = request("Run");
    NX_CHECK(confirmed.has_value() && confirmed->result == exec::StateResult::Success);
    NX_CHECK(app_running(manager, run_dir, "service"));

    std::filesystem::remove_all(run_dir);
}

void test_readiness_dependency() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateReadinessDependency");
    NX_CHECK(name.has_value());
    const std::string run_dir = "/tmp/opencode/state_client_readiness_dependency";
    std::filesystem::create_directories(run_dir);

    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "ready_group";
    group.states = {"Off", "Run"};
    manifest.function_groups.push_back(group);
    exec::AppSpec service;
    service.app = "service";
    service.instance = "1";
    service.command = "/bin/sh";
    service.function_group = "ready_group";
    service.reporting = true;
    exec::StateDependentStartupConfig service_config;
    service_config.state = "Run";
    service_config.args = {"-c", "exec sleep 60"};
    service.startup_configs.push_back(service_config);
    manifest.apps.push_back(service);
    exec::AppSpec client_app;
    client_app.app = "client";
    client_app.instance = "1";
    client_app.command = "/bin/sh";
    client_app.function_group = "ready_group";
    client_app.reporting = false;
    exec::StateDependentStartupConfig client_config;
    client_config.state = "Run";
    client_config.args = {"-c", "exec sleep 60"};
    client_config.dependencies.push_back(
        exec::ExecutionDependency{"service", exec::ProcessState::Running});
    client_app.startup_configs.push_back(client_config);
    manifest.apps.push_back(client_app);

    exec::FunctionGroupRegistry registry(manifest);
    exec::ExecutionStateTracker tracker;
    exec::ExecutionManager manager;
    app::RunIdentity identity{"test", run_dir, "inst1", {}};
    auto state_client =
        exec::StateClient::create(*node, *name, registry, manager, identity, &tracker);
    NX_CHECK(state_client.has_value());
    auto reporter = exec::ApplicationStateReporter::create(*node, "service");
    NX_CHECK(reporter.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    auto request = [&](const char* target) {
        std::thread loop([&] {
            for (int i = 0; i < 100; ++i) {
                auto served = state_client->serve_once();
                if (!served || !*served) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
        const auto response =
            client->call(make_command("ready_group", target), std::chrono::milliseconds(2000));
        loop.join();
        return response;
    };

    const auto started = request("Run");
    NX_CHECK(started.has_value() && started->result == exec::StateResult::InProgress);
    NX_CHECK(!app_running(manager, run_dir, "client"));
    NX_CHECK(reporter->report(exec::AppState::Running).has_value());
    for (int i = 0; i < 50 && tracker.state("service") != exec::AppState::Running; ++i) {
        const auto served = state_client->serve_once();
        NX_CHECK(served.has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto confirmed = request("Run");
    NX_CHECK(confirmed.has_value() && confirmed->result == exec::StateResult::Success);
    NX_CHECK(app_running(manager, run_dir, "client"));

    std::filesystem::remove_all(run_dir);
}

void test_daemon_exit_detection() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateDaemonExit");
    NX_CHECK(name.has_value());
    const std::string run_dir = "/tmp/opencode/state_client_daemon_exit";
    std::filesystem::create_directories(run_dir);

    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "daemon_group";
    group.states = {"Off", "Run"};
    manifest.function_groups.push_back(group);
    exec::AppSpec app;
    app.app = "quick_daemon";
    app.instance = "1";
    app.command = "/bin/true";
    app.function_group = "daemon_group";
    app.reporting = false;
    exec::StateDependentStartupConfig config;
    config.state = "Run";
    app.startup_configs.push_back(config);
    manifest.apps.push_back(app);

    exec::FunctionGroupRegistry registry(manifest);
    exec::ExecutionStateTracker tracker;
    exec::ExecutionManager manager;
    app::RunIdentity identity{"test", run_dir, "inst1", {}};
    auto state_client =
        exec::StateClient::create(*node, *name, registry, manager, identity, &tracker);
    NX_CHECK(state_client.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    auto request = [&](const char* target) {
        std::thread loop([&] {
            for (int i = 0; i < 100; ++i) {
                auto served = state_client->serve_once();
                if (!served || !*served) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
        const auto response =
            client->call(make_command("daemon_group", target), std::chrono::milliseconds(2000));
        loop.join();
        return response;
    };

    const auto started = request("Run");
    NX_CHECK(started.has_value() && started->result == exec::StateResult::Success);
    for (int i = 0; i < 50 && tracker.state("quick_daemon") != exec::AppState::Failed; ++i) {
        const auto served = state_client->serve_once();
        NX_CHECK(served.has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    NX_CHECK(tracker.state("quick_daemon") == exec::AppState::Failed);
    NX_CHECK(tracker.process_state("quick_daemon") == exec::ProcessState::Terminated);

    std::filesystem::remove_all(run_dir);
}

void test_termination_timeout() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateTerminationTimeout");
    NX_CHECK(name.has_value());
    const std::string run_dir = "/tmp/opencode/state_client_termination_timeout";
    std::filesystem::create_directories(run_dir);

    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "stubborn_group";
    group.states = {"Off", "Run"};
    manifest.function_groups.push_back(group);
    exec::AppSpec app;
    app.app = "stubborn";
    app.instance = "1";
    app.command = "/bin/sh";
    app.function_group = "stubborn_group";
    app.reporting = false;
    exec::StateDependentStartupConfig config;
    config.state = "Run";
    config.args = {"-c", "trap '' TERM; exec sleep 60"};
    config.termination_timeout_ms = 1000;
    app.startup_configs.push_back(config);
    manifest.apps.push_back(app);

    exec::FunctionGroupRegistry registry(manifest);
    exec::ExecutionStateTracker tracker;
    exec::ExecutionManager manager;
    app::RunIdentity identity{"test", run_dir, "inst1", {}};
    auto state_client =
        exec::StateClient::create(*node, *name, registry, manager, identity, &tracker);
    NX_CHECK(state_client.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    auto request = [&](const char* target) {
        std::thread loop([&] {
            for (int i = 0; i < 400; ++i) {
                auto served = state_client->serve_once();
                if (!served || !*served) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
        const auto response =
            client->call(make_command("stubborn_group", target), std::chrono::milliseconds(5000));
        loop.join();
        return response;
    };

    const auto started = request("Run");
    NX_CHECK(started.has_value() && started->result == exec::StateResult::Success);
    const auto stopped = request("Off");
    NX_CHECK(stopped.has_value() && stopped->result == exec::StateResult::Success);
    NX_CHECK(registry.group_state(0) == "Off");
    NX_CHECK(!app_running(manager, run_dir, "stubborn"));

    std::filesystem::remove_all(run_dir);
}

void test_transition_timeout() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateTimeout");
    NX_CHECK(name.has_value());
    const std::string run_dir = "/tmp/opencode/state_client_timeout";
    std::filesystem::create_directories(run_dir);

    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "timeout_group";
    group.states = {"Off", "Run"};
    manifest.function_groups.push_back(group);
    exec::AppSpec app;
    app.app = "never_ready";
    app.instance = "1";
    app.command = "/bin/sh";
    app.function_group = "timeout_group";
    app.reporting = true;
    exec::StateDependentStartupConfig config;
    config.state = "Run";
    config.args = {"-c", "exec sleep 60"};
    config.startup_timeout_ms = 10;
    config.restart_attempts = 1;
    app.startup_configs.push_back(config);
    manifest.apps.push_back(app);

    exec::FunctionGroupRegistry registry(manifest);
    exec::ExecutionStateTracker tracker;
    exec::ExecutionManager manager;
    app::RunIdentity identity{"test", run_dir, "inst1", {}};
    auto state_client =
        exec::StateClient::create(*node, *name, registry, manager, identity, &tracker);
    NX_CHECK(state_client.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    auto request = [&](const char* target) {
        std::thread loop([&] {
            for (int i = 0; i < 100; ++i) {
                auto served = state_client->serve_once();
                if (!served || !*served) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
        const auto response =
            client->call(make_command("timeout_group", target), std::chrono::milliseconds(2000));
        loop.join();
        return response;
    };

    const auto started = request("Run");
    NX_CHECK(started.has_value() && started->result == exec::StateResult::InProgress);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto retried = request("Run");
    NX_CHECK(retried.has_value() && retried->result == exec::StateResult::InProgress);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto failed = request("Run");
    NX_CHECK(failed.has_value() && failed->result == exec::StateResult::OperationFailed);
    NX_CHECK(registry.group_state(0) == "Undefined");

    std::filesystem::remove_all(run_dir);
}

void test_reverse_termination_order() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateReverseOrder");
    NX_CHECK(name.has_value());
    const std::string run_dir = "/tmp/opencode/state_client_reverse_order";
    const std::string marker = run_dir + "/order.log";
    std::filesystem::remove_all(run_dir);
    std::filesystem::create_directories(run_dir);

    auto trap_script = [&](const char* role) {
        return std::string("trap 'echo ") + role + " >> " + marker +
               "; exit 0' TERM; while :; do sleep 0.05; done";
    };

    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "order_group";
    group.states = {"Off", "Run"};
    manifest.function_groups.push_back(group);
    exec::AppSpec provider;
    provider.app = "provider";
    provider.instance = "1";
    provider.command = "/bin/sh";
    provider.function_group = "order_group";
    provider.reporting = false;
    exec::StateDependentStartupConfig provider_config;
    provider_config.state = "Run";
    provider_config.args = {"-c", trap_script("provider")};
    provider.startup_configs.push_back(provider_config);
    manifest.apps.push_back(provider);

    exec::AppSpec consumer;
    consumer.app = "consumer";
    consumer.instance = "1";
    consumer.command = "/bin/sh";
    consumer.function_group = "order_group";
    consumer.reporting = false;
    exec::StateDependentStartupConfig consumer_config;
    consumer_config.state = "Run";
    consumer_config.args = {"-c", trap_script("consumer")};
    consumer_config.dependencies.push_back(
        exec::ExecutionDependency{"provider", exec::ProcessState::Running});
    consumer.startup_configs.push_back(consumer_config);
    manifest.apps.push_back(consumer);

    exec::FunctionGroupRegistry registry(manifest);
    exec::ExecutionStateTracker tracker;
    exec::ExecutionManager manager;
    app::RunIdentity identity{"test", run_dir, "inst1", {}};
    auto state_client =
        exec::StateClient::create(*node, *name, registry, manager, identity, &tracker);
    NX_CHECK(state_client.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    auto request = [&](const char* target) {
        std::thread loop([&] {
            for (int i = 0; i < 100; ++i) {
                auto served = state_client->serve_once();
                if (!served || !*served) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
        const auto response =
            client->call(make_command("order_group", target), std::chrono::milliseconds(2000));
        loop.join();
        return response;
    };

    const auto started = request("Run");
    NX_CHECK(started.has_value() && started->result == exec::StateResult::Success);
    NX_CHECK(app_running(manager, run_dir, "provider"));
    NX_CHECK(app_running(manager, run_dir, "consumer"));
    const auto stopped = request("Off");
    NX_CHECK(stopped.has_value() && stopped->result == exec::StateResult::Success);

    const std::string order = read_text_file(marker);
    NX_CHECK(order.find("consumer") < order.find("provider"));

    std::filesystem::remove_all(run_dir);
}

void test_transition_cancellation() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateCancel");
    NX_CHECK(name.has_value());
    const std::string run_dir = "/tmp/opencode/state_client_cancel";
    std::filesystem::create_directories(run_dir);

    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "cancel_group";
    group.states = {"Off", "Run", "Parking"};
    manifest.function_groups.push_back(group);
    exec::AppSpec app;
    app.app = "blocking_app";
    app.instance = "1";
    app.command = "/bin/sh";
    app.function_group = "cancel_group";
    exec::StateDependentStartupConfig config;
    config.state = "Run";
    config.args = {"-c", "exec sleep 60"};
    app.startup_configs.push_back(config);
    manifest.apps.push_back(app);

    exec::FunctionGroupRegistry registry(manifest);
    exec::ExecutionStateTracker tracker;
    exec::ExecutionManager manager;
    app::RunIdentity identity{"test", run_dir, "inst1", {}};
    auto state_client =
        exec::StateClient::create(*node, *name, registry, manager, identity, &tracker);
    NX_CHECK(state_client.has_value());
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    auto request = [&](const char* target) {
        std::thread loop([&] {
            for (int i = 0; i < 100; ++i) {
                auto served = state_client->serve_once();
                if (!served || !*served) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
        const auto response =
            client->call(make_command("cancel_group", target), std::chrono::milliseconds(2000));
        loop.join();
        return response;
    };

    const auto running = request("Run");
    NX_CHECK(running.has_value() && running->result == exec::StateResult::InProgress);
    const auto parking = request("Parking");
    NX_CHECK(parking.has_value() && parking->result == exec::StateResult::Success);
    NX_CHECK(registry.group_state(0) == "Parking");
    NX_CHECK(!app_running(manager, run_dir, "blocking_app"));

    std::filesystem::remove_all(run_dir);
}

void test_process_handlers() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateHandlers");
    NX_CHECK(name.has_value());
    const std::string run_dir = "/tmp/opencode/state_client_handlers";
    std::filesystem::create_directories(run_dir);

    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "handler_group";
    group.states = {"Off", "Run"};
    manifest.function_groups.push_back(group);
    exec::AppSpec app;
    app.app = "handler_app";
    app.instance = "node7";
    app.command = "/bin/true";
    app.function_group = "handler_group";
    app.reporting = false;
    exec::StateDependentStartupConfig config;
    config.state = "Run";
    app.startup_configs.push_back(config);
    manifest.apps.push_back(app);

    exec::FunctionGroupRegistry registry(manifest);
    NX_CHECK(registry.instance_of("handler_app") == "node7");
    exec::ExecutionStateTracker tracker;
    exec::ExecutionManager manager;
    app::RunIdentity identity{"test", run_dir, "generic", {}};
    auto state_client =
        exec::StateClient::create(*node, *name, registry, manager, identity, &tracker);
    NX_CHECK(state_client.has_value());

    std::vector<std::string> started;
    std::vector<std::string> stopped;
    state_client->set_process_handlers(
        [&started](std::string_view app_name, std::string_view) -> core::Status {
            started.emplace_back(app_name);
            return {};
        },
        [&stopped](std::string_view app_name, std::string_view) -> core::Status {
            stopped.emplace_back(app_name);
            return {};
        });

    NX_CHECK(state_client->request_state("handler_group", "Run") == exec::StateResult::Success);
    NX_CHECK(started.size() == 1U && started.front() == "handler_app");
    NX_CHECK(registry.group_state(0) == "Run");

    NX_CHECK(state_client->request_state("handler_group", "Off") == exec::StateResult::Success);
    NX_CHECK(stopped.size() == 1U && stopped.front() == "handler_app");
    NX_CHECK(registry.group_state(0) == "Off");

    std::filesystem::remove_all(run_dir);
}

void test_instance_directory_uses_manifest_instance() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateInstanceDir");
    NX_CHECK(name.has_value());
    const std::string run_dir = "/tmp/opencode/state_client_instance_dir";
    std::filesystem::remove_all(run_dir);
    std::filesystem::create_directories(run_dir);

    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "instance_group";
    group.states = {"Off", "Run"};
    manifest.function_groups.push_back(group);
    exec::AppSpec app;
    app.app = "instance_app";
    app.instance = "node7";
    app.command = "/bin/true";
    app.function_group = "instance_group";
    app.reporting = false;
    exec::StateDependentStartupConfig config;
    config.state = "Run";
    app.startup_configs.push_back(config);
    manifest.apps.push_back(app);

    exec::FunctionGroupRegistry registry(manifest);
    exec::ExecutionStateTracker tracker;
    exec::ExecutionManager manager;
    // The run identity's own instance id must not override the manifest one.
    app::RunIdentity identity{"test", run_dir, "generic", {}};
    auto state_client =
        exec::StateClient::create(*node, *name, registry, manager, identity, &tracker);
    NX_CHECK(state_client.has_value());

    NX_CHECK(state_client->request_state("instance_group", "Run") == exec::StateResult::Success);
    NX_CHECK(std::filesystem::exists(run_dir + "/instance_app/node7/resource.json"));
    NX_CHECK(!std::filesystem::exists(run_dir + "/instance_app/generic/resource.json"));

    std::filesystem::remove_all(run_dir);
}

} // namespace

int main() {
    test_state_client();
    test_run_confirmation();
    test_state_transitions();
    test_transition_undefined();
    test_transition_timeout();
    test_transition_cancellation();
    test_dependency_order();
    test_readiness_dependency();
    test_reverse_termination_order();
    test_daemon_exit_detection();
    test_termination_timeout();
    test_process_handlers();
    test_instance_directory_uses_manifest_instance();
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
