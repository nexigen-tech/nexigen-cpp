// Тесты nexigen::exec::StateClient (change add-sm; spec exec/state-client).
// Сборка только при NEXIGEN_ENABLE_COM=ON. Mock SM — com::Client, шлющий
// SetState; реальный ExecutionManager исполняет команды во временном run-dir.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <string>
#include <thread>
#include <vector>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/com/client.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_client.hpp>
#include <nexigen/exec/state_command.hpp>

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

void test_state_client() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/StateCommand");
    NX_CHECK(name.has_value());

    // Mock SM client + real ExecutionManager over a temporary run directory.
    auto client = com::Client<exec::StateCommand, exec::StateAck>::create(*node, *name);
    NX_CHECK(client.has_value());

    exec::ExecutionManager manager;
    const std::string run_dir = "/tmp/opencode/state_client_run";
    std::filesystem::create_directories(run_dir);
    app::RunIdentity identity;
    identity.run_id = "test";
    identity.run_dir = run_dir;
    identity.instance_id = "inst1";

    std::vector<exec::StateClient::GroupMapping> mappings;
    exec::StateClient::GroupMapping mapping;
    mapping.function_group = "motion_control";
    mapping.app_names = {"true_app"};
    mapping.binary = "/bin/true";
    mappings.push_back(mapping);

    auto state_client =
        exec::StateClient::create(*node, *name, std::move(mappings), manager, identity);
    NX_CHECK(state_client.has_value());

    // Serve the loop in a thread so the mock SM can call synchronously.
    std::atomic<bool> stop{false};
    std::thread server_loop([&] {
        while (!stop.load()) {
            auto served = state_client->serve_once();
            if (!served || !*served) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    });

    // Unmapped group: rejection ack.
    {
        const auto response =
            client->call(make_command("unknown_group", "Off"), std::chrono::milliseconds(2000));
        NX_CHECK(response.has_value());
        NX_CHECK(response->accepted == 0U);
    }

    // Mapped group, Off target: stop is idempotent for a not-yet-started
    // instance layout the EM manages — the command is processed (ack reflects
    // the EM's verdict, either way the group is NOT rejected as unknown).
    {
        const auto response =
            client->call(make_command("motion_control", "Off"), std::chrono::milliseconds(2000));
        NX_CHECK(response.has_value());
    }

    // Mapped group, start target: the EM launches /bin/true in the run dir.
    {
        const auto response =
            client->call(make_command("motion_control", "Active"), std::chrono::milliseconds(2000));
        NX_CHECK(response.has_value());
        NX_CHECK(response->accepted == 1U);
    }

    stop.store(true);
    server_loop.join();

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
