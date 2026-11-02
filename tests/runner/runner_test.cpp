// nexigen::runner tests — instance lifecycle and process supervision.
// Same zero-dependency NX_CHECK harness as the other modules.
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "../support/test_temp.hpp"

#include <nexigen/core/result.hpp>
#include <nexigen/core/detail/fault_injection.hpp>
#include <nexigen/runner/instance_lifecycle.hpp>
#include <nexigen/runner/runner.hpp>

namespace core = nexigen::core;
namespace runner = nexigen::runner;
namespace app = nexigen::app;
namespace logging = nexigen::logging;

namespace {

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(...)                                                                           \
    do {                                                                                        \
        ++g_checks;                                                                             \
        if (!(__VA_ARGS__)) {                                                                   \
            ++g_failures;                                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);        \
        }                                                                                       \
    } while (false)

void run_suite(const char* name, void (*body)()) {
    const int before_checks = g_checks;
    const int before_failures = g_failures;
    body();
    std::printf("[%-22s] %d checks, %d failed\n", name, g_checks - before_checks,
                g_failures - before_failures);
}

std::string unique_dir(std::string_view tag) {
    static int counter = 0;
    ++counter;
    return "/tmp/opencode/nexigen_runner_" + std::string(tag) + "_" +
           std::to_string(static_cast<long long>(::getpid())) + "_" +
           std::to_string(counter);
}

void remove_tree(const std::string& path) {
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(path, error));
}

std::string read_text(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

/// True when an atomic-write scratch file (`*.tmp.<pid>`) was left behind.
bool has_temporary_files(const std::string& path) {
    std::error_code error;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(path, error)) {
        if (entry.path().filename().string().find(".tmp.") !=
            std::string::npos) {
            return true;
        }
    }
    return false;
}

template <class T>
bool failed_with(const core::Result<T>& result, core::ErrorCode expected) {
    return !result.has_value() && result.error() == expected;
}

bool failed_with(const core::Status& result, core::ErrorCode expected) {
    return !result.has_value() && result.error() == expected;
}

template <class Predicate>
bool wait_until(Predicate predicate, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

void reap_supervisors() {
    int status = 0;
    while (::waitpid(-1, &status, WNOHANG) > 0) {
    }
}

runner::ResourceMetadata read_resource(const std::string& directory) {
    runner::ResourceMetadata metadata{};
    const auto document =
        runner::read_json_file(directory + "/resource.json");
    if (document) {
        runner::from_json(*document, metadata);
    }
    return metadata;
}

runner::ExitMetadata read_exit(const std::string& directory) {
    runner::ExitMetadata metadata{};
    const auto document = runner::read_json_file(directory + "/exit.json");
    if (document) {
        runner::from_json(*document, metadata);
    }
    return metadata;
}

constexpr std::string_view k_test_app = "test_app";

app::RunIdentity identity_for(const std::string& root) {
    return app::RunIdentity{
        "run-1", root, "instance-1", root + "/test_app/instance-1"};
}

std::optional<std::uint32_t> first_allowed_cpu() {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (::sched_getaffinity(0, sizeof(mask), &mask) != 0) {
        return std::nullopt;
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &mask) != 0) {
            return static_cast<std::uint32_t>(cpu);
        }
    }
#pragma GCC diagnostic pop
    return std::nullopt;
}

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
core::detail::FaultPoint g_fault_point =
    core::detail::FaultPoint::Runner_pipe2;
bool g_fault_enabled = false;

bool test_fault_hook(core::detail::FaultPoint point) noexcept {
    return g_fault_enabled && point == g_fault_point;
}
#endif

// --- Compile-time contracts ---
template <class T, class = void>
struct has_from_json : std::false_type {};
template <class T>
struct has_from_json<
    T, std::void_t<decltype(from_json(std::declval<const nlohmann::json&>(),
                                      std::declval<T&>()))>> : std::true_type {};

template <class T>
constexpr bool is_plain_value_type =
    std::is_copy_constructible_v<T> && std::is_move_constructible_v<T> &&
    std::is_copy_assignable_v<T> && std::is_move_assignable_v<T> &&
    std::is_nothrow_destructible_v<T> && !std::is_polymorphic_v<T>;

static_assert(has_from_json<runner::ResourceMetadata>::value);
static_assert(has_from_json<runner::ExitMetadata>::value);
static_assert(has_from_json<runner::TerminatedBy>::value);
static_assert(is_plain_value_type<runner::ResourceMetadata>);
static_assert(is_plain_value_type<runner::ExitMetadata>);
static_assert(std::is_trivially_copyable_v<runner::TerminatedBy>);

// --- TerminatedBy ---
void test_terminated_by() {
    for (const auto value : {runner::TerminatedBy::Self,
                             runner::TerminatedBy::Runner,
                             runner::TerminatedBy::Unknown}) {
        const std::string_view text = runner::to_string(value);
        runner::TerminatedBy parsed = static_cast<runner::TerminatedBy>(0xff);
        NX_CHECK(runner::from_string(text, parsed));
        NX_CHECK(parsed == value);
    }
    NX_CHECK(runner::to_string(static_cast<runner::TerminatedBy>(0x7f)) ==
             "Unknown");

    nlohmann::json json;
    runner::to_json(json, runner::TerminatedBy::Runner);
    runner::TerminatedBy parsed = runner::TerminatedBy::Unknown;
    runner::from_json(json, parsed);
    NX_CHECK(parsed == runner::TerminatedBy::Runner);

    runner::from_json(nlohmann::json::array(), parsed);
    NX_CHECK(parsed == runner::TerminatedBy::Runner);

    NX_CHECK(std::format("{}", runner::TerminatedBy::Self) == "self");
}

