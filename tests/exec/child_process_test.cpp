// nexigen::exec tests — ChildProcess lifecycle wrapper.
// Same zero-dependency NX_CHECK harness as the other execution_manager tests.
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/exec/app_spec.hpp>
#include <nexigen/exec/app_state.hpp>
#include <nexigen/exec/child_process.hpp>

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

void run_suite(const char* name, void (*body)()) {
    const int before_checks = g_checks;
    const int before_failures = g_failures;
    body();
    std::printf("[%-22s] %d checks, %d failed\n", name, g_checks - before_checks,
                g_failures - before_failures);
}

exec::AppSpec make_spec() {
    exec::AppSpec spec{};
    spec.app = "alpha";
    spec.instance = "1";
    spec.command = "/bin/true";
    return spec;
}

void test_construction() {
    exec::ChildProcess child(make_spec());
    NX_CHECK(child.spec.app == "alpha");
    NX_CHECK(child.spec.instance == "1");
    NX_CHECK(child.state == exec::AppState::Unknown);
    NX_CHECK(child.attempt == 0);
    NX_CHECK(child.restarts_used == 0);
    NX_CHECK(child.child_pid == 0);
    NX_CHECK(child.supervisor_pid == 0);
    NX_CHECK(child.next_restart_ns == 0);
    NX_CHECK(!child.is_running());
    NX_CHECK(!child.exit_consumed);
    NX_CHECK(!child.launch_failed);
}

void test_is_running() {
    exec::ChildProcess child(make_spec());
    child.state = exec::AppState::Running;
    NX_CHECK(child.is_running());
    child.state = exec::AppState::Restarting;
    NX_CHECK(child.is_running());
    child.state = exec::AppState::Exited;
    NX_CHECK(!child.is_running());
    child.state = exec::AppState::Stopped;
    NX_CHECK(!child.is_running());
    child.state = exec::AppState::Failed;
    NX_CHECK(!child.is_running());
}

void test_to_string_and_format() {
    exec::ChildProcess child(make_spec());
    child.child_pid = 42;
    child.supervisor_pid = 43;
    child.state = exec::AppState::Running;
    child.attempt = 3;
    child.restarts_used = 2;

    const std::string text = child.to_string();
    NX_CHECK(text.find("alpha") != std::string::npos);
    NX_CHECK(text.find("42") != std::string::npos);
    NX_CHECK(text.find("running") != std::string::npos);

    const std::string adl = exec::to_string(child);
    NX_CHECK(adl == text);

    const std::string formatted = std::format("{}", child);
    NX_CHECK(formatted == text);
}

void test_json_roundtrip() {
    exec::ChildProcess child(make_spec());
    child.child_pid = 7;
    child.supervisor_pid = 8;
    child.state = exec::AppState::Restarting;
    child.attempt = 2;
    child.restarts_used = 1;

    nlohmann::json document;
    exec::to_json(document, child);
    NX_CHECK(document.is_object());
    NX_CHECK(document["app"] == "alpha");
    NX_CHECK(document["instance"] == "1");
    NX_CHECK(document["pid"] == 7);
    NX_CHECK(document["supervisor_pid"] == 8);
    NX_CHECK(document["state"] == "restarting");
    NX_CHECK(document["attempt"] == 2);
    NX_CHECK(document["restarts_used"] == 1);

    exec::ChildProcess round(make_spec());
    exec::from_json(document, round);
    NX_CHECK(round.spec.app == "alpha");
    NX_CHECK(round.spec.instance == "1");
    NX_CHECK(round.child_pid == 7);
    NX_CHECK(round.supervisor_pid == 8);
    NX_CHECK(round.state == exec::AppState::Restarting);
    NX_CHECK(round.attempt == 2);
    NX_CHECK(round.restarts_used == 1);
}

void test_json_hostile_input() {
    exec::ChildProcess child(make_spec());
    child.child_pid = 42;
    child.state = exec::AppState::Failed;

    // Non-object input leaves the destination untouched.
    exec::from_json(nlohmann::json::array(), child);
    NX_CHECK(child.spec.app == "alpha");
    NX_CHECK(child.child_pid == 42);
    NX_CHECK(child.state == exec::AppState::Failed);

    // Wrong-typed fields leave the destination untouched.
    exec::from_json(nlohmann::json{{"app", 1}, {"pid", "x"}, {"state", 5}}, child);
    NX_CHECK(child.spec.app == "alpha");
    NX_CHECK(child.child_pid == 42);
    NX_CHECK(child.state == exec::AppState::Failed);

    // Unknown state name leaves the state untouched.
    exec::from_json(nlohmann::json{{"state", "chatty"}}, child);
    NX_CHECK(child.state == exec::AppState::Failed);
}

} // namespace

int main() {
    run_suite("construction", test_construction);
    run_suite("is_running", test_is_running);
    run_suite("to_string_and_format", test_to_string_and_format);
    run_suite("json_roundtrip", test_json_roundtrip);
    run_suite("json_hostile_input", test_json_hostile_input);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
