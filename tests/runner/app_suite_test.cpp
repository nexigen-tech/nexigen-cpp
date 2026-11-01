// nexigen::runner suite tests — manifest, policy, artifacts, and CLI-facing state.
#include <chrono>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <sched.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "../support/test_temp.hpp"

#include <nexigen/core/result.hpp>
#include <nexigen/runner/app_suite.hpp>
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
    std::printf("[%-24s] %d checks, %d failed\n", name, g_checks - before_checks,
                g_failures - before_failures);
}

std::string unique_dir(std::string_view tag) {
    static int counter = 0;
    ++counter;
    return "/tmp/opencode/nexigen_suite_" + std::string(tag) + "_" +
           std::to_string(static_cast<long long>(::getpid())) + "_" +
           std::to_string(counter);
}

void remove_tree(const std::string& path) {
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(path, error));
}

bool file_exists(const std::string& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
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

bool cpu_is_set(int cpu, const cpu_set_t& mask) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
    const bool set = CPU_ISSET(cpu, &mask) != 0;
#pragma GCC diagnostic pop
    return set;
}

int first_allowed_cpu() {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (::sched_getaffinity(0, sizeof(mask), &mask) != 0) {
        return -1;
    }
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (cpu_is_set(cpu, mask)) {
            return cpu;
        }
    }
    return -1;
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

bool failed_with(const core::Status& result, core::ErrorCode expected) {
    return !result.has_value() && result.error() == expected;
}

runner::App_spec app_spec(std::string app_name, std::string instance,
                          std::string command,
                          runner::Impact_on_app_suite_operability impact) {
    runner::App_spec spec{};
    spec.app = std::move(app_name);
    spec.instance = std::move(instance);
    spec.command = std::move(command);
    spec.impact = impact;
    return spec;
}

runner::Suite_manifest base_manifest(const std::string& root,
                                     const std::string& artifacts) {
    runner::Suite_manifest manifest{};
    manifest.version = 1;
    manifest.suite_id = "suite";
    manifest.run_id = "run-1";
    manifest.run_dir = root;
    manifest.artifacts_path = artifacts;
    manifest.manifest_path = root + "/manifest.json";
    manifest.suite_restart_attempts = 0;
    manifest.suite_restart_delay_s = 0;
    return manifest;
}

runner::Suite_manifest two_app_manifest(const std::string& root,
                                        const std::string& artifacts) {
    runner::Suite_manifest manifest = base_manifest(root, artifacts);
    runner::App_spec alpha =
        app_spec("alpha", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Degrade);
    alpha.args = {"-c", "echo alpha-out; exec sleep 60"};
    runner::App_spec beta =
        app_spec("beta", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Not_impact);
    beta.args = {"-c", "echo beta-out; exec sleep 60"};
    manifest.apps.push_back(std::move(alpha));
    manifest.apps.push_back(std::move(beta));
    return manifest;
}

bool write_manifest(const std::string& path, const runner::Suite_manifest& manifest) {
    nlohmann::json document;
    runner::to_json(document, manifest);
    return runner::write_json_file(path, document).has_value();
}

runner::Suite_status read_status(const std::string& suite_dir) {
    runner::Suite_status status{};
    static_cast<void>(runner::suite_status(suite_dir, status));
    return status;
}

runner::Suite_metadata read_metadata(const std::string& suite_dir) {
    runner::Suite_metadata metadata{};
    const core::Result<nlohmann::json> document =
        runner::read_json_file(suite_dir + "/suite.json");
    if (document) {
        runner::from_json(*document, metadata);
    }
    return metadata;
}

runner::Suite_app_status find_app(const runner::Suite_status& status,
                                  std::string_view app_name) {
    for (const runner::Suite_app_status& app : status.apps) {
        if (app.app == app_name) {
            return app;
        }
    }
    return {};
}