// --- ResourceMetadata ---
void test_resource_metadata() {
    const app::RunIdentity identity{
        "run-1", "/runs/run-1", "instance-1",
        "/runs/run-1/app/instance-1"};
    const runner::ResourceMetadata metadata =
        runner::ResourceMetadata::from_identity(identity, "app", 12345, 67890);

    NX_CHECK(metadata.run_id == "run-1");
    NX_CHECK(metadata.app_name == "app");
    NX_CHECK(metadata.instance_id == "instance-1");
    NX_CHECK(metadata.instance_dir == "/runs/run-1/app/instance-1");
    NX_CHECK(metadata.pid == 12345);
    NX_CHECK(metadata.ppid == 67890);
    NX_CHECK(metadata.version == "nexigen-cpp");
    NX_CHECK(metadata.start_time_rfc3339.size() == 30);
    NX_CHECK(metadata.start_time_rfc3339.back() == 'Z');
    NX_CHECK(metadata.start_time_mono_ns > 0ULL);

    nlohmann::json json;
    runner::to_json(json, metadata);
    NX_CHECK(json["ppid"] == 67890);

    runner::ResourceMetadata parsed{};
    runner::from_json(json, parsed);
    NX_CHECK(parsed.run_id == metadata.run_id);
    NX_CHECK(parsed.app_name == metadata.app_name);
    NX_CHECK(parsed.instance_id == metadata.instance_id);
    NX_CHECK(parsed.instance_dir == metadata.instance_dir);
    NX_CHECK(parsed.pid == metadata.pid);
    NX_CHECK(parsed.ppid == metadata.ppid);
    NX_CHECK(parsed.start_time_rfc3339 == metadata.start_time_rfc3339);
    NX_CHECK(parsed.start_time_mono_ns == metadata.start_time_mono_ns);
    NX_CHECK(parsed.version == metadata.version);

    NX_CHECK(std::format("{}", metadata) == metadata.to_string());

    // Hostile JSON: arrays, wrong types, and extra fields change nothing.
    runner::from_json(nlohmann::json::array(), parsed);
    NX_CHECK(parsed.run_id == metadata.run_id);
    runner::from_json(
        nlohmann::json{{"pid", "bad"}, {"ppid", nullptr}, {"extra", true}},
        parsed);
    NX_CHECK(parsed.pid == metadata.pid);
    NX_CHECK(parsed.ppid == metadata.ppid);
    runner::from_json(
        nlohmann::json{{"pid", std::int64_t{1} << 40}}, parsed);
    NX_CHECK(parsed.pid == metadata.pid);
}

// --- ExitMetadata ---
void test_exit_metadata() {
    const runner::ExitMetadata normal = runner::ExitMetadata::from_child_status(
        100, 0, 42, runner::TerminatedBy::Self);
    NX_CHECK(normal.exit_code.has_value());
    NX_CHECK(*normal.exit_code == 0);
    NX_CHECK(!normal.signal.has_value());
    NX_CHECK(normal.uptime_s == 42ULL);
    NX_CHECK(normal.terminated_by == runner::TerminatedBy::Self);

    nlohmann::json json;
    runner::to_json(json, normal);
    NX_CHECK(json["exit_code"] == 0);
    NX_CHECK(json["signal"].is_null());

    runner::ExitMetadata parsed{};
    runner::from_json(json, parsed);
    NX_CHECK(parsed.exit_code.has_value());
    NX_CHECK(*parsed.exit_code == 0);
    NX_CHECK(!parsed.signal.has_value());
    NX_CHECK(parsed.uptime_s == 42ULL);
    NX_CHECK(parsed.terminated_by == runner::TerminatedBy::Self);

    const runner::ExitMetadata killed = runner::ExitMetadata::from_child_status(
        100, SIGKILL, 3, runner::TerminatedBy::Runner);
    NX_CHECK(!killed.exit_code.has_value());
    NX_CHECK(killed.signal.has_value());
    NX_CHECK(*killed.signal == SIGKILL);
    NX_CHECK(killed.terminated_by == runner::TerminatedBy::Runner);

    nlohmann::json killed_json;
    runner::to_json(killed_json, killed);
    NX_CHECK(killed_json["exit_code"].is_null());
    NX_CHECK(killed_json["signal"] == SIGKILL);
    NX_CHECK(killed_json["terminated_by"] == "runner");

    NX_CHECK(std::format("{}", killed) == killed.to_string());

    // Hostile input leaves the target untouched.
    const runner::ExitMetadata before = parsed;
    runner::from_json(nlohmann::json::array(), parsed);
    NX_CHECK(parsed.uptime_s == before.uptime_s);
    runner::from_json(nlohmann::json{{"exit_code", "bad"},
                                     {"signal", false},
                                     {"uptime_s", "bad"}},
                      parsed);
    NX_CHECK(parsed.exit_code == before.exit_code);
    NX_CHECK(parsed.signal == before.signal);
    NX_CHECK(parsed.uptime_s == before.uptime_s);
    runner::from_json(
        nlohmann::json{{"exit_code", std::int64_t{1} << 40},
                       {"signal", std::int64_t{1} << 40}},
        parsed);
    NX_CHECK(parsed.exit_code == before.exit_code);
    NX_CHECK(parsed.signal == before.signal);
    runner::from_json(nlohmann::json{{"signal", 15}}, parsed);
    NX_CHECK(parsed.signal.has_value());
    NX_CHECK(*parsed.signal == 15);
}

