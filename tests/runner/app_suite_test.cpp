// nexigen::runner suite tests — manifest, policy, artifacts, and CLI-facing state.
#include <chrono>
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
    manifest.apps.push_back(spec);
    nlohmann::json manifest_json;
    runner::to_json(manifest_json, manifest);
    runner::Suite_manifest manifest_round;
    runner::from_json(manifest_json, manifest_round);
    NX_CHECK(manifest_round.suite_id == "s");
    NX_CHECK(manifest_round.log_level == logging::LogLevel::Debug);
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
    metadata.apps.push_back(app_metadata);
    nlohmann::json metadata_json;
    runner::to_json(metadata_json, metadata);
    runner::Suite_metadata metadata_round;
    runner::from_json(metadata_json, metadata_round);
    NX_CHECK(metadata_round.pid == 42);
    NX_CHECK(metadata_round.apps.size() == 1);
    NX_CHECK(metadata_round.artifacts_path == "/artifacts");

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
    bad.apps.clear();
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
    run_suite("env_affinity_level", test_env_affinity_and_log_level);
    run_suite("settings_precedence", test_settings_precedence);
    run_suite("not_impact_retries", test_not_impact_retries_new_attempts);
    run_suite("failure_restarts_suite", test_failure_impact_restarts_suite);
    run_suite("clean_exit_is_final", test_clean_exit_is_final);
    run_suite("setup_failure", test_setup_failure);
    run_suite("forced_stop", test_forced_stop);

    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
