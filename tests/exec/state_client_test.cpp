// Тесты nexigen::exec::StateClient (change add-sm; add-em-orchestration adds
// Function Groups + SafeState priority). Сборка только при NEXIGEN_ENABLE_COM=ON.
// Mock SM — com::Client, шлющий SetState; реальный ExecutionManager исполняет
// команды во временном run-dir.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/com/client.hpp>
#include <nexigen/com/node.hpp>
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
    group.states = {"Active", "Off", "SafeState"};
    manifest.function_groups.push_back(group);
    exec::AppSpec app;
    app.app = "true_app";
    app.command = "/bin/true";
    app.function_groups = {"motion_control"};
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
    NX_CHECK(rtext.find("motion_control=Active") != std::string::npos);
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

} // namespace

int main() {
    test_state_client();
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