// --- Directory creation ---
void test_instance_dir_creation() {
    const std::string root = unique_dir("dir");
    const std::string nested = root + "/a/b/c";
    NX_CHECK(runner::create_instance_dir(nested).has_value());

    struct stat status {};
    NX_CHECK(::stat((nested + "/settings").c_str(), &status) == 0);
    NX_CHECK(S_ISDIR(status.st_mode));

    // Second creation is idempotent.
    NX_CHECK(runner::create_instance_dir(nested).has_value());

    // Relative paths and over-long paths are rejected before any syscall.
    NX_CHECK(failed_with(runner::create_instance_dir("relative/path"),
                         core::ErrorCode::InvalidArgument));
    const std::string too_long = "/tmp/" + std::string(600, 'x');
    NX_CHECK(failed_with(runner::create_instance_dir(too_long),
                         core::ErrorCode::InvalidArgument));

    // A permission failure surfaces as SystemError.
    const std::string denied =
        "/proc/nexigen_runner_denied_" +
        std::to_string(static_cast<long long>(::getpid()));
    NX_CHECK(failed_with(runner::create_instance_dir(denied),
                         core::ErrorCode::SystemError));

    // An existing non-directory component is rejected by the mkdir walk.
    const std::string component = root + "/component-file";
    {
        std::ofstream output(component, std::ios::binary | std::ios::trunc);
        output << "x";
    }
    NX_CHECK(failed_with(runner::create_instance_dir(component + "/child"),
                         core::ErrorCode::SystemError));

    remove_tree(root);
}

// --- Settings generation ---
void test_generate_settings() {
    const std::string directory = unique_dir("settings");
    NX_CHECK(runner::create_instance_dir(directory).has_value());

    app::TelemetryPaths resolved{
        directory + "/custom-metrics.jsonl",
        "/etc/traces.jsonl",
        "",
    };
    NX_CHECK(runner::generate_settings(directory, resolved).has_value());

    for (const auto* name :
         {"logging.json", "metrics.json", "tracing.json", "chrono.json"}) {
        const std::string path =
            directory + "/settings/" + std::string(name);
        struct stat status {};
        NX_CHECK(::stat(path.c_str(), &status) == 0);
        NX_CHECK(status.st_size > 0);
        const auto document = runner::read_json_file(path);
        NX_CHECK(document.has_value());
        if (document) {
            NX_CHECK(document->is_object());
        }
    }

    const auto logging = runner::read_json_file(directory +
                                                "/settings/logging.json");
    NX_CHECK(logging.has_value());
    if (logging) {
        NX_CHECK((*logging)["log_root_directory"] == directory);
        NX_CHECK((*logging)["sink"]["path"] == directory + "/app.jsonl");
    }

    // Inside the instance directory: resolved path is kept verbatim.
    const auto metrics = runner::read_json_file(directory +
                                                "/settings/metrics.json");
    NX_CHECK(metrics.has_value());
    if (metrics) {
        NX_CHECK((*metrics)["sink"]["path"] ==
                 directory + "/custom-metrics.jsonl");
    }

    // Outside the instance directory: deterministic fallback is used.
    const auto tracing = runner::read_json_file(directory +
                                                "/settings/tracing.json");
    NX_CHECK(tracing.has_value());
    if (tracing) {
        NX_CHECK((*tracing)["sink"]["path"] == directory + "/traces.jsonl");
    }

    const auto chrono = runner::read_json_file(directory +
                                               "/settings/chrono.json");
    NX_CHECK(chrono.has_value());
    if (chrono) {
        NX_CHECK((*chrono)["sink"]["path"] == directory + "/chrono.jsonl");
    }

    remove_tree(directory);
}

// --- JSON file I/O ---
void test_json_file_io() {
    const std::string directory = unique_dir("json");
    NX_CHECK(runner::create_instance_dir(directory).has_value());
    const std::string path = directory + "/sample.json";

    nlohmann::json document{{"a", 1}, {"b", "hello"}};
    NX_CHECK(runner::write_json_file(path, document).has_value());

    const auto parsed = runner::read_json_file(path);
    NX_CHECK(parsed.has_value());
    if (parsed) {
        NX_CHECK((*parsed)["a"] == 1);
        NX_CHECK((*parsed)["b"] == "hello");
    }

    const std::string invalid_path = directory + "/invalid.json";
    {
        std::ofstream output(invalid_path, std::ios::binary | std::ios::trunc);
        output << "{not json";
    }
    NX_CHECK(failed_with(runner::read_json_file(invalid_path),
                         core::ErrorCode::InvalidArgument));
    NX_CHECK(failed_with(runner::read_json_file(directory + "/missing.json"),
                         core::ErrorCode::SystemError));
    NX_CHECK(failed_with(runner::read_json_file(directory),
                         core::ErrorCode::SystemError));

    // Write failures are reported and keep the error contract.
    NX_CHECK(failed_with(runner::write_json_file(
                             directory + "/missing/state.json", document),
                         core::ErrorCode::SystemError));

    const std::string missing = unique_dir("json_missing");
    NX_CHECK(failed_with(runner::write_resource_json(missing, {}),
                         core::ErrorCode::SystemError));
    NX_CHECK(failed_with(runner::write_exit_json(missing, {}),
                         core::ErrorCode::SystemError));

    NX_CHECK(failed_with(runner::create_instance_dir(""),
                         core::ErrorCode::InvalidArgument));
    NX_CHECK(failed_with(runner::create_instance_dir("relative"),
                         core::ErrorCode::InvalidArgument));
    NX_CHECK(failed_with(runner::create_artifact_dir(""),
                         core::ErrorCode::InvalidArgument));
    NX_CHECK(failed_with(
        runner::create_artifact_dir("/proc/nexigen_artifact_denied"),
        core::ErrorCode::SystemError));
    NX_CHECK(failed_with(runner::create_instance_dir("/proc/self"),
                         core::ErrorCode::SystemError));
    NX_CHECK(failed_with(runner::generate_settings("relative", {}),
                         core::ErrorCode::InvalidArgument));
    NX_CHECK(failed_with(
        runner::generate_settings("/proc/nexigen_settings_denied", {}),
        core::ErrorCode::SystemError));

    for (const char* blocked : {"metrics.json", "tracing.json", "chrono.json"}) {
        const std::string settings_root =
            unique_dir(std::string("settings_") + blocked);
        std::error_code mkdir_error;
        std::filesystem::create_directories(
            settings_root + "/settings/" + blocked, mkdir_error);
        NX_CHECK(failed_with(runner::generate_settings(settings_root, {}),
                             core::ErrorCode::SystemError));
        remove_tree(settings_root);
    }

    const app::RunIdentity identity{
        "run-1", directory, "instance-1", directory};
    const runner::ResourceMetadata resource =
        runner::ResourceMetadata::from_identity(identity, "app", 4242, 4343);
    NX_CHECK(runner::write_resource_json(directory, resource).has_value());
    const auto resource_document =
        runner::read_json_file(directory + "/resource.json");
    NX_CHECK(resource_document.has_value());
    if (resource_document) {
        runner::ResourceMetadata parsed_resource{};
        runner::from_json(*resource_document, parsed_resource);
        NX_CHECK(parsed_resource.pid == 4242);
        NX_CHECK(parsed_resource.ppid == 4343);
        NX_CHECK(parsed_resource.instance_dir == directory);
    }

    runner::ExitMetadata exit_metadata;
    exit_metadata.exit_code = 0;
    exit_metadata.uptime_s = 9;
    exit_metadata.terminated_by = runner::TerminatedBy::Self;
    NX_CHECK(runner::write_exit_json(directory, exit_metadata).has_value());
    const auto exit_document =
        runner::read_json_file(directory + "/exit.json");
    NX_CHECK(exit_document.has_value());
    if (exit_document) {
        runner::ExitMetadata parsed_exit{};
        runner::from_json(*exit_document, parsed_exit);
        NX_CHECK(parsed_exit.exit_code.has_value());
        NX_CHECK(*parsed_exit.exit_code == 0);
        NX_CHECK(parsed_exit.uptime_s == 9ULL);
        NX_CHECK(parsed_exit.terminated_by == runner::TerminatedBy::Self);
    }

    remove_tree(directory);
}