void test_enums_and_serialization() {
    for (const auto value : {runner::Impact_on_app_suite_operability::Failure,
                             runner::Impact_on_app_suite_operability::Degrade,
                             runner::Impact_on_app_suite_operability::Not_impact}) {
        const std::string_view text = runner::to_string(value);
        runner::Impact_on_app_suite_operability parsed =
            static_cast<runner::Impact_on_app_suite_operability>(0xff);
        NX_CHECK(runner::from_string(text, parsed));
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == text);
        nlohmann::json json;
        runner::to_json(json, value);
        runner::from_json(json, parsed);
        NX_CHECK(parsed == value);
    }
    NX_CHECK(runner::to_string(
                 static_cast<runner::Impact_on_app_suite_operability>(0x7f)) ==
             "unknown");

    for (const auto value : {runner::Suite_state::Running,
                             runner::Suite_state::Degraded,
                             runner::Suite_state::Failed,
                             runner::Suite_state::Stopped,
                             runner::Suite_state::Unknown}) {
        runner::Suite_state parsed = static_cast<runner::Suite_state>(0xff);
        NX_CHECK(runner::from_string(runner::to_string(value), parsed));
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == runner::to_string(value));
        nlohmann::json state_json;
        runner::to_json(state_json, value);
        NX_CHECK(state_json.is_string());
    }
    for (const auto value : {runner::App_state::Running,
                             runner::App_state::Restarting,
                             runner::App_state::Exited,
                             runner::App_state::Stopped,
                             runner::App_state::Failed,
                             runner::App_state::Unknown}) {
        runner::App_state parsed = static_cast<runner::App_state>(0xff);
        NX_CHECK(runner::from_string(runner::to_string(value), parsed));
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == runner::to_string(value));
        nlohmann::json state_json;
        runner::to_json(state_json, value);
        NX_CHECK(state_json.is_string());
    }

    runner::App_spec spec = app_spec(
        "app", "1", "/bin/true", runner::Impact_on_app_suite_operability::Failure);
    spec.args = {"--a", "b"};
    spec.env = {{"A", "1"}, {"B", "2"}};
    spec.cpus = {0, 2};
    spec.restart_attempts = 3;
    spec.restart_delay_s = 4;
    spec.log_level = logging::LogLevel::Warning;
    spec.global_settings_path = "/etc/nexigen";
    spec.local_settings_path = "local";
    nlohmann::json spec_json;
    runner::to_json(spec_json, spec);
    runner::App_spec spec_round;
    runner::from_json(spec_json, spec_round);
    NX_CHECK(spec_round.app == "app");
    NX_CHECK(spec_round.args.size() == 2 && spec_round.args[1] == "b");
    NX_CHECK(spec_round.env.size() == 2);
    NX_CHECK(spec_round.cpus.size() == 2 && spec_round.cpus[1] == 2);
    NX_CHECK(spec_round.log_level == logging::LogLevel::Warning);
    NX_CHECK(!spec.to_string().empty());
    NX_CHECK(!std::format("{}", spec).empty());

    runner::Suite_manifest manifest{};
    manifest.version = 1;
    manifest.suite_id = "s";
    manifest.run_dir = "/runs";
    manifest.artifacts_path = "/artifacts";
    manifest.global_settings_path = "/etc/nexigen";
    manifest.log_level = logging::LogLevel::Debug;
    manifest.suite_restart_attempts = 2;
    manifest.suite_restart_delay_s = 3;
    manifest.manifest_path = "/etc/nexigen/suite.json";
    manifest.runner_control_cpus = {0, 1};
    manifest.runner_logger_cpus = {2};
    manifest.apps.push_back(spec);
    nlohmann::json manifest_json;
    runner::to_json(manifest_json, manifest);
    runner::Suite_manifest manifest_round;
    runner::from_json(manifest_json, manifest_round);
    NX_CHECK(manifest_round.suite_id == "s");
    NX_CHECK(manifest_round.log_level == logging::LogLevel::Debug);
    NX_CHECK(manifest_round.runner_control_cpus.size() == 2);
    NX_CHECK(manifest_round.runner_logger_cpus.size() == 1);
    NX_CHECK(manifest_round.apps.size() == 1);

    runner::Suite_status status{};
    status.state = runner::Suite_state::Degraded;
    status.suite_id = "s";
    status.run_dir = "/runs";
    status.artifacts_path = "/artifacts";
    status.suite_restarts_used = 1;
    runner::Suite_app_status app_status{};
    app_status.app = "app";
    app_status.instance = "1";
    app_status.artifact_dir = "/artifacts/s/app/1/attempt-0002";
    app_status.attempt = 2;
    app_status.state = runner::App_state::Failed;
    app_status.restarts_used = 1;
    status.apps.push_back(app_status);
    nlohmann::json status_json;
    runner::to_json(status_json, status);
    runner::Suite_status status_round;
    runner::from_json(status_json, status_round);
    NX_CHECK(status_round.state == runner::Suite_state::Degraded);
    NX_CHECK(status_round.apps.size() == 1);
    NX_CHECK(status_round.apps[0].attempt == 2);
    NX_CHECK(status_round.apps[0].state == runner::App_state::Failed);

    runner::App_spec hostile = spec;
    runner::from_json(nlohmann::json::array(), hostile);
    NX_CHECK(hostile.app == "app");
    NX_CHECK(hostile.log_level == logging::LogLevel::Warning);
    nlohmann::json wrong_spec{
        {"app", 1}, {"instance", 2}, {"command", 3}, {"args", 4},
        {"env", 5}, {"cpus", 6},
        {"impact_on_app_suite_operability", 7},
        {"restart_attempts", "x"}, {"restart_delay_s", "x"},
        {"idle_timeout_s", "x"}, {"stop_grace_s", "x"},
        {"log_level", 8}, {"global_settings_path", 9},
        {"local_settings_path", 10}};
    runner::from_json(wrong_spec, hostile);
    NX_CHECK(hostile.app == "app");
    NX_CHECK(hostile.args.size() == 2);
    NX_CHECK(hostile.env.size() == 2);
    NX_CHECK(hostile.cpus.size() == 2);
    NX_CHECK(hostile.restart_attempts == 3);
    NX_CHECK(hostile.log_level == logging::LogLevel::Warning);

    runner::Suite_app_metadata app_metadata{};
    app_metadata.app = "app";
    app_metadata.instance = "1";
    app_metadata.instance_dir = "/runs/app/1";
    app_metadata.stop_grace_s = 7;
    nlohmann::json app_metadata_json;
    runner::to_json(app_metadata_json, app_metadata);
    runner::Suite_app_metadata app_metadata_round;
    runner::from_json(app_metadata_json, app_metadata_round);
    NX_CHECK(app_metadata_round.app == "app");
    NX_CHECK(app_metadata_round.stop_grace_s == 7);

    runner::Suite_metadata metadata{};
    metadata.suite_id = "s";
    metadata.run_id = "r";
    metadata.run_dir = "/runs";
    metadata.global_settings_path = "/etc/nexigen";
    metadata.artifacts_path = "/artifacts";
    metadata.pid = 42;
    metadata.start_time_rfc3339 = "2026-11-01T00:00:00.000000000Z";
    metadata.start_time_mono_ns = 123;
    metadata.manifest_path = "/etc/nexigen/suite.json";
    metadata.runner_control_cpus = {0};
    metadata.runner_logger_cpus = {1};
    metadata.apps.push_back(app_metadata);
    nlohmann::json metadata_json;
    runner::to_json(metadata_json, metadata);
    runner::Suite_metadata metadata_round;
    runner::from_json(metadata_json, metadata_round);
    NX_CHECK(metadata_round.pid == 42);
    NX_CHECK(metadata_round.apps.size() == 1);
    NX_CHECK(metadata_round.artifacts_path == "/artifacts");
    NX_CHECK(metadata_round.runner_control_cpus.size() == 1);
    NX_CHECK(metadata_round.runner_logger_cpus.size() == 1);

    runner::Suite_exit suite_exit{};
    suite_exit.state = runner::Suite_state::Failed;
    suite_exit.reason = "restart_exhausted";
    suite_exit.uptime_s = 9;
    nlohmann::json suite_exit_json;
    runner::to_json(suite_exit_json, suite_exit);
    runner::Suite_exit suite_exit_round;
    runner::from_json(suite_exit_json, suite_exit_round);
    NX_CHECK(suite_exit_round.state == runner::Suite_state::Failed);
    NX_CHECK(suite_exit_round.reason == "restart_exhausted");
    NX_CHECK(suite_exit_round.uptime_s == 9);

    nlohmann::json app_status_json;
    runner::to_json(app_status_json, app_status);
    runner::Suite_app_status app_status_round;
    runner::from_json(app_status_json, app_status_round);
    NX_CHECK(app_status_round.artifact_dir ==
             "/artifacts/s/app/1/attempt-0002");
    NX_CHECK(app_status_round.restarts_used == 1);

    runner::Suite_manifest hostile_manifest = manifest_round;
    runner::from_json(nlohmann::json::array(), hostile_manifest);
    NX_CHECK(hostile_manifest.suite_id == "s");
    NX_CHECK(hostile_manifest.apps.size() == 1);
    runner::Suite_metadata hostile_metadata = metadata_round;
    runner::from_json(nlohmann::json::array(), hostile_metadata);
    NX_CHECK(hostile_metadata.suite_id == "s");
    runner::Suite_status hostile_status = status_round;
    runner::from_json(nlohmann::json::array(), hostile_status);
    NX_CHECK(hostile_status.state == runner::Suite_state::Degraded);
    NX_CHECK(hostile_status.apps.size() == 1);

    NX_CHECK(!manifest.to_string().empty());
    NX_CHECK(!app_metadata.to_string().empty());
    NX_CHECK(!metadata.to_string().empty());
    NX_CHECK(!suite_exit.to_string().empty());
    NX_CHECK(!status.to_string().empty());
    NX_CHECK(!app_status.to_string().empty());
    NX_CHECK(!std::format("{}", manifest).empty());
    NX_CHECK(!std::format("{}", app_metadata).empty());
    NX_CHECK(!std::format("{}", metadata).empty());
    NX_CHECK(!std::format("{}", suite_exit).empty());
    NX_CHECK(!std::format("{}", status).empty());
    NX_CHECK(!std::format("{}", app_status).empty());
}

