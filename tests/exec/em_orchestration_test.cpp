// Тесты оркестрации EM (change add-em-orchestration): readiness reporting,
// манифестные поля (dependencies/function_groups), FunctionGroupRegistry,
// crash-cleanup (перезапуск после kill -9 без AlreadyExists). Сборка только
// при NEXIGEN_ENABLE_COM=ON.
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/exec/app_spec.hpp>
#include <nexigen/exec/application_state.hpp>
#include <nexigen/exec/function_group_registry.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace com = nexigen::com;
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

struct PodPayload {
    std::int32_t x;
    std::int32_t y;
};

void test_readiness_reporting() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const std::string service = exec::application_state_service("camera");
    const auto name = com::ServiceName::create(service.c_str());
    NX_CHECK(name.has_value());
    auto listener = com::EventListener::create(*node, *name);
    NX_CHECK(listener.has_value());

    auto reporter = exec::ApplicationStateReporter::create(*node, "camera");
    NX_CHECK(reporter.has_value());
    NX_CHECK(reporter->report(exec::AppState::Running).has_value());
    NX_CHECK(reporter->last_state() == exec::AppState::Running);

    bool observed = false;
    const auto collected = listener->try_wait([&](com::EventId id) {
        observed = id.as_value() == static_cast<std::uint64_t>(exec::AppState::Running);
    });
    NX_CHECK(collected.has_value());
    NX_CHECK(observed);
    NX_CHECK(reporter->to_string().find("camera") != std::string::npos);
    NX_CHECK(std::format("{}", *reporter) == reporter->to_string());
}

void test_manifest_fields() {
    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "motion_control";
    group.states = {"Active", "Off", "SafeState"};
    manifest.function_groups.push_back(group);
    exec::AppSpec camera;
    camera.app = "camera";
    camera.command = "/bin/camera";
    camera.dependencies = {"telemetry"};
    camera.function_groups = {"motion_control"};
    manifest.apps.push_back(camera);

    nlohmann::json j;
    to_json(j, manifest);
    exec::SuiteManifest back;
    from_json(j, back);
    NX_CHECK(back.function_groups.size() == 1U);
    NX_CHECK(back.function_groups[0].name == "motion_control");
    NX_CHECK(back.function_groups[0].states.size() == 3U);
    NX_CHECK(back.apps.size() == 1U);
    NX_CHECK(back.apps[0].dependencies.size() == 1U);
    NX_CHECK(back.apps[0].dependencies[0] == "telemetry");
    NX_CHECK(back.apps[0].function_groups.size() == 1U);
    NX_CHECK(back.apps[0].function_groups[0] == "motion_control");
}

void test_function_group_registry() {
    exec::SuiteManifest manifest;
    exec::FunctionGroupConfig group;
    group.name = "motion_control";
    group.states = {"Active", "Off", "SafeState"};
    manifest.function_groups.push_back(group);
    exec::AppSpec camera;
    camera.app = "camera";
    camera.command = "/bin/camera";
    camera.function_groups = {"motion_control"};
    manifest.apps.push_back(camera);
    exec::AppSpec unrelated;
    unrelated.app = "infotainment";
    unrelated.command = "/bin/info";
    manifest.apps.push_back(unrelated);

    exec::FunctionGroupRegistry registry(manifest);
    NX_CHECK(registry.group_count() == 1U);
    NX_CHECK(registry.group_name(0) == "motion_control");
    NX_CHECK(registry.group_state(0) == "Active");
    NX_CHECK(registry.bound_apps(0).size() == 1U);
    NX_CHECK(registry.bound_apps(0)[0] == "camera");
    NX_CHECK(registry.command_of("camera") == "/bin/camera");
    NX_CHECK(registry.command_of("infotainment") == "/bin/info");
    NX_CHECK(registry.find_group("motion_control") == 0U);
    NX_CHECK(registry.find_group("unknown") == static_cast<std::size_t>(-1));
    NX_CHECK(exec::FunctionGroupRegistry::is_off_target("Off"));
    NX_CHECK(exec::FunctionGroupRegistry::is_off_target("Shutdown"));
    NX_CHECK(!exec::FunctionGroupRegistry::is_off_target("Active"));

    registry.set_state(0, "SafeState");
    NX_CHECK(registry.group_state(0) == "SafeState");
    const std::string text = registry.to_string();
    NX_CHECK(text.find("motion_control=SafeState") != std::string::npos);
    NX_CHECK(std::format("{}", registry) == text);
}

void test_crash_cleanup() {
    // REQ_EM_014 item 3: a killed process's stale services are reclaimed; a
    // relaunched process re-registers the same identifiers without
    // AlreadyExists. The child holds a publisher; after kill -9 the parent
    // re-creates the same service.
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/CrashCleanup");
    NX_CHECK(name.has_value());

    const pid_t child = fork();
    NX_CHECK(child >= 0);
    if (child == 0) {
        auto child_node = com::Node::create();
        auto publisher = com::Publisher<PodPayload>::create(*child_node, *name);
        // Hold the service open until killed.
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    ::kill(child, SIGKILL);
    int status = 0;
    waitpid(child, &status, 0);

    // The relaunch registers the same identifiers without AlreadyExists.
    auto publisher = com::Publisher<PodPayload>::create(*node, *name);
    NX_CHECK(publisher.has_value());
}

} // namespace

int main() {
    test_readiness_reporting();
    test_manifest_fields();
    test_function_group_registry();
    test_crash_cleanup();
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