// --- Start / status / stop ---
void test_start_status_stop() {
    const std::string root = unique_dir("lifecycle");
    const app::RunIdentity identity = identity_for(root);
    const std::vector<std::string> command{
        "/bin/sh", "-c",
        "echo hello-out; echo hello-err >&2; exec sleep 60"};

    runner::Runner runner;
    const core::Status started = runner.start(identity, k_test_app, command, 0);
    NX_CHECK(started.has_value());
    if (!started) {
        remove_tree(root);
        reap_supervisors();
        return;
    }

    std::string state;
    NX_CHECK(runner.status(identity.instance_dir, state).has_value());
    NX_CHECK(state == "running");

    // A second start on a live instance is rejected without touching it.
    NX_CHECK(failed_with(runner.start(identity, k_test_app, command, 0),
                         core::ErrorCode::Conflict));

    const runner::ResourceMetadata metadata =
        read_resource(identity.instance_dir);
    NX_CHECK(metadata.pid > 0);
    NX_CHECK(metadata.ppid > 0);
    NX_CHECK(metadata.app_name == "test_app");
    NX_CHECK(metadata.instance_dir == identity.instance_dir);
    NX_CHECK(::getsid(static_cast<pid_t>(metadata.pid)) !=
             ::getsid(::getpid()));

    for (const auto* name :
         {"logging.json", "metrics.json", "tracing.json", "chrono.json"}) {
        struct stat status_info {};
        NX_CHECK(::stat((identity.instance_dir + "/settings/" + name).c_str(),
                        &status_info) == 0);
    }
    for (const auto* name :
         {"app.jsonl", "metrics.jsonl", "traces.jsonl", "chrono.jsonl"}) {
        struct stat status_info {};
        NX_CHECK(::stat((identity.instance_dir + "/" + name).c_str(),
                        &status_info) == 0);
    }

    NX_CHECK(wait_until(
        [&] {
            return contains(read_text(identity.instance_dir + "/stdout.log"),
                            "hello-out");
        },
        3000));
    NX_CHECK(wait_until(
        [&] {
            return contains(read_text(identity.instance_dir + "/stderr.log"),
                            "hello-err");
        },
        3000));

    const core::Status stopped = runner.stop(identity.instance_dir, 2);
    NX_CHECK(stopped.has_value());

    NX_CHECK(runner.status(identity.instance_dir, state).has_value());
    NX_CHECK(state == "stopped");

    const runner::ExitMetadata exit_metadata =
        read_exit(identity.instance_dir);
    NX_CHECK(exit_metadata.terminated_by == runner::TerminatedBy::Runner);
    NX_CHECK(exit_metadata.signal.has_value());
    NX_CHECK(*exit_metadata.signal == SIGTERM);
    NX_CHECK(!exit_metadata.exit_code.has_value());

    // Idempotent double stop: no second signal, still successful.
    NX_CHECK(runner.stop(identity.instance_dir, 0).has_value());
    NX_CHECK(::kill(static_cast<pid_t>(metadata.pid), 0) != 0);

    // Atomic writes must not leave scratch files behind.
    NX_CHECK(!has_temporary_files(identity.instance_dir));

    reap_supervisors();
    remove_tree(root);
}

// --- Self exit ---
void test_self_exit_status() {
    const std::string root = unique_dir("selfexit");
    const app::RunIdentity identity = identity_for(root);
    const std::vector<std::string> command{
        "/bin/sh", "-c", "sleep 1; exit 7"};

    runner::Runner runner;
    const core::Status started = runner.start(identity, k_test_app, command, 0);
    NX_CHECK(started.has_value());
    if (!started) {
        remove_tree(root);
        reap_supervisors();
        return;
    }

    NX_CHECK(wait_until(
        [&] {
            struct stat status {};
            return ::stat((identity.instance_dir + "/exit.json").c_str(),
                          &status) == 0;
        },
        5000));

    std::string state;
    NX_CHECK(runner.status(identity.instance_dir, state).has_value());
    NX_CHECK(state == "exited");

    const runner::ExitMetadata exit_metadata =
        read_exit(identity.instance_dir);
    NX_CHECK(exit_metadata.exit_code.has_value());
    NX_CHECK(*exit_metadata.exit_code == 7);
    NX_CHECK(!exit_metadata.signal.has_value());
    NX_CHECK(exit_metadata.uptime_s >= 1);
    NX_CHECK(exit_metadata.terminated_by == runner::TerminatedBy::Self);

    reap_supervisors();
    remove_tree(root);
}