void test_manifest_validation() {
    const std::string root = unique_dir("manifest");
    const std::string artifacts = unique_dir("manifest_artifacts");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    runner::Suite_manifest manifest = two_app_manifest(root, artifacts);
    NX_CHECK(runner::validate_suite_manifest(manifest).has_value());

    runner::Suite_manifest bad = manifest;
    bad.version = 2;
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.artifacts_path = "relative";
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.push_back(bad.apps.front());
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().impact =
        static_cast<runner::Impact_on_app_suite_operability>(0x7f);
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().local_settings_path = "/absolute";
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().local_settings_path = "../escape";
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().cpus = {99999};
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.runner_control_cpus = {99999};
    NX_CHECK(runner::validate_suite_manifest(bad).has_value());
    bad = manifest;
    bad.runner_logger_cpus = {99999};
    NX_CHECK(runner::validate_suite_manifest(bad).has_value());

    bad = manifest;
    bad.apps.clear();
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.log_level = static_cast<logging::LogLevel>(0x7f);
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.suite_id = "bad/id";
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.run_dir = std::string("/tmp/\0bad", 9);
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.global_settings_path = "relative";
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.suite_restart_attempts = 1001;
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.suite_restart_delay_s = 86401;
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.resize(257, manifest.apps.front());
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().app = ".suite";
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().command = std::string("a\0b", 3);
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().args = {std::string("a\0b", 3)};
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().env = {{"", "x"}};
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().env = {{"A=B", "x"}};
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().env = {{"A", std::string("x\0y", 3)}};
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().log_level = static_cast<logging::LogLevel>(0x7f);
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().restart_attempts = 1001;
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().idle_timeout_s = 86401;
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().stop_grace_s = 86401;
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().global_settings_path = "relative";
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().cpus = {0, 0};
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.run_dir = "/" + std::string(510, 'r');
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.artifacts_path = "/" + std::string(510, 'a');
    NX_CHECK(failed_with(runner::validate_suite_manifest(bad),
                         core::ErrorCode::InvalidArgument));

    const std::string path = root + "/manifest.json";
    NX_CHECK(write_manifest(path, manifest));
    runner::Suite_manifest loaded{};
    NX_CHECK(runner::load_suite_manifest(path, loaded).has_value());
    NX_CHECK(loaded.manifest_path == path);
    NX_CHECK(loaded.apps.size() == 2);

    std::ofstream invalid(root + "/bad.json", std::ios::binary | std::ios::trunc);
    invalid << "{not json";
    invalid.close();
    NX_CHECK(failed_with(runner::load_suite_manifest(root + "/bad.json", loaded),
                         core::ErrorCode::InvalidArgument));
    NX_CHECK(failed_with(runner::load_suite_manifest(root + "/missing.json", loaded),
                         core::ErrorCode::SystemError));

    nlohmann::json document;
    runner::to_json(document, manifest);
    document["apps"][0]["impact_on_app_suite_operability"] = "bogus";
    NX_CHECK(runner::write_json_file(root + "/bad_impact.json", document).has_value());
    NX_CHECK(failed_with(
        runner::load_suite_manifest(root + "/bad_impact.json", loaded),
        core::ErrorCode::InvalidArgument));

    runner::to_json(document, manifest);
    document["log_level"] = "chatty";
    NX_CHECK(runner::write_json_file(root + "/bad_level.json", document).has_value());
    NX_CHECK(failed_with(
        runner::load_suite_manifest(root + "/bad_level.json", loaded),
        core::ErrorCode::InvalidArgument));

    runner::to_json(document, manifest);
    document["runner_control_cpus"] = "bad";
    NX_CHECK(runner::write_json_file(root + "/bad_runner_cpus.json", document)
                 .has_value());
    NX_CHECK(failed_with(
        runner::load_suite_manifest(root + "/bad_runner_cpus.json", loaded),
        core::ErrorCode::InvalidArgument));

    int case_index = 0;
    auto rejected = [&](const nlohmann::json& candidate) {
        ++case_index;
        const std::string case_path =
            root + "/loader_case_" + std::to_string(case_index) + ".json";
        if (!runner::write_json_file(case_path, candidate).has_value()) {
            return false;
        }
        return failed_with(runner::load_suite_manifest(case_path, loaded),
                           core::ErrorCode::InvalidArgument);
    };

    NX_CHECK(rejected(nlohmann::json::array()));
    runner::to_json(document, manifest);
    document["log_level"] = 7;
    NX_CHECK(rejected(document));
    runner::to_json(document, manifest);
    document["runner_control_cpus"] = nlohmann::json::array({-1});
    NX_CHECK(rejected(document));
    runner::to_json(document, manifest);
    document["runner_logger_cpus"] =
        nlohmann::json::array({std::uint64_t{4294967296ULL}});
    NX_CHECK(rejected(document));
    runner::to_json(document, manifest);
    document.erase("apps");
    NX_CHECK(rejected(document));
    runner::to_json(document, manifest);
    document["apps"] = nlohmann::json::object();
    NX_CHECK(rejected(document));
    runner::to_json(document, manifest);
    document["apps"] = nlohmann::json::array({42});
    NX_CHECK(rejected(document));
    runner::to_json(document, manifest);
    document["apps"][0].erase("impact_on_app_suite_operability");
    NX_CHECK(rejected(document));
    runner::to_json(document, manifest);
    document["apps"][0]["impact_on_app_suite_operability"] = 3;
    NX_CHECK(rejected(document));
    runner::to_json(document, manifest);
    document["apps"][0]["log_level"] = "chatty";
    NX_CHECK(rejected(document));

    runner::Suite_status missing{};
    NX_CHECK(runner::suite_status(root + "/missing_suite", missing).has_value());
    NX_CHECK(missing.state == runner::Suite_state::Unknown);

    remove_tree(root);
    remove_tree(artifacts);
}