// --- Identity supplied through the environment contract ---
void test_env_identity_layout() {
    const std::string root = unique_dir("env");
    NX_CHECK(::setenv("NEXIGEN_RUN_ID", "run-env", 1) == 0);
    NX_CHECK(::setenv("NEXIGEN_RUN_DIR", root.c_str(), 1) == 0);
    NX_CHECK(::setenv("NEXIGEN_INSTANCE_ID", "env_instance", 1) == 0);
    NX_CHECK(::unsetenv("NEXIGEN_INSTANCE_DIR") == 0);

    const app::RunIdentity identity = app::load_run_identity();
    NX_CHECK(identity.run_id == "run-env");
    NX_CHECK(identity.run_dir == root);
    NX_CHECK(identity.instance_id == "env_instance");
    NX_CHECK(identity.instance_dir.empty());

    const std::vector<std::string> command{"/bin/true"};
    runner::Runner runner;
    const core::Status started = runner.start(identity, "env_app", command, 0);
    NX_CHECK(started.has_value());

    const std::string instance_dir = root + "/env_app/env_instance";
    if (started) {
        for (const auto* name : {"logging.json", "metrics.json", "tracing.json",
                                 "chrono.json"}) {
            struct stat status_info {};
            NX_CHECK(::stat((instance_dir + "/settings/" + name).c_str(),
                            &status_info) == 0);
        }
        struct stat resource_status {};
        NX_CHECK(::stat((instance_dir + "/resource.json").c_str(),
                        &resource_status) == 0);
        NX_CHECK(wait_until(
            [&] {
                struct stat status {};
                return ::stat((instance_dir + "/exit.json").c_str(),
                              &status) == 0;
            },
            5000));
        NX_CHECK(read_resource(instance_dir).pid > 0);
    }

    static_cast<void>(::unsetenv("NEXIGEN_RUN_ID"));
    static_cast<void>(::unsetenv("NEXIGEN_RUN_DIR"));
    static_cast<void>(::unsetenv("NEXIGEN_INSTANCE_ID"));

    reap_supervisors();
    remove_tree(root);
}

// --- Restarting an instance truncates the captured logs ---
void test_log_truncation_on_restart() {
    const std::string root = unique_dir("restart");
    const app::RunIdentity identity = identity_for(root);
    runner::Runner runner;

    const std::vector<std::string> first{
        "/bin/sh", "-c", "echo first-run; exec sleep 60"};
    NX_CHECK(runner.start(identity, k_test_app, first, 0).has_value());
    NX_CHECK(wait_until(
        [&] {
            return contains(read_text(identity.instance_dir + "/stdout.log"),
                            "first-run");
        },
        3000));
    NX_CHECK(runner.stop(identity.instance_dir, 2).has_value());

    const std::vector<std::string> second{
        "/bin/sh", "-c", "echo second-run; exec sleep 60"};
    NX_CHECK(runner.start(identity, k_test_app, second, 0).has_value());
    NX_CHECK(wait_until(
        [&] {
            return contains(read_text(identity.instance_dir + "/stdout.log"),
                            "second-run");
        },
        3000));

    const std::string stdout_text =
        read_text(identity.instance_dir + "/stdout.log");
    NX_CHECK(contains(stdout_text, "second-run"));
    NX_CHECK(!contains(stdout_text, "first-run"));

    NX_CHECK(runner.stop(identity.instance_dir, 2).has_value());

    reap_supervisors();
    remove_tree(root);
}

// --- Graceful SIGTERM keeps the child's exit code ---
void test_graceful_sigterm() {
    const std::string root = unique_dir("graceful");
    const app::RunIdentity identity = identity_for(root);
    const std::vector<std::string> command{
        "/bin/sh", "-c",
        "trap 'exit 0' TERM; echo trap-ready; while :; do sleep 1; done"};

    runner::Runner runner;
    const core::Status started = runner.start(identity, k_test_app, command, 0);
    NX_CHECK(started.has_value());
    if (!started) {
        remove_tree(root);
        reap_supervisors();
        return;
    }

    // Wait for the trap to be installed before signalling.
    NX_CHECK(wait_until(
        [&] {
            return contains(read_text(identity.instance_dir + "/stdout.log"),
                            "trap-ready");
        },
        3000));

    NX_CHECK(runner.stop(identity.instance_dir, 5).has_value());

    std::string state;
    NX_CHECK(runner.status(identity.instance_dir, state).has_value());
    NX_CHECK(state == "stopped");

    const runner::ExitMetadata exit_metadata =
        read_exit(identity.instance_dir);
    NX_CHECK(exit_metadata.exit_code.has_value());
    NX_CHECK(*exit_metadata.exit_code == 0);
    NX_CHECK(!exit_metadata.signal.has_value());
    NX_CHECK(exit_metadata.terminated_by == runner::TerminatedBy::Runner);

    reap_supervisors();
    remove_tree(root);
}

// --- A killed runner session leaves the supervised child running ---
void test_runner_session_isolation() {
    const std::string root = unique_dir("session");
    const app::RunIdentity identity = identity_for(root);
    const std::vector<std::string> command{
        "/bin/sh", "-c", "exec sleep 60"};

    runner::Runner runner;
    int report_pipe[2]{-1, -1};
    NX_CHECK(::pipe(report_pipe) == 0);

    const pid_t launcher = ::fork();
    NX_CHECK(launcher >= 0);
    if (launcher == 0) {
        // Run `start` from a disposable session, then wait to be killed.
        static_cast<void>(::close(report_pipe[0]));
        static_cast<void>(::setsid());
        runner::Runner launcher_runner;
        const bool ok =
            launcher_runner.start(identity, k_test_app, command, 0).has_value();
        const char report = ok ? 1 : 0;
        const ssize_t written =
            ::write(report_pipe[1], &report, sizeof(report));
        static_cast<void>(written);
        static_cast<void>(::close(report_pipe[1]));
        if (!ok) {
            _exit(1);
        }
        for (;;) {
            ::pause();
        }
    }

    static_cast<void>(::close(report_pipe[1]));
    char report = 0;
    const ssize_t received =
        ::read(report_pipe[0], &report, sizeof(report));
    static_cast<void>(received);
    static_cast<void>(::close(report_pipe[0]));

    NX_CHECK(report == 1);
    if (report == 1) {
        // Kill the whole session that performed the start.
        NX_CHECK(::kill(-launcher, SIGKILL) == 0);
        int launcher_status = 0;
        while (::waitpid(launcher, &launcher_status, 0) < 0 &&
               errno == EINTR) {
        }

        std::string state;
        NX_CHECK(runner.status(identity.instance_dir, state).has_value());
        NX_CHECK(state == "running");

        const runner::ResourceMetadata metadata =
            read_resource(identity.instance_dir);
        NX_CHECK(metadata.pid > 0);
        NX_CHECK(::kill(static_cast<pid_t>(metadata.pid), 0) == 0);

        NX_CHECK(runner.stop(identity.instance_dir, 0).has_value());
    } else {
        int launcher_status = 0;
        while (::waitpid(launcher, &launcher_status, 0) < 0 &&
               errno == EINTR) {
        }
    }

    reap_supervisors();
    remove_tree(root);
}

// --- Idle timeout ---
void test_idle_timeout() {
    const std::string root = unique_dir("timeout");
    const app::RunIdentity identity = identity_for(root);
    const std::vector<std::string> command{"/bin/sh", "-c", "exec sleep 60"};

    runner::Runner runner;
    const core::Status started = runner.start(identity, k_test_app, command, 1);
    NX_CHECK(started.has_value());
    if (!started) {
        remove_tree(root);
        reap_supervisors();
        return;
    }

    NX_CHECK(wait_until(
        [&] {
            struct stat status {};
            return ::stat((identity.instance_dir + "/exit.json").c_str(),
                          &status) == 0;
        },
        10000));

    std::string state;
    NX_CHECK(runner.status(identity.instance_dir, state).has_value());
    NX_CHECK(state == "stopped");

    const runner::ExitMetadata exit_metadata =
        read_exit(identity.instance_dir);
    NX_CHECK(exit_metadata.terminated_by == runner::TerminatedBy::Runner);
    NX_CHECK(exit_metadata.signal.has_value());
    NX_CHECK(*exit_metadata.signal == SIGTERM);

    reap_supervisors();
    remove_tree(root);
}

// --- SIGKILL after an unresponsive grace period ---
void test_sigkill_after_grace() {
    const std::string root = unique_dir("sigkill");
    const app::RunIdentity identity = identity_for(root);
    const std::vector<std::string> command{
        "/bin/sh", "-c", "trap '' TERM; exec sleep 60"};

    runner::Runner runner;
    const core::Status started = runner.start(identity, k_test_app, command, 0);
    NX_CHECK(started.has_value());
    if (!started) {
        remove_tree(root);
        reap_supervisors();
        return;
    }

    const runner::ResourceMetadata metadata =
        read_resource(identity.instance_dir);
    NX_CHECK(metadata.pid > 0);

    // Let the shell install the ignored-SIGTERM disposition before stopping.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    const core::Status stopped = runner.stop(identity.instance_dir, 0);
    NX_CHECK(stopped.has_value());

    const runner::ExitMetadata exit_metadata =
        read_exit(identity.instance_dir);
    NX_CHECK(exit_metadata.terminated_by == runner::TerminatedBy::Runner);
    NX_CHECK(exit_metadata.signal.has_value());
    NX_CHECK(*exit_metadata.signal == SIGKILL);
    NX_CHECK(!exit_metadata.exit_code.has_value());

    std::string state;
    NX_CHECK(runner.status(identity.instance_dir, state).has_value());
    NX_CHECK(state == "stopped");

    reap_supervisors();
    remove_tree(root);
}