void test_start_status_stop() {
    const std::string root = unique_dir("lifecycle");
    const std::string artifacts = unique_dir("lifecycle_artifacts");
    runner::Suite_manifest manifest = two_app_manifest(root, artifacts);
    std::string suite_dir;
    NX_CHECK(runner::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    runner::Suite_status status = read_status(suite_dir);
    NX_CHECK(status.state == runner::Suite_state::Running);
    NX_CHECK(status.apps.size() == 2);
    for (const runner::Suite_app_status& app : status.apps) {
        NX_CHECK(app.attempt == 1);
        NX_CHECK(file_exists(app.artifact_dir + "/stdout.log"));
        NX_CHECK(file_exists(app.artifact_dir + "/stderr.log"));
        NX_CHECK(file_exists(app.artifact_dir + "/app.jsonl"));
        NX_CHECK(file_exists(app.artifact_dir + "/metrics.jsonl"));
        NX_CHECK(file_exists(app.artifact_dir + "/traces.jsonl"));
        NX_CHECK(file_exists(app.artifact_dir + "/chrono.jsonl"));
        NX_CHECK(file_exists(app.instance_dir + "/settings/logging.json"));
    }

    // A second start while the supervisor is alive is rejected.
    std::string duplicate_dir;
    NX_CHECK(failed_with(runner::start_suite(manifest, duplicate_dir),
                         core::ErrorCode::Conflict));

    NX_CHECK(wait_until(
        [&] {
            const runner::Suite_status current = read_status(suite_dir);
            return contains(read_text(find_app(current, "alpha").artifact_dir +
                                      "/stdout.log"),
                            "alpha-out") &&
                   contains(read_text(find_app(current, "beta").artifact_dir +
                                      "/stdout.log"),
                            "beta-out");
        },
        5000));

    const runner::Suite_app_status alpha = find_app(read_status(suite_dir), "alpha");
    const core::Result<nlohmann::json> logging_document =
        runner::read_json_file(alpha.instance_dir + "/settings/logging.json");
    NX_CHECK(logging_document.has_value());
    if (logging_document) {
        NX_CHECK((*logging_document)["log_root_directory"] == alpha.artifact_dir);
        NX_CHECK((*logging_document)["sink"]["path"] ==
                 alpha.artifact_dir + "/app.jsonl");
    }

    NX_CHECK(runner::stop_suite(suite_dir, 2).has_value());
    status = read_status(suite_dir);
    NX_CHECK(status.state == runner::Suite_state::Stopped);
    NX_CHECK(find_app(status, "alpha").state == runner::App_state::Stopped);
    NX_CHECK(file_exists(suite_dir + "/suite-exit.json"));
    NX_CHECK(runner::stop_suite(suite_dir, 0).has_value());

    remove_tree(root);
    remove_tree(artifacts);
}

void test_suite_edge_paths() {
    runner::Suite_status status{};
    NX_CHECK(failed_with(runner::stop_suite(""),
                         core::ErrorCode::InvalidArgument));
    NX_CHECK(failed_with(runner::suite_status("", status),
                         core::ErrorCode::InvalidArgument));

    const std::string directory = unique_dir("suite_edges");
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    NX_CHECK(failed_with(runner::stop_suite(directory),
                         core::ErrorCode::InvalidArgument));
    NX_CHECK(runner::suite_status(directory, status).has_value());
    NX_CHECK(status.state == runner::Suite_state::Unknown);

    const std::string root = unique_dir("suite_edges_root");
    const std::string artifacts = unique_dir("suite_edges_artifacts");
    std::filesystem::create_directories(root, error);
    runner::Suite_metadata metadata{};
    metadata.suite_id = "edges";
    metadata.run_dir = root;
    metadata.artifacts_path = artifacts;
    metadata.pid = 999999;
    runner::Suite_app_metadata app{};
    app.app = "alpha";
    app.instance = "1";
    app.instance_dir = root + "/alpha/1";
    app.stop_grace_s = 0;
    metadata.apps.push_back(app);
    nlohmann::json metadata_json;
    runner::to_json(metadata_json, metadata);
    NX_CHECK(runner::write_json_file(directory + "/suite.json", metadata_json)
                 .has_value());
    NX_CHECK(runner::suite_status(directory, status).has_value());
    NX_CHECK(status.state == runner::Suite_state::Unknown);
    NX_CHECK(status.apps.size() == 1);
    NX_CHECK(runner::stop_suite(directory).has_value());
    NX_CHECK(file_exists(directory + "/suite-exit.json"));
    NX_CHECK(runner::stop_suite(directory).has_value());

    // Stale metadata with a dead supervisor is replaced by a new start.
    runner::Suite_manifest manifest = base_manifest(root, artifacts);
    manifest.suite_id = "stale";
    manifest.apps.push_back(
        app_spec("alpha", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Degrade));
    manifest.apps.front().args = {"-c", "exec sleep 60"};
    const std::string stale_dir = root + "/.suite/stale";
    std::filesystem::create_directories(stale_dir, error);
    runner::Suite_metadata stale{};
    stale.suite_id = "stale";
    stale.run_dir = root;
    stale.artifacts_path = artifacts;
    stale.pid = 999999;
    nlohmann::json stale_json;
    runner::to_json(stale_json, stale);
    NX_CHECK(runner::write_json_file(stale_dir + "/suite.json", stale_json)
                 .has_value());
    std::string started_dir;
    NX_CHECK(runner::start_suite(manifest, started_dir).has_value());
    NX_CHECK(started_dir == stale_dir);
    NX_CHECK(runner::stop_suite(started_dir, 1).has_value());

    // A target below /proc cannot be created.
    runner::Suite_manifest denied = manifest;
    denied.run_dir =
        "/proc/nexigen_suite_denied_" +
        std::to_string(static_cast<long long>(::getpid()));
    std::string denied_dir;
    NX_CHECK(failed_with(runner::start_suite(denied, denied_dir),
                         core::ErrorCode::SystemError));

    // The Runner facade rejects an invalid manifest before forking.
    runner::Runner suite_runner;
    runner::Suite_manifest invalid_manifest{};
    std::string invalid_dir;
    NX_CHECK(failed_with(
        suite_runner.start_suite(invalid_manifest, invalid_dir),
        core::ErrorCode::InvalidArgument));

    remove_tree(directory);
    remove_tree(root);
    remove_tree(artifacts);
}

void test_relative_manifest_path() {
    const std::string root = unique_dir("relative_manifest");
    const std::string artifacts = unique_dir("relative_artifacts");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    runner::Suite_manifest manifest = two_app_manifest(root, artifacts);
    manifest.manifest_path = root + "/manifest.json";
    NX_CHECK(write_manifest(manifest.manifest_path, manifest));

    char previous[4096]{};
    NX_CHECK(::getcwd(previous, sizeof(previous)) != nullptr);
    NX_CHECK(::chdir(root.c_str()) == 0);
    runner::Suite_manifest loaded{};
    NX_CHECK(runner::load_suite_manifest("manifest.json", loaded).has_value());
    NX_CHECK(loaded.manifest_path == root + "/manifest.json");
    NX_CHECK(::chdir(previous) == 0);

    remove_tree(root);
    remove_tree(artifacts);
}

void test_env_affinity_and_log_level() {
    const std::string root = unique_dir("env");
    const std::string artifacts = unique_dir("env_artifacts");
    runner::Suite_manifest manifest = base_manifest(root, artifacts);
    runner::App_spec spec =
        app_spec("envapp", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Not_impact);
    spec.args = {"-c",
                 "echo SUITE_ENV=$SUITE_ENV; echo APP=$NEXIGEN_APP_NAME; "
                 "echo LEVEL=$NEXIGEN_LOG_LEVEL; exec sleep 60"};
    spec.env = {{"SUITE_ENV", "visible"},
                {"NEXIGEN_APP_NAME", "spoofed"},
                {"NEXIGEN_LOG_LEVEL", "debug"}};
    spec.cpus = {0};
    spec.log_level = logging::LogLevel::Error;
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(runner::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    runner::Suite_status status = read_status(suite_dir);
    const runner::Suite_app_status app = find_app(status, "envapp");
    NX_CHECK(wait_until(
        [&] {
            const std::string output = read_text(app.artifact_dir + "/stdout.log");
            return contains(output, "SUITE_ENV=visible") &&
                   contains(output, "APP=envapp") &&
                   contains(output, "LEVEL=error");
        },
        5000));

    const core::Result<nlohmann::json> resource =
        runner::read_json_file(app.instance_dir + "/resource.json");
    NX_CHECK(resource.has_value());
    if (resource) {
        const std::int32_t pid = (*resource)["pid"].get<std::int32_t>();
        cpu_set_t mask;
        CPU_ZERO(&mask);
        NX_CHECK(::sched_getaffinity(pid, sizeof(mask), &mask) == 0);
        NX_CHECK(CPU_ISSET(0, &mask));
        NX_CHECK(CPU_COUNT(&mask) == 1);
    }

    const core::Result<nlohmann::json> logging_document =
        runner::read_json_file(app.instance_dir + "/settings/logging.json");
    NX_CHECK(logging_document.has_value());
    if (logging_document) {
        NX_CHECK((*logging_document)["initial_log_level"] == "error");
    }

    NX_CHECK(runner::stop_suite(suite_dir, 2).has_value());
    remove_tree(root);
    remove_tree(artifacts);
}

void test_settings_precedence() {
    const std::string root = unique_dir("settings");
    const std::string artifacts = unique_dir("settings_artifacts");
    const std::string global = root + "/global";
    const std::string local = root + "/local";
    const std::string app_global = root + "/app_global";
    std::error_code error;
    std::filesystem::create_directories(global, error);
    std::filesystem::create_directories(local, error);
    std::filesystem::create_directories(app_global, error);
    {
        nlohmann::json document{{"log_root_directory", global},
                                {"app_name", "x"},
                                {"initial_log_level", "debug"}};
        NX_CHECK(runner::write_json_file(global + "/logging.json", document).has_value());
    }
    {
        nlohmann::json document{{"initial_log_level", "warning"}};
        NX_CHECK(runner::write_json_file(local + "/logging.json", document).has_value());
    }
    {
        nlohmann::json document{{"log_root_directory", app_global},
                                {"app_name", "x"},
                                {"initial_log_level", "fatal"}};
        NX_CHECK(runner::write_json_file(app_global + "/logging.json", document)
                     .has_value());
    }

    runner::Suite_manifest manifest = base_manifest(root, artifacts);
    manifest.global_settings_path = global;
    runner::App_spec inherited =
        app_spec("inherited", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Not_impact);
    inherited.args = {"-c", "exec sleep 60"};
    inherited.local_settings_path = "local";
    runner::App_spec overridden =
        app_spec("overridden", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Not_impact);
    overridden.args = {"-c", "exec sleep 60"};
    overridden.local_settings_path = "local";
    overridden.log_level = logging::LogLevel::Error;
    runner::App_spec replaced =
        app_spec("replaced", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Not_impact);
    replaced.args = {"-c", "exec sleep 60"};
    replaced.global_settings_path = app_global;
    manifest.apps.push_back(inherited);
    manifest.apps.push_back(overridden);
    manifest.apps.push_back(replaced);

    std::string suite_dir;
    NX_CHECK(runner::start_suite(manifest, suite_dir).has_value());
    if (file_exists(suite_dir + "/suite.json")) {
        const runner::Suite_status status = read_status(suite_dir);
        const auto inherited_logging = runner::read_json_file(
            find_app(status, "inherited").instance_dir + "/settings/logging.json");
        const auto overridden_logging = runner::read_json_file(
            find_app(status, "overridden").instance_dir + "/settings/logging.json");
        const auto replaced_logging = runner::read_json_file(
            find_app(status, "replaced").instance_dir + "/settings/logging.json");
        NX_CHECK(inherited_logging.has_value());
        NX_CHECK(overridden_logging.has_value());
        NX_CHECK(replaced_logging.has_value());
        if (inherited_logging) {
            NX_CHECK((*inherited_logging)["initial_log_level"] == "warning");
        }
        if (overridden_logging) {
            NX_CHECK((*overridden_logging)["initial_log_level"] == "error");
        }
        if (replaced_logging) {
            NX_CHECK((*replaced_logging)["initial_log_level"] == "fatal");
        }
        NX_CHECK(runner::stop_suite(suite_dir, 2).has_value());
    }

    remove_tree(root);
    remove_tree(artifacts);
}

void test_not_impact_retries_new_attempts() {
    const std::string root = unique_dir("not_impact");
    const std::string artifacts = unique_dir("not_impact_artifacts");
    runner::Suite_manifest manifest = base_manifest(root, artifacts);
    runner::App_spec spec =
        app_spec("crash", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Not_impact);
    spec.args = {"-c", "exit 1"};
    spec.restart_attempts = 2;
    spec.restart_delay_s = 0;
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(runner::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    NX_CHECK(wait_until(
        [&] {
            const runner::Suite_status status = read_status(suite_dir);
            const runner::Suite_app_status app = find_app(status, "crash");
            return app.state == runner::App_state::Failed && app.attempt == 3;
        },
        15000));
    const runner::Suite_status status = read_status(suite_dir);
    NX_CHECK(status.state == runner::Suite_state::Running);
    NX_CHECK(find_app(status, "crash").state == runner::App_state::Failed);
    NX_CHECK(file_exists(runner::attempt_dir_path(artifacts, manifest.suite_id, "crash",
                                          "1", 1) +
                         "/stdout.log"));
    NX_CHECK(file_exists(runner::attempt_dir_path(artifacts, manifest.suite_id, "crash",
                                          "1", 2) +
                         "/stdout.log"));
    NX_CHECK(file_exists(runner::attempt_dir_path(artifacts, manifest.suite_id, "crash",
                                          "1", 3) +
                         "/stdout.log"));
    NX_CHECK(runner::stop_suite(suite_dir, 2).has_value());

    remove_tree(root);
    remove_tree(artifacts);
}

void test_failure_impact_restarts_suite() {
    const std::string root = unique_dir("failure");
    const std::string artifacts = unique_dir("failure_artifacts");
    runner::Suite_manifest manifest = base_manifest(root, artifacts);
    manifest.suite_restart_attempts = 1;
    manifest.suite_restart_delay_s = 0;
    runner::App_spec crashing =
        app_spec("crashing", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Failure);
    crashing.args = {"-c", "exit 1"};
    crashing.restart_attempts = 5;
    crashing.restart_delay_s = 0;
    runner::App_spec healthy =
        app_spec("healthy", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Not_impact);
    healthy.args = {"-c", "exec sleep 60"};
    manifest.apps.push_back(crashing);
    manifest.apps.push_back(healthy);

    std::string suite_dir;
    NX_CHECK(runner::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    NX_CHECK(wait_until(
        [&] {
            return file_exists(suite_dir + "/suite-exit.json") &&
                   read_status(suite_dir).state == runner::Suite_state::Failed;
        },
        20000));

    const runner::Suite_status status = read_status(suite_dir);
    NX_CHECK(status.state == runner::Suite_state::Failed);
    NX_CHECK(file_exists(runner::attempt_dir_path(artifacts, manifest.suite_id,
                                          "healthy", "1", 2) +
                         "/stdout.log"));
    const core::Result<nlohmann::json> exit_document =
        runner::read_json_file(suite_dir + "/suite-exit.json");
    NX_CHECK(exit_document.has_value());
    if (exit_document) {
        NX_CHECK((*exit_document)["state"] == "failed");
        NX_CHECK((*exit_document)["reason"] == "restart_exhausted");
    }

    remove_tree(root);
    remove_tree(artifacts);
}

void test_clean_exit_is_final() {
    const std::string root = unique_dir("clean");
    const std::string artifacts = unique_dir("clean_artifacts");
    runner::Suite_manifest manifest = base_manifest(root, artifacts);
    runner::App_spec spec =
        app_spec("clean", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Not_impact);
    spec.args = {"-c", "exit 0"};
    spec.restart_attempts = 3;
    spec.restart_delay_s = 0;
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(runner::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    NX_CHECK(wait_until(
        [&] {
            const runner::Suite_app_status app =
                find_app(read_status(suite_dir), "clean");
            return app.state == runner::App_state::Exited;
        },
        10000));
    const runner::Suite_status status = read_status(suite_dir);
    NX_CHECK(status.state == runner::Suite_state::Running);
    NX_CHECK(find_app(status, "clean").attempt == 1);
    NX_CHECK(find_app(status, "clean").state == runner::App_state::Exited);
    NX_CHECK(runner::stop_suite(suite_dir, 2).has_value());

    remove_tree(root);
    remove_tree(artifacts);
}

void test_runner_logging_and_affinity() {
    const std::string root = unique_dir("runner_log");
    const std::string artifacts = unique_dir("runner_log_artifacts");
    const int cpu = first_allowed_cpu();
    if (cpu < 0) {
        return;
    }
    runner::Suite_manifest manifest = base_manifest(root, artifacts);
    manifest.log_level = logging::LogLevel::Debug;
    manifest.runner_control_cpus = {static_cast<std::uint32_t>(cpu)};
    manifest.runner_logger_cpus = {static_cast<std::uint32_t>(cpu)};
    runner::App_spec spec =
        app_spec("logged", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Not_impact);
    spec.args = {"-c", "exec sleep 60"};
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(runner::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    const std::string log_path = artifacts + "/" + manifest.suite_id +
                                 "/runner.jsonl";
    NX_CHECK(wait_until([&] { return file_exists(log_path); }, 5000));
    NX_CHECK(wait_until(
        [&] { return contains(read_text(log_path), "event=suite_started"); },
        5000));

    const runner::Suite_metadata metadata = read_metadata(suite_dir);
    NX_CHECK(metadata.runner_control_cpus.size() == 1);
    NX_CHECK(metadata.runner_logger_cpus.size() == 1);
    NX_CHECK(metadata.pid > 0);

    cpu_set_t control_mask;
    CPU_ZERO(&control_mask);
    NX_CHECK(::sched_getaffinity(metadata.pid, sizeof(control_mask),
                                 &control_mask) == 0);
    NX_CHECK(CPU_COUNT(&control_mask) == 1);
    NX_CHECK(cpu_is_set(cpu, control_mask));

    const runner::Suite_status status = read_status(suite_dir);
    const runner::Suite_app_status app = find_app(status, "logged");
    NX_CHECK(app.state == runner::App_state::Running);
    const auto resource =
        runner::read_json_file(app.instance_dir + "/resource.json");
    NX_CHECK(resource.has_value());
    if (resource) {
        const std::int32_t child_pid = (*resource)["pid"].get<std::int32_t>();
        cpu_set_t app_mask;
        CPU_ZERO(&app_mask);
        NX_CHECK(::sched_getaffinity(child_pid, sizeof(app_mask), &app_mask) == 0);
        const long online = ::sysconf(_SC_NPROCESSORS_ONLN);
        if (online > 1) {
            NX_CHECK(CPU_COUNT(&app_mask) > 1);
        }
    }

    NX_CHECK(runner::stop_suite(suite_dir, 2).has_value());
    NX_CHECK(wait_until(
        [&] { return contains(read_text(log_path), "event=suite_stopped"); },
        5000));
    NX_CHECK(contains(read_text(log_path), "event=stop_requested"));

    remove_tree(root);
    remove_tree(artifacts);
}

void test_invalid_affinity_nonfatal() {
    const std::string root = unique_dir("bad_affinity");
    const std::string artifacts = unique_dir("bad_affinity_artifacts");
    runner::Suite_manifest manifest = base_manifest(root, artifacts);
    manifest.runner_control_cpus = {99999};
    manifest.runner_logger_cpus = {99999};
    runner::App_spec spec =
        app_spec("bad_pin", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Not_impact);
    spec.args = {"-c", "exec sleep 60"};
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(runner::start_suite(manifest, suite_dir).has_value());
    if (file_exists(suite_dir + "/suite.json")) {
        NX_CHECK(read_status(suite_dir).state == runner::Suite_state::Running);
        NX_CHECK(runner::stop_suite(suite_dir, 2).has_value());
        NX_CHECK(read_status(suite_dir).state == runner::Suite_state::Stopped);
    }
    remove_tree(root);
    remove_tree(artifacts);
}

void test_setup_failure() {
    const std::string root = unique_dir("setup");
    const std::string artifacts = unique_dir("setup_artifacts");
    runner::Suite_manifest manifest = base_manifest(root, artifacts);
    runner::App_spec spec =
        app_spec("setup", "1", "/bin/true",
                 runner::Impact_on_app_suite_operability::Not_impact);
    manifest.apps.push_back(spec);

    const app::Run_identity identity{manifest.run_id, manifest.run_dir,
                                     "setup", "1", root + "/setup/1"};
    runner::Start_options options{};
    options.artifact_dir =
        "/proc/nexigen_suite_denied_" +
        std::to_string(static_cast<long long>(::getpid()));
    runner::Runner runner;
    NX_CHECK(failed_with(runner.start(identity, {"/bin/true"}, 0, options),
                         core::ErrorCode::SystemError));
    NX_CHECK(!file_exists(root + "/setup/1/resource.json"));

    remove_tree(root);
    remove_tree(artifacts);
}

void test_forced_stop() {
    const std::string root = unique_dir("forced");
    const std::string artifacts = unique_dir("forced_artifacts");
    runner::Suite_manifest manifest = base_manifest(root, artifacts);
    runner::App_spec spec =
        app_spec("forced", "1", "/bin/sh",
                 runner::Impact_on_app_suite_operability::Not_impact);
    spec.args = {"-c", "exec sleep 60"};
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(runner::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    const auto metadata_document =
        runner::read_json_file(suite_dir + "/suite.json");
    NX_CHECK(metadata_document.has_value());
    if (metadata_document) {
        runner::Suite_metadata metadata{};
        runner::from_json(*metadata_document, metadata);
        const runner::Suite_app_status app =
            find_app(read_status(suite_dir), "forced");
        const auto resource =
            runner::read_json_file(app.instance_dir + "/resource.json");
        NX_CHECK(resource.has_value());
        if (resource) {
            const std::int32_t child_pid =
                (*resource)["pid"].get<std::int32_t>();
            NX_CHECK(::kill(metadata.pid, SIGKILL) == 0);
            NX_CHECK(runner::stop_suite(suite_dir, 0).has_value());
            NX_CHECK(file_exists(suite_dir + "/suite-exit.json"));
            const auto exit_document =
                runner::read_json_file(suite_dir + "/suite-exit.json");
            NX_CHECK(exit_document.has_value());
            if (exit_document) {
                NX_CHECK((*exit_document)["reason"] == "forced_stop");
            }
            NX_CHECK(::kill(child_pid, 0) != 0);
        }
    }

    remove_tree(root);
    remove_tree(artifacts);
}

} // namespace

int main() {
    run_suite("enums_serialization", test_enums_and_serialization);
    run_suite("manifest_validation", test_manifest_validation);
    run_suite("start_status_stop", test_start_status_stop);
    run_suite("suite_edge_paths", test_suite_edge_paths);
    run_suite("relative_manifest", test_relative_manifest_path);
    run_suite("env_affinity_level", test_env_affinity_and_log_level);
    run_suite("settings_precedence", test_settings_precedence);
    run_suite("not_impact_retries", test_not_impact_retries_new_attempts);
    run_suite("failure_restarts_suite", test_failure_impact_restarts_suite);
    run_suite("clean_exit_is_final", test_clean_exit_is_final);
    run_suite("runner_logging_affinity", test_runner_logging_and_affinity);
    run_suite("invalid_affinity", test_invalid_affinity_nonfatal);
    run_suite("setup_failure", test_setup_failure);
    run_suite("forced_stop", test_forced_stop);

    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