// --- Error and unknown paths ---
// --- Start options validation and the suite-style settings path ---
void test_start_options() {
    const std::string root = unique_dir("options");
    const std::string artifacts = unique_dir("options_artifacts");
    const std::string settings = unique_dir("options_settings");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    std::filesystem::create_directories(settings, error);
    const app::RunIdentity identity = identity_for(root);
    const std::vector<std::string> command{"/bin/true"};
    runner::Runner runner;

    auto invalid = [&](const runner::StartOptions& options) {
        return failed_with(runner.start(identity, k_test_app, command, 0, options),
                           core::ErrorCode::InvalidArgument);
    };

    runner::StartOptions bad;
    bad.artifact_dir = "relative/artifacts";
    NX_CHECK(invalid(bad));
    bad = {};
    bad.artifact_dir = "/" + std::string(600, 'a');
    NX_CHECK(invalid(bad));
    bad = {};
    bad.global_settings_dir = "relative";
    NX_CHECK(invalid(bad));
    bad = {};
    bad.local_settings_dir = "relative";
    NX_CHECK(invalid(bad));
    bad = {};
    bad.env = {{"", "value"}};
    NX_CHECK(invalid(bad));
    bad = {};
    bad.env = {{"A=B", "value"}};
    NX_CHECK(invalid(bad));
    bad = {};
    bad.env = {{std::string("A\0B", 3), "value"}};
    NX_CHECK(invalid(bad));
    bad = {};
    bad.cpus = {99999};
    NX_CHECK(invalid(bad));
    if (const auto cpu = first_allowed_cpu(); cpu.has_value()) {
        bad = {};
        bad.cpus = {*cpu, *cpu};
        NX_CHECK(invalid(bad));
    }

    runner::StartOptions options;
    options.artifact_dir = artifacts;
    options.global_settings_dir = settings;
    options.local_settings_dir = settings;
    options.log_level = logging::LogLevel::Warning;
    options.env = {{"NEXIGEN_TEST_OPTION", "yes"}};
    if (const auto cpu = first_allowed_cpu(); cpu.has_value()) {
        options.cpus = {*cpu};
    }
    NX_CHECK(!options.to_string().empty());
    const std::vector<std::string> child_command{
        "/bin/sh", "-c", "echo \"option=$NEXIGEN_TEST_OPTION\"; exec sleep 60"};
    NX_CHECK(runner.start(identity, k_test_app, child_command, 0, options).has_value());
    NX_CHECK(wait_until(
        [&] {
            return contains(read_text(artifacts + "/stdout.log"),
                            "option=yes");
        },
        3000));
    NX_CHECK(runner.stop(identity.instance_dir, 2).has_value());
    for (const auto* name : {"logging.json", "metrics.json", "tracing.json",
                             "chrono.json"}) {
        struct stat status_info {};
        NX_CHECK(::stat((identity.instance_dir + "/settings/" + name).c_str(),
                        &status_info) == 0);
    }

    reap_supervisors();
    remove_tree(root);
    remove_tree(artifacts);
    remove_tree(settings);
}

// --- Stop and status edge conditions ---
void test_lifecycle_edges() {
    runner::Runner runner;
    std::string state;

    NX_CHECK(failed_with(runner.stop("", 0), core::ErrorCode::InvalidArgument));
    NX_CHECK(failed_with(runner.status("", state),
                         core::ErrorCode::InvalidArgument));

    const std::string root = unique_dir("edges");
    NX_CHECK(runner::create_instance_dir(root).has_value());

    // resource.json with a non-positive PID is rejected by stop and reported
    // as unknown by status.
    runner::ResourceMetadata invalid_resource{};
    invalid_resource.pid = 0;
    NX_CHECK(runner::write_resource_json(root, invalid_resource).has_value());
    NX_CHECK(failed_with(runner.stop(root, 0),
                         core::ErrorCode::InvalidArgument));
    NX_CHECK(runner.status(root, state).has_value());
    NX_CHECK(state == "unknown");

    // A dead PID without exit.json becomes exited, and stop writes the
    // fallback completion record.
    runner::ResourceMetadata dead_resource{};
    dead_resource.pid = 999999;
    dead_resource.start_time_mono_ns = 1;
    NX_CHECK(runner::write_resource_json(root, dead_resource).has_value());
    NX_CHECK(runner.status(root, state).has_value());
    NX_CHECK(state == "exited");
    NX_CHECK(runner.stop(root, 0).has_value());
    const runner::ExitMetadata fallback = read_exit(root);
    NX_CHECK(fallback.terminated_by == runner::TerminatedBy::Unknown);

    // An exit record with a missing resource directory is a successful no-op.
    const std::string completed = unique_dir("edges_completed");
    NX_CHECK(runner::create_instance_dir(completed).has_value());
    runner::ExitMetadata exit_metadata{};
    exit_metadata.exit_code = 0;
    exit_metadata.terminated_by = runner::TerminatedBy::Self;
    NX_CHECK(runner::write_exit_json(completed, exit_metadata).has_value());
    NX_CHECK(runner.stop(completed, 0).has_value());

    // A completed record whose process group is still alive is a conflict.
    const pid_t live_group = ::fork();
    if (live_group == 0) {
        static_cast<void>(::setpgid(0, 0));
        ::execl("/bin/sleep", "sleep", "60", nullptr);
        _exit(127);
    }
    NX_CHECK(live_group > 0);
    if (live_group > 0) {
        static_cast<void>(::setpgid(live_group, live_group));
        runner::ResourceMetadata alive{};
        alive.pid = static_cast<std::int32_t>(live_group);
        NX_CHECK(runner::write_resource_json(root, alive).has_value());
        NX_CHECK(runner::write_exit_json(root, exit_metadata).has_value());
        NX_CHECK(failed_with(runner.stop(root, 0),
                             core::ErrorCode::SystemError));
        static_cast<void>(::kill(-live_group, SIGKILL));
        int status = 0;
        static_cast<void>(::waitpid(live_group, &status, 0));
    }

    reap_supervisors();
    remove_tree(root);
    remove_tree(completed);
}

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
void test_fault_injection() {
    const std::string root = unique_dir("faults");
    const app::RunIdentity identity = identity_for(root);
    const std::vector<std::string> command{"/bin/true"};

    core::detail::set_fault_hook(test_fault_hook);
    runner::Runner launcher;
    auto run_fault = [&](core::detail::FaultPoint point) {
        g_fault_point = point;
        g_fault_enabled = true;
        const core::Status status = launcher.start(identity, k_test_app, command, 0);
        g_fault_enabled = false;
        return status;
    };

    NX_CHECK(failed_with(run_fault(core::detail::FaultPoint::Runner_pipe2),
                         core::ErrorCode::SystemError));
    NX_CHECK(failed_with(run_fault(core::detail::FaultPoint::Runner_fork),
                         core::ErrorCode::SystemError));
    NX_CHECK(failed_with(run_fault(core::detail::FaultPoint::Runner_open_null),
                         core::ErrorCode::SystemError));
    core::detail::set_fault_hook(nullptr);

    reap_supervisors();
    remove_tree(root);
}
#endif

void test_error_paths() {
    runner::Runner runner;

    std::string state;
    NX_CHECK(runner.status("/tmp/opencode/nexigen_runner_missing", state)
                 .has_value());
    NX_CHECK(state == "unknown");

    const core::Status stop_missing =
        runner.stop("/tmp/opencode/nexigen_runner_missing", 0);
    NX_CHECK(failed_with(stop_missing, core::ErrorCode::SystemError));

    // Empty command.
    const std::string root = unique_dir("errors");
    app::RunIdentity identity = identity_for(root);
    const std::vector<std::string> empty_command;
    NX_CHECK(failed_with(runner.start(identity, k_test_app, empty_command, 0),
                         core::ErrorCode::InvalidArgument));

    // Empty identity cannot derive an instance directory.
    const app::RunIdentity empty_identity{};
    const std::vector<std::string> command{"/bin/true"};
    NX_CHECK(failed_with(runner.start(empty_identity, "", command, 0),
                         core::ErrorCode::InvalidArgument));

    // Relative instance directory.
    app::RunIdentity relative = identity_for(root);
    relative.instance_dir = "relative/path";
    NX_CHECK(failed_with(runner.start(relative, k_test_app, command, 0),
                         core::ErrorCode::InvalidArgument));

    // Unwritable parent.
    app::RunIdentity denied = identity_for(root);
    denied.instance_dir =
        "/proc/nexigen_runner_denied_" +
        std::to_string(static_cast<long long>(::getpid())) + "/instance";
    NX_CHECK(failed_with(runner.start(denied, k_test_app, command, 0),
                         core::ErrorCode::SystemError));

    // Identity derived from the three components when instance_dir is empty.
    app::RunIdentity derived{"run-2", root, "derived", ""};
    const core::Status derived_start = runner.start(derived, k_test_app, command, 0);
    NX_CHECK(derived_start.has_value());
    if (derived_start) {
        const std::string target = root + "/test_app/derived";
        NX_CHECK(runner.status(target, state).has_value());
        NX_CHECK(wait_until(
            [&] {
                struct stat status {};
                return ::stat((target + "/exit.json").c_str(), &status) == 0;
            },
            5000));
        NX_CHECK(runner.status(target, state).has_value());
        NX_CHECK(state == "exited");
    }

    // Trailing slashes are normalized before the layout is created.
    app::RunIdentity padded = identity_for(root);
    padded.instance_dir = root + "/test_app/instance-padded///";
    NX_CHECK(runner.start(padded, k_test_app, command, 0).has_value());

    // An instance directory beyond the path budget is rejected.
    app::RunIdentity too_long = identity_for(root);
    too_long.instance_dir = "/" + std::string(600, 'x');
    NX_CHECK(failed_with(runner.start(too_long, k_test_app, command, 0),
                         core::ErrorCode::InvalidArgument));

    const std::string task_root = unique_dir("errors_io");
    NX_CHECK(runner::create_instance_dir(task_root + "/test_app/instance-1")
                 .has_value());
    std::error_code io_error;
    std::filesystem::create_directories(
        task_root + "/test_app/instance-1/stdout.log", io_error);
    app::RunIdentity stdout_blocked{
        "run-1", task_root, "instance-1",
        task_root + "/test_app/instance-1"};
    NX_CHECK(failed_with(runner.start(stdout_blocked, k_test_app, command, 0),
                         core::ErrorCode::SystemError));

    const std::string stderr_root = unique_dir("errors_io2");
    NX_CHECK(runner::create_instance_dir(stderr_root + "/test_app/instance-1")
                 .has_value());
    std::filesystem::create_directories(
        stderr_root + "/test_app/instance-1/stderr.log", io_error);
    app::RunIdentity stderr_blocked{
        "run-1", stderr_root, "instance-1",
        stderr_root + "/test_app/instance-1"};
    NX_CHECK(failed_with(runner.start(stderr_blocked, k_test_app, command, 0),
                         core::ErrorCode::SystemError));

    // A stream file that cannot be created fails the preparation phase.
    const std::string stream_root = unique_dir("errors_stream");
    NX_CHECK(runner::create_instance_dir(stream_root + "/test_app/instance-1")
                 .has_value());
    std::filesystem::create_directories(
        stream_root + "/test_app/instance-1/app.jsonl", io_error);
    app::RunIdentity stream_blocked{
        "run-1", stream_root, "instance-1",
        stream_root + "/test_app/instance-1"};
    NX_CHECK(failed_with(runner.start(stream_blocked, k_test_app, command, 0),
                         core::ErrorCode::SystemError));

    reap_supervisors();
    remove_tree(root);
    remove_tree(task_root);
    remove_tree(stderr_root);
    remove_tree(stream_root);
}

} // namespace

int main() {
    run_suite("terminated_by", test_terminated_by);
    run_suite("resource_metadata", test_resource_metadata);
    run_suite("exit_metadata", test_exit_metadata);
    run_suite("instance_dir", test_instance_dir_creation);
    run_suite("generate_settings", test_generate_settings);
    run_suite("json_file_io", test_json_file_io);
    run_suite("start_status_stop", test_start_status_stop);
    run_suite("self_exit_status", test_self_exit_status);
    run_suite("env_identity_layout", test_env_identity_layout);
    run_suite("log_truncation", test_log_truncation_on_restart);
    run_suite("graceful_sigterm", test_graceful_sigterm);
    run_suite("session_isolation", test_runner_session_isolation);
    run_suite("idle_timeout", test_idle_timeout);
    run_suite("sigkill_after_grace", test_sigkill_after_grace);
    run_suite("start_options", test_start_options);
    run_suite("lifecycle_edges", test_lifecycle_edges);
    run_suite("error_paths", test_error_paths);
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    run_suite("fault_injection", test_fault_injection);
#endif

    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
