// nexigen::exec suite tests — manifest, policy, artifacts, and CLI-facing state.
#include <chrono>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
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

#include <nexigen/core/integrity/sha256.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/app_suite.hpp>
#include <nexigen/exec/control_host.hpp>
#include <nexigen/exec/execution_state_tracker.hpp>
#include <nexigen/exec/instance_lifecycle.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/process_state.hpp>
#include <nexigen/exec/process_lifecycle.hpp>
#include <nexigen/exec/platform_controls.hpp>
#include <nexigen/exec/deterministic_client.hpp>
#include <nexigen/exec/manifest_accessor.hpp>
#include <nexigen/exec/machine_manifest.hpp>
#include <nexigen/exec/state_result.hpp>
#include <nexigen/exec/unrecoverable_state.hpp>

namespace core = nexigen::core;
namespace exec = nexigen::exec;
namespace app = nexigen::app;
namespace logging = nexigen::logging;

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
    std::printf("[%-24s] %d checks, %d failed\n", name, g_checks - before_checks,
                g_failures - before_failures);
}

[[nodiscard]] core::CpuSet invalid_cpu_set() {
    core::CpuSet cpus{};
    static_cast<void>(cpus.add(1023));
    return cpus;
}

std::string unique_dir(std::string_view tag) {
    static int counter = 0;
    ++counter;
    return "/tmp/opencode/nexigen_suite_" + std::string(tag) + "_" +
           std::to_string(static_cast<long long>(::getpid())) + "_" + std::to_string(counter);
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
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
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

template <class Predicate> bool wait_until(Predicate predicate, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
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

exec::StateDependentStartupConfig& first_config(exec::AppSpec& spec) {
    if (spec.startup_configs.empty()) {
        spec.startup_configs.push_back(exec::StateDependentStartupConfig{});
        spec.startup_configs.front().state = "Running";
    }
    return spec.startup_configs.front();
}

exec::AppSpec app_spec(std::string app_name, std::string instance, std::string command,
                       exec::ImpactOnAppSuiteOperability impact) {
    exec::AppSpec spec{};
    spec.app = std::move(app_name);
    spec.instance = std::move(instance);
    spec.command = std::move(command);
    spec.impact = impact;
    spec.function_group = "default";
    spec.startup_configs.push_back(exec::StateDependentStartupConfig{});
    spec.startup_configs.front().state = "Running";
    return spec;
}

exec::SuiteManifest base_manifest(const std::string& root, const std::string& artifacts) {
    exec::SuiteManifest manifest{};
    manifest.version = 1;
    manifest.suite_id = "suite";
    manifest.run_id = "run-1";
    manifest.run_dir = root;
    manifest.artifacts_path = artifacts;
    manifest.manifest_path = root + "/manifest.json";
    manifest.suite_restart_attempts = 0;
    manifest.suite_restart_delay_s = 0;
    exec::FunctionGroupConfig group{};
    group.name = "default";
    group.states = {"Off", "Running"};
    group.initial_state = "Running";
    manifest.function_groups.push_back(std::move(group));
    return manifest;
}

exec::SuiteManifest two_app_manifest(const std::string& root, const std::string& artifacts) {
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    exec::AppSpec alpha =
        app_spec("alpha", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Degrade);
    first_config(alpha).args = {"-c", "echo alpha-out; exec sleep 60"};
    exec::AppSpec beta =
        app_spec("beta", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(beta).args = {"-c", "echo beta-out; exec sleep 60"};
    manifest.apps.push_back(std::move(alpha));
    manifest.apps.push_back(std::move(beta));
    return manifest;
}

bool write_manifest(const std::string& path, const exec::SuiteManifest& manifest) {
    nlohmann::json document;
    exec::to_json(document, manifest);
    return exec::write_json_file(path, document).has_value();
}

exec::SuiteStatus read_status(const std::string& suite_dir) {
    exec::SuiteStatus status{};
    static_cast<void>(exec::suite_status(suite_dir, status));
    return status;
}

exec::SuiteMetadata read_metadata(const std::string& suite_dir) {
    exec::SuiteMetadata metadata{};
    const core::Result<nlohmann::json> document = exec::read_json_file(suite_dir + "/suite.json");
    if (document) {
        exec::from_json(*document, metadata);
    }
    return metadata;
}

exec::SuiteAppStatus find_app(const exec::SuiteStatus& status, std::string_view app_name) {
    for (const exec::SuiteAppStatus& app : status.apps) {
        if (app.app == app_name) {
            return app;
        }
    }
    return {};
}

void test_enums_and_serialization() {
    for (const auto value :
         {exec::ImpactOnAppSuiteOperability::Failure, exec::ImpactOnAppSuiteOperability::Degrade,
          exec::ImpactOnAppSuiteOperability::Not_impact}) {
        const std::string_view text = exec::to_string(value);
        exec::ImpactOnAppSuiteOperability parsed =
            static_cast<exec::ImpactOnAppSuiteOperability>(0xff);
        NX_CHECK(exec::from_string(text, parsed));
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == text);
        nlohmann::json json;
        exec::to_json(json, value);
        exec::from_json(json, parsed);
        NX_CHECK(parsed == value);
    }
    NX_CHECK(exec::to_string(static_cast<exec::ImpactOnAppSuiteOperability>(0x7f)) == "unknown");

    for (const auto value : {exec::ProcessType::Daemon, exec::ProcessType::Oneshot}) {
        const std::string_view text = exec::to_string(value);
        exec::ProcessType parsed = static_cast<exec::ProcessType>(0xff);
        NX_CHECK(exec::from_string(text, parsed));
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == text);
        nlohmann::json json;
        exec::to_json(json, value);
        exec::from_json(json, parsed);
        NX_CHECK(parsed == value);
    }
    NX_CHECK(exec::to_string(static_cast<exec::ProcessType>(0x7f)) == "unknown");

    for (const auto value :
         {exec::ProcessState::Idle, exec::ProcessState::Starting, exec::ProcessState::Running,
          exec::ProcessState::Terminating, exec::ProcessState::Terminated}) {
        const std::string_view text = exec::to_string(value);
        exec::ProcessState parsed = static_cast<exec::ProcessState>(0xff);
        NX_CHECK(exec::from_string(text, parsed));
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == text);
        nlohmann::json json;
        exec::to_json(json, value);
        exec::from_json(json, parsed);
        NX_CHECK(parsed == value);
    }
    NX_CHECK(exec::to_string(static_cast<exec::ProcessState>(0x7f)) == "unknown");

    for (const auto value :
         {exec::StateResult::Success, exec::StateResult::InProgress,
          exec::StateResult::OperationFailed, exec::StateResult::OperationCanceled,
          exec::StateResult::UnexpectedTermination, exec::StateResult::InvalidTransition,
          exec::StateResult::InvalidArgument, exec::StateResult::AccessDenied}) {
        const std::string_view text = exec::to_string(value);
        exec::StateResult parsed = static_cast<exec::StateResult>(0xff);
        NX_CHECK(exec::from_string(text, parsed));
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == text);
        nlohmann::json json;
        exec::to_json(json, value);
        exec::from_json(json, parsed);
        NX_CHECK(parsed == value);
    }
    NX_CHECK(exec::to_string(static_cast<exec::StateResult>(0x7f)) == "unknown");

    for (const auto value :
         {exec::SuiteState::Running, exec::SuiteState::Degraded, exec::SuiteState::Failed,
          exec::SuiteState::Stopped, exec::SuiteState::Unknown}) {
        exec::SuiteState parsed = static_cast<exec::SuiteState>(0xff);
        NX_CHECK(exec::from_string(exec::to_string(value), parsed));
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == exec::to_string(value));
        nlohmann::json state_json;
        exec::to_json(state_json, value);
        NX_CHECK(state_json.is_string());
    }
    for (const auto value :
         {exec::AppState::Running, exec::AppState::Restarting, exec::AppState::Exited,
          exec::AppState::Stopped, exec::AppState::Failed, exec::AppState::Unknown}) {
        exec::AppState parsed = static_cast<exec::AppState>(0xff);
        NX_CHECK(exec::from_string(exec::to_string(value), parsed));
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == exec::to_string(value));
        nlohmann::json state_json;
        exec::to_json(state_json, value);
        NX_CHECK(state_json.is_string());
    }

    exec::AppSpec spec =
        app_spec("app", "1", "/bin/true", exec::ImpactOnAppSuiteOperability::Failure);
    first_config(spec).args = {"--a", "b"};
    first_config(spec).env = {{"A", "1"}, {"B", "2"}};
    spec.cpus = core::CpuSet::from_core_mask(1ULL);
    NX_CHECK(spec.cpus.add(64).has_value());
    first_config(spec).restart_attempts = 3;
    first_config(spec).restart_delay_s = 4;
    spec.log_level = core::LogLevel::Warning;
    spec.global_settings_path = "/etc/nexigen";
    spec.local_settings_path = "local";
    spec.type = exec::ProcessType::Oneshot;
    nlohmann::json spec_json;
    exec::to_json(spec_json, spec);
    exec::AppSpec spec_round;
    exec::from_json(spec_json, spec_round);
    NX_CHECK(spec_round.app == "app");
    NX_CHECK(first_config(spec_round).args.size() == 2 && first_config(spec_round).args[1] == "b");
    NX_CHECK(first_config(spec_round).env.size() == 2);
    NX_CHECK(spec_round.cpus.count() == 2U && spec_round.cpus.contains(64U));
    NX_CHECK(spec_round.log_level == core::LogLevel::Warning);
    NX_CHECK(spec_round.type == exec::ProcessType::Oneshot);
    NX_CHECK(spec_json["type"] == "oneshot");
    NX_CHECK(!spec.to_string().empty());
    NX_CHECK(!std::format("{}", spec).empty());

    exec::SuiteManifest manifest{};
    manifest.version = 1;
    manifest.suite_id = "s";
    manifest.run_dir = "/runs";
    manifest.artifacts_path = "/artifacts";
    manifest.global_settings_path = "/etc/nexigen";
    manifest.log_level = core::LogLevel::Debug;
    manifest.suite_restart_attempts = 2;
    manifest.suite_restart_delay_s = 3;
    manifest.manifest_path = "/etc/nexigen/suite.json";
    manifest.execution_manager_control_thread.set_cpus(core::CpuSet::from_core_mask(3));
    NX_CHECK(manifest.execution_manager_control_thread.set_name("control").has_value());
    NX_CHECK(
        manifest.execution_manager_control_thread.set_scheduler(core::SchedulingPolicy::Other, 0)
            .has_value());
    manifest.execution_manager_control_thread.set_apply_mode(core::ThreadApplyMode::Required);
    manifest.execution_manager_logger_thread.set_cpus(core::CpuSet::from_core_mask(2));
    manifest.apps.push_back(spec);
    nlohmann::json manifest_json;
    exec::to_json(manifest_json, manifest);
    exec::SuiteManifest manifest_round;
    exec::from_json(manifest_json, manifest_round);
    NX_CHECK(manifest_round.suite_id == "s");
    NX_CHECK(manifest_round.log_level == core::LogLevel::Debug);
    NX_CHECK(manifest_round.execution_manager_control_thread.cpus().count() == 2U);
    NX_CHECK(manifest_round.execution_manager_control_thread.name() == "control");
    NX_CHECK(manifest_round.execution_manager_control_thread.scheduler_configured());
    NX_CHECK(manifest_round.execution_manager_logger_thread.cpus().count() == 1U);
    NX_CHECK(manifest_round.apps.size() == 1);

    exec::SuiteStatus status{};
    status.state = exec::SuiteState::Degraded;
    status.suite_id = "s";
    status.run_dir = "/runs";
    status.artifacts_path = "/artifacts";
    status.suite_restarts_used = 1;
    exec::SuiteAppStatus app_status{};
    app_status.app = "app";
    app_status.instance = "1";
    app_status.artifact_dir = "/artifacts/s/app/1/attempt-0002";
    app_status.attempt = 2;
    app_status.state = exec::AppState::Failed;
    app_status.restarts_used = 1;
    status.apps.push_back(app_status);
    nlohmann::json status_json;
    exec::to_json(status_json, status);
    exec::SuiteStatus status_round;
    exec::from_json(status_json, status_round);
    NX_CHECK(status_round.state == exec::SuiteState::Degraded);
    NX_CHECK(status_round.apps.size() == 1);
    NX_CHECK(status_round.apps[0].attempt == 2);
    NX_CHECK(status_round.apps[0].state == exec::AppState::Failed);

    exec::AppSpec hostile = spec;
    exec::from_json(nlohmann::json::array(), hostile);
    NX_CHECK(hostile.app == "app");
    NX_CHECK(hostile.log_level == core::LogLevel::Warning);
    nlohmann::json wrong_spec{{"app", 1},
                              {"instance", 2},
                              {"command", 3},
                              {"function_group", 4},
                              {"startup_configs", 5},
                              {"cpus", 6},
                              {"impact_on_app_suite_operability", 7},
                              {"idle_timeout_s", "x"},
                              {"log_level", 8},
                              {"global_settings_path", 9},
                              {"local_settings_path", 10}};
    exec::from_json(wrong_spec, hostile);
    NX_CHECK(hostile.app == "app");
    NX_CHECK(first_config(hostile).args.size() == 2);
    NX_CHECK(first_config(hostile).env.size() == 2);
    NX_CHECK(hostile.cpus.count() == 2U);
    NX_CHECK(first_config(hostile).restart_attempts == 3);
    NX_CHECK(hostile.log_level == core::LogLevel::Warning);

    exec::from_json(nlohmann::json::object(), hostile);
    exec::from_json(nlohmann::json{{"startup_configs", 3}}, hostile);
    exec::from_json(nlohmann::json{{"cpus", nlohmann::json::array({-1})}}, hostile);
    exec::from_json(nlohmann::json{{"cpus", nlohmann::json::array({0, 0})}}, hostile);
    NX_CHECK(hostile.cpus.count() == 2U); // duplicates leave the old set intact
    exec::from_json(nlohmann::json{{"cpus", nlohmann::json::array({std::uint64_t{4294967296ULL}})}},
                    hostile);
    exec::from_json(nlohmann::json{{"startup_configs", 4}}, hostile);
    exec::from_json(nlohmann::json{{"startup_configs", 5}}, hostile);
    exec::from_json(nlohmann::json{{"log_level", "chatty"}}, hostile);
    NX_CHECK(hostile.log_level == core::LogLevel::Warning);

    exec::SuiteAppMetadata app_metadata{};
    app_metadata.app = "app";
    app_metadata.instance = "1";
    app_metadata.instance_dir = "/runs/app/1";
    app_metadata.stop_grace_s = 7;
    nlohmann::json app_metadata_json;
    exec::to_json(app_metadata_json, app_metadata);
    exec::SuiteAppMetadata app_metadata_round;
    exec::from_json(app_metadata_json, app_metadata_round);
    NX_CHECK(app_metadata_round.app == "app");
    NX_CHECK(app_metadata_round.stop_grace_s == 7);

    exec::SuiteMetadata metadata{};
    metadata.suite_id = "s";
    metadata.run_id = "r";
    metadata.run_dir = "/runs";
    metadata.global_settings_path = "/etc/nexigen";
    metadata.artifacts_path = "/artifacts";
    metadata.pid = 42;
    metadata.start_time_rfc3339 = "2026-11-01T00:00:00.000000000Z";
    metadata.start_time_mono_ns = 123;
    metadata.manifest_path = "/etc/nexigen/suite.json";
    metadata.execution_manager_control_thread.set_cpus(core::CpuSet::from_core_mask(1));
    metadata.execution_manager_logger_thread.set_cpus(core::CpuSet::from_core_mask(2));
    metadata.apps.push_back(app_metadata);
    nlohmann::json metadata_json;
    exec::to_json(metadata_json, metadata);
    exec::SuiteMetadata metadata_round;
    exec::from_json(metadata_json, metadata_round);
    NX_CHECK(metadata_round.pid == 42);
    NX_CHECK(metadata_round.apps.size() == 1);
    NX_CHECK(metadata_round.artifacts_path == "/artifacts");
    NX_CHECK(metadata_round.execution_manager_control_thread.cpus().count() == 1U);
    NX_CHECK(metadata_round.execution_manager_logger_thread.cpus().count() == 1U);

    // Legacy persisted lists remain compatibility inputs for both manifest and
    // suite metadata; canonical specs are emitted by the new writer instead.
    nlohmann::json legacy_manifest_json;
    legacy_manifest_json["execution_manager_control_cpus"] = {0U, 1U};
    legacy_manifest_json["execution_manager_logger_cpus"] = {2U};
    exec::SuiteManifest legacy_manifest{};
    exec::from_json(legacy_manifest_json, legacy_manifest);
    NX_CHECK(legacy_manifest.execution_manager_control_thread.cpus().count() == 2U);
    NX_CHECK(legacy_manifest.execution_manager_logger_thread.cpus().contains(2U));
    nlohmann::json legacy_metadata_json;
    legacy_metadata_json["execution_manager_control_cpus"] = {0U};
    legacy_metadata_json["execution_manager_logger_cpus"] = {1U};
    exec::SuiteMetadata legacy_metadata{};
    exec::from_json(legacy_metadata_json, legacy_metadata);
    NX_CHECK(legacy_metadata.execution_manager_control_thread.cpus().contains(0U));
    NX_CHECK(legacy_metadata.execution_manager_logger_thread.cpus().contains(1U));

    exec::SuiteExit suite_exit{};
    suite_exit.state = exec::SuiteState::Failed;
    suite_exit.reason = "restart_exhausted";
    suite_exit.uptime_s = 9;
    nlohmann::json suite_exit_json;
    exec::to_json(suite_exit_json, suite_exit);
    exec::SuiteExit suite_exit_round;
    exec::from_json(suite_exit_json, suite_exit_round);
    NX_CHECK(suite_exit_round.state == exec::SuiteState::Failed);
    NX_CHECK(suite_exit_round.reason == "restart_exhausted");
    NX_CHECK(suite_exit_round.uptime_s == 9);

    nlohmann::json app_status_json;
    exec::to_json(app_status_json, app_status);
    exec::SuiteAppStatus app_status_round;
    exec::from_json(app_status_json, app_status_round);
    NX_CHECK(app_status_round.artifact_dir == "/artifacts/s/app/1/attempt-0002");
    NX_CHECK(app_status_round.restarts_used == 1);

    exec::SuiteManifest hostile_manifest = manifest_round;
    exec::from_json(nlohmann::json::array(), hostile_manifest);
    NX_CHECK(hostile_manifest.suite_id == "s");
    NX_CHECK(hostile_manifest.apps.size() == 1);
    exec::SuiteMetadata hostile_metadata = metadata_round;
    exec::from_json(nlohmann::json::array(), hostile_metadata);
    NX_CHECK(hostile_metadata.suite_id == "s");
    exec::SuiteStatus hostile_status = status_round;
    exec::from_json(nlohmann::json::array(), hostile_status);
    NX_CHECK(hostile_status.state == exec::SuiteState::Degraded);
    NX_CHECK(hostile_status.apps.size() == 1);

    nlohmann::json wrong_manifest{{"version", "x"},
                                  {"suite_id", 1},
                                  {"run_id", 2},
                                  {"run_dir", 3},
                                  {"global_settings_path", 4},
                                  {"artifacts_path", 5},
                                  {"log_level", 6},
                                  {"suite_restart_attempts", "x"},
                                  {"suite_restart_delay_s", "x"},
                                  {"manifest_path", 7},
                                  {"execution_manager_control_cpus", 8},
                                  {"execution_manager_logger_cpus", 9},
                                  {"apps", 10}};
    exec::from_json(wrong_manifest, hostile_manifest);
    NX_CHECK(hostile_manifest.suite_id == "s");

    nlohmann::json wrong_metadata{{"suite_id", 1},
                                  {"run_id", 2},
                                  {"run_dir", 3},
                                  {"global_settings_path", 4},
                                  {"artifacts_path", 5},
                                  {"pid", "x"},
                                  {"start_time_rfc3339", 6},
                                  {"start_time_mono_ns", "x"},
                                  {"manifest_path", 7},
                                  {"execution_manager_control_cpus", 8},
                                  {"execution_manager_logger_cpus", 9},
                                  {"apps", 10}};
    exec::from_json(wrong_metadata, hostile_metadata);
    NX_CHECK(hostile_metadata.suite_id == "s");
    exec::from_json(nlohmann::json{{"pid", std::int64_t{1} << 40}}, hostile_metadata);
    NX_CHECK(hostile_metadata.suite_id == "s");

    nlohmann::json wrong_status{{"state", 1},
                                {"suite_id", 2},
                                {"run_dir", 3},
                                {"artifacts_path", 4},
                                {"suite_restarts_used", "x"},
                                {"apps", 5}};
    exec::from_json(wrong_status, hostile_status);
    NX_CHECK(hostile_status.state == exec::SuiteState::Degraded);

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
    exec::SuiteManifest manifest = two_app_manifest(root, artifacts);
    NX_CHECK(exec::validate_suite_manifest(manifest).has_value());

    exec::SuiteManifest bad = manifest;
    bad.version = 2;
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.artifacts_path = "relative";
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.push_back(bad.apps.front());
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().impact = static_cast<exec::ImpactOnAppSuiteOperability>(0x7f);
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().local_settings_path = "/absolute";
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().local_settings_path = "../escape";
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    static_cast<void>(bad.apps.front().cpus.add(1023));
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.execution_manager_control_thread.set_cpus(invalid_cpu_set());
    NX_CHECK(exec::validate_suite_manifest(bad).has_value());
    bad = manifest;
    bad.execution_manager_logger_thread.set_cpus(invalid_cpu_set());
    NX_CHECK(exec::validate_suite_manifest(bad).has_value());

    bad = manifest;
    bad.apps.clear();
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.log_level = static_cast<core::LogLevel>(0x7f);
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.suite_id = "bad/id";
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.run_dir = std::string("/tmp/\0bad", 9);
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.global_settings_path = "relative";
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.suite_restart_attempts = 1001;
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.suite_restart_delay_s = 86401;
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.resize(257, manifest.apps.front());
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().app = ".suite";
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().command = std::string("a\0b", 3);
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.front()).args = {std::string("a\0b", 3)};
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.front()).env = {{"", "x"}};
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.front()).env = {{"A=B", "x"}};
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.front()).env = {{"A", std::string("x\0y", 3)}};
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().log_level = static_cast<core::LogLevel>(0x7f);
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.front()).restart_attempts = 1001;
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().idle_timeout_s = 86401;
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.front()).termination_timeout_ms = 86401000;
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().global_settings_path = "relative";
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    static_cast<void>(bad.apps.front().cpus.add(1023));
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.run_dir = "/" + std::string(510, 'r');
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.artifacts_path = "/" + std::string(510, 'a');
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.artifacts_path = std::string("/tmp/\0bad", 9);
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.global_settings_path = std::string("/tmp/\0bad", 9);
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().instance = "bad/instance";
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.front()).restart_delay_s = 86401;
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.front()).env = {{std::string("A\0B", 3), "x"}};
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.back()).env = {{"C", "d"}};
    NX_CHECK(exec::validate_suite_manifest(bad).has_value());

    bad = manifest;
    bad.function_groups.front().initial_state = "Missing";
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().function_group.clear();
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().function_group = "missing";
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().startup_configs.clear();
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.front()).state = "Off";
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.front()).state = "Missing";
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.front().startup_configs.push_back(first_config(bad.apps.front()));
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.front()).dependencies.push_back({"ghost", exec::ProcessState::Running});
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    bad.apps.back().function_group = "other";
    bad.function_groups.push_back({"other", {"Off", "Running"}, "Running"});
    first_config(bad.apps.front()).dependencies.push_back({"beta", exec::ProcessState::Running});
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    bad = manifest;
    first_config(bad.apps.front()).dependencies.push_back({"beta", exec::ProcessState::Running});
    first_config(bad.apps.back()).dependencies.push_back({"alpha", exec::ProcessState::Running});
    NX_CHECK(failed_with(exec::validate_suite_manifest(bad), core::ErrorCode::InvalidArgument));

    const std::string path = root + "/manifest.json";
    NX_CHECK(write_manifest(path, manifest));
    exec::SuiteManifest loaded{};
    NX_CHECK(exec::load_suite_manifest(path, loaded).has_value());
    NX_CHECK(loaded.manifest_path == path);
    NX_CHECK(loaded.apps.size() == 2);

    std::ofstream invalid(root + "/bad.json", std::ios::binary | std::ios::trunc);
    invalid << "{not json";
    invalid.close();
    NX_CHECK(failed_with(exec::load_suite_manifest(root + "/bad.json", loaded),
                         core::ErrorCode::InvalidArgument));
    NX_CHECK(failed_with(exec::load_suite_manifest(root + "/missing.json", loaded),
                         core::ErrorCode::SystemError));

    nlohmann::json document;
    exec::to_json(document, manifest);
    document["apps"][0]["impact_on_app_suite_operability"] = "bogus";
    NX_CHECK(exec::write_json_file(root + "/bad_impact.json", document).has_value());
    NX_CHECK(failed_with(exec::load_suite_manifest(root + "/bad_impact.json", loaded),
                         core::ErrorCode::InvalidArgument));

    exec::to_json(document, manifest);
    document["log_level"] = "chatty";
    NX_CHECK(exec::write_json_file(root + "/bad_level.json", document).has_value());
    NX_CHECK(failed_with(exec::load_suite_manifest(root + "/bad_level.json", loaded),
                         core::ErrorCode::InvalidArgument));

    exec::to_json(document, manifest);
    document["execution_manager_control_cpus"] = "bad";
    NX_CHECK(
        exec::write_json_file(root + "/bad_execution_manager_cpus.json", document).has_value());
    NX_CHECK(
        failed_with(exec::load_suite_manifest(root + "/bad_execution_manager_cpus.json", loaded),
                    core::ErrorCode::InvalidArgument));

    int case_index = 0;
    auto rejected = [&](const nlohmann::json& candidate) {
        ++case_index;
        const std::string case_path = root + "/loader_case_" + std::to_string(case_index) + ".json";
        if (!exec::write_json_file(case_path, candidate).has_value()) {
            return false;
        }
        return failed_with(exec::load_suite_manifest(case_path, loaded),
                           core::ErrorCode::InvalidArgument);
    };

    NX_CHECK(rejected(nlohmann::json::array()));
    exec::to_json(document, manifest);
    document["log_level"] = 7;
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["execution_manager_control_cpus"] = nlohmann::json::array({-1});
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["execution_manager_logger_cpus"] =
        nlohmann::json::array({std::uint64_t{4294967296ULL}});
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document.erase("apps");
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["apps"] = nlohmann::json::object();
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["apps"] = nlohmann::json::array({42});
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["apps"][0].erase("impact_on_app_suite_operability");
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["apps"][0]["impact_on_app_suite_operability"] = 3;
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["apps"][0]["type"] = "batch";
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["apps"][0]["type"] = 3;
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["apps"][0]["log_level"] = "chatty";
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["global_settings_path"] = "relative";
    NX_CHECK(rejected(document));

    // Focused json_enum_fields_valid rejection paths for the decomposed helpers.
    exec::to_json(document, manifest);
    document["execution_manager_control_thread"] = nlohmann::json{{"cpus", "bad"}};
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["execution_manager_logger_thread"] = nlohmann::json{{"policy", "bad"}};
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["log_level"] = nlohmann::json::array();
    NX_CHECK(rejected(document));
    exec::to_json(document, manifest);
    document["apps"][0]["log_level"] = nlohmann::json::object();
    NX_CHECK(rejected(document));

    exec::SuiteStatus missing{};
    NX_CHECK(exec::suite_status(root + "/missing_suite", missing).has_value());
    NX_CHECK(missing.state == exec::SuiteState::Unknown);

    remove_tree(root);
    remove_tree(artifacts);
}


void test_manifest_digest() {
    const std::string root = unique_dir("manifest_digest");
    const std::string artifacts = unique_dir("manifest_digest_artifacts");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    exec::SuiteManifest manifest = two_app_manifest(root, artifacts);
    const std::string path = root + "/manifest.json";
    NX_CHECK(write_manifest(path, manifest));

    const auto digest = nexigen::core::integrity::sha256_file(path);
    NX_CHECK(digest.has_value());

    exec::SuiteManifest verified{};
    NX_CHECK(exec::load_suite_manifest(path, *digest, verified).has_value());
    NX_CHECK(verified.apps.size() == 2U);

    exec::SuiteManifest tampered{};
    const core::Status mismatch =
        exec::load_suite_manifest(path, std::string(64, '0'), tampered);
    NX_CHECK(!mismatch.has_value() && mismatch.error() == core::ErrorCode::SecurityError);

    // An empty digest keeps the unverified path.
    exec::SuiteManifest plain{};
    NX_CHECK(exec::load_suite_manifest(path, std::string_view{}, plain).has_value());

    std::filesystem::remove_all(root, error);
    std::filesystem::remove_all(artifacts, error);
}


void test_settings_digests() {
    const std::string root = unique_dir("settings_digests");
    const std::string artifacts = unique_dir("settings_digests_artifacts");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const std::string settings_path = root + "/logging.json";
    const std::string content = "{\"initial_log_level\":\"debug\"}\n";
    {
        std::ofstream out(settings_path, std::ios::binary | std::ios::trunc);
        out << content;
    }
    const std::string digest = nexigen::core::integrity::sha256_hex(std::string_view(content));

    exec::SuiteManifest manifest = two_app_manifest(root, artifacts);
    manifest.settings_digests.push_back(exec::SettingsDigest{settings_path, digest});
    NX_CHECK(exec::validate_suite_manifest(manifest).has_value());

    // Round-trip preserves the section.
    nlohmann::json document;
    exec::to_json(document, manifest);
    exec::SuiteManifest parsed{};
    exec::from_json(document, parsed);
    NX_CHECK(parsed.settings_digests.size() == 1U);
    NX_CHECK(parsed.settings_digests[0].path == settings_path);
    NX_CHECK(parsed.settings_digests[0].sha256 == digest);

    // Matching file verifies; a changed file fails and names the path.
    std::string verify_error;
    NX_CHECK(exec::verify_settings_digests(manifest, verify_error));
    NX_CHECK(verify_error.empty());
    {
        std::ofstream out(settings_path, std::ios::binary | std::ios::trunc);
        out << "{\"initial_log_level\":\"trace\"}\n";
    }
    NX_CHECK(!exec::verify_settings_digests(manifest, verify_error));
    NX_CHECK(verify_error.find(settings_path) != std::string::npos);

    // An absent section is the compatible default.
    exec::SuiteManifest plain = two_app_manifest(root, artifacts);
    NX_CHECK(exec::verify_settings_digests(plain, verify_error));

    // Relative path and malformed digest are rejected.
    exec::SuiteManifest relative = two_app_manifest(root, artifacts);
    relative.settings_digests.push_back(exec::SettingsDigest{"relative.json", digest});
    NX_CHECK(!exec::validate_suite_manifest(relative).has_value());

    exec::SuiteManifest bad_digest = two_app_manifest(root, artifacts);
    bad_digest.settings_digests.push_back(exec::SettingsDigest{settings_path, "not-a-digest"});
    NX_CHECK(!exec::validate_suite_manifest(bad_digest).has_value());

    std::filesystem::remove_all(root, error);
    std::filesystem::remove_all(artifacts, error);
}


void test_app_credentials() {
    const std::string root = unique_dir("credentials");
    const std::string artifacts = unique_dir("credentials_artifacts");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    exec::SuiteManifest manifest = two_app_manifest(root, artifacts);
    manifest.apps[0].uid = 1000U;
    manifest.apps[0].gid = 1000U;
    NX_CHECK(exec::validate_suite_manifest(manifest).has_value());

    nlohmann::json document;
    exec::to_json(document, manifest);
    exec::SuiteManifest parsed{};
    exec::from_json(document, parsed);
    NX_CHECK(parsed.apps[0].uid.has_value() && *parsed.apps[0].uid == 1000U);
    NX_CHECK(parsed.apps[0].gid.has_value() && *parsed.apps[0].gid == 1000U);

    exec::SuiteManifest bad = manifest;
    bad.apps[0].uid = 0U;
    NX_CHECK(!exec::validate_suite_manifest(bad).has_value());

    bad = manifest;
    bad.apps[0].uid.reset();
    NX_CHECK(!exec::validate_suite_manifest(bad).has_value()); // gid without uid

    bad = manifest;
    bad.apps[0].gid = 0U;
    NX_CHECK(!exec::validate_suite_manifest(bad).has_value());

    // Non-root policy round-trips.
    manifest.trusted_platform.require_non_root = true;
    exec::to_json(document, manifest);
    exec::from_json(document, parsed);
    NX_CHECK(parsed.trusted_platform.require_non_root);

    std::filesystem::remove_all(root, error);
    std::filesystem::remove_all(artifacts, error);
}

void test_start_status_stop() {
    const std::string root = unique_dir("lifecycle");
    const std::string artifacts = unique_dir("lifecycle_artifacts");
    exec::SuiteManifest manifest = two_app_manifest(root, artifacts);
    std::string suite_dir;
    NX_CHECK(exec::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    exec::SuiteStatus status = read_status(suite_dir);
    NX_CHECK(status.state == exec::SuiteState::Running);
    NX_CHECK(status.apps.size() == 2);
    for (const exec::SuiteAppStatus& app : status.apps) {
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
    NX_CHECK(failed_with(exec::start_suite(manifest, duplicate_dir), core::ErrorCode::Conflict));

    NX_CHECK(wait_until(
        [&] {
            const exec::SuiteStatus current = read_status(suite_dir);
            return contains(read_text(find_app(current, "alpha").artifact_dir + "/stdout.log"),
                            "alpha-out") &&
                   contains(read_text(find_app(current, "beta").artifact_dir + "/stdout.log"),
                            "beta-out");
        },
        5000));

    const exec::SuiteAppStatus alpha = find_app(read_status(suite_dir), "alpha");
    const core::Result<nlohmann::json> logging_document =
        exec::read_json_file(alpha.instance_dir + "/settings/logging.json");
    NX_CHECK(logging_document.has_value());
    if (logging_document) {
        NX_CHECK((*logging_document)["log_root_directory"] == alpha.artifact_dir);
        NX_CHECK((*logging_document)["sink"]["path"] == alpha.artifact_dir + "/app.jsonl");
    }

    NX_CHECK(exec::stop_suite(suite_dir, 2).has_value());
    status = read_status(suite_dir);
    NX_CHECK(status.state == exec::SuiteState::Stopped);
    NX_CHECK(find_app(status, "alpha").state == exec::AppState::Stopped);
    NX_CHECK(file_exists(suite_dir + "/suite-exit.json"));
    NX_CHECK(exec::stop_suite(suite_dir, 0).has_value());

    remove_tree(root);
    remove_tree(artifacts);
}

void test_initial_state_selection() {
    const std::string root = unique_dir("initial_state");
    const std::string artifacts = unique_dir("initial_state_artifacts");
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    manifest.function_groups.front().initial_state.clear();
    exec::AppSpec spec =
        app_spec("initial_app", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(spec).args = {"-c", "exec sleep 60"};
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(exec::start_suite(manifest, suite_dir).has_value());
    if (file_exists(suite_dir + "/suite.json")) {
        NX_CHECK(wait_until(
            [&] {
                return find_app(read_status(suite_dir), "initial_app").state ==
                       exec::AppState::Running;
            },
            5000));
        NX_CHECK(exec::stop_suite(suite_dir, 2).has_value());
    }
    remove_tree(root);
    remove_tree(artifacts);

    const std::string off_root = unique_dir("initial_off");
    const std::string off_artifacts = unique_dir("initial_off_artifacts");
    exec::SuiteManifest off_manifest = base_manifest(off_root, off_artifacts);
    off_manifest.function_groups.front().initial_state = "Off";
    exec::AppSpec off_spec =
        app_spec("off_app", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(off_spec).args = {"-c", "exec sleep 60"};
    off_manifest.apps.push_back(off_spec);
    std::string off_dir;
    NX_CHECK(exec::start_suite(off_manifest, off_dir).has_value());
    if (file_exists(off_dir + "/suite.json")) {
        NX_CHECK(find_app(read_status(off_dir), "off_app").state == exec::AppState::Stopped);
        NX_CHECK(exec::stop_suite(off_dir, 2).has_value());
    }
    remove_tree(off_root);
    remove_tree(off_artifacts);
}

void test_suite_edge_paths() {
    exec::SuiteStatus status{};
    NX_CHECK(failed_with(exec::stop_suite(""), core::ErrorCode::InvalidArgument));
    NX_CHECK(failed_with(exec::suite_status("", status), core::ErrorCode::InvalidArgument));

    const std::string directory = unique_dir("suite_edges");
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    NX_CHECK(failed_with(exec::stop_suite(directory), core::ErrorCode::InvalidArgument));
    NX_CHECK(exec::suite_status(directory, status).has_value());
    NX_CHECK(status.state == exec::SuiteState::Unknown);

    const std::string root = unique_dir("suite_edges_root");
    const std::string artifacts = unique_dir("suite_edges_artifacts");
    std::filesystem::create_directories(root, error);
    exec::SuiteMetadata metadata{};
    metadata.suite_id = "edges";
    metadata.run_dir = root;
    metadata.artifacts_path = artifacts;
    metadata.pid = 999999;
    exec::SuiteAppMetadata app{};
    app.app = "alpha";
    app.instance = "1";
    app.instance_dir = root + "/alpha/1";
    app.stop_grace_s = 0;
    metadata.apps.push_back(app);
    nlohmann::json metadata_json;
    exec::to_json(metadata_json, metadata);
    NX_CHECK(exec::write_json_file(directory + "/suite.json", metadata_json).has_value());
    NX_CHECK(exec::suite_status(directory, status).has_value());
    NX_CHECK(status.state == exec::SuiteState::Unknown);
    NX_CHECK(status.apps.size() == 1);
    NX_CHECK(exec::stop_suite(directory).has_value());
    NX_CHECK(file_exists(directory + "/suite-exit.json"));
    NX_CHECK(exec::stop_suite(directory).has_value());

    // Stale metadata with a dead supervisor is replaced by a new start.
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    manifest.suite_id = "stale";
    manifest.apps.push_back(
        app_spec("alpha", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Degrade));
    first_config(manifest.apps.front()).args = {"-c", "exec sleep 60"};
    const std::string stale_dir = root + "/.suite/stale";
    std::filesystem::create_directories(stale_dir, error);
    exec::SuiteMetadata stale{};
    stale.suite_id = "stale";
    stale.run_dir = root;
    stale.artifacts_path = artifacts;
    stale.pid = 999999;
    nlohmann::json stale_json;
    exec::to_json(stale_json, stale);
    NX_CHECK(exec::write_json_file(stale_dir + "/suite.json", stale_json).has_value());
    std::string started_dir;
    NX_CHECK(exec::start_suite(manifest, started_dir).has_value());
    NX_CHECK(started_dir == stale_dir);
    NX_CHECK(exec::stop_suite(started_dir, 1).has_value());

    // A target below /proc cannot be created.
    exec::SuiteManifest denied = manifest;
    denied.run_dir =
        "/proc/nexigen_suite_denied_" + std::to_string(static_cast<long long>(::getpid()));
    std::string denied_dir;
    NX_CHECK(failed_with(exec::start_suite(denied, denied_dir), core::ErrorCode::SystemError));

    // The ExecutionManager facade rejects an invalid manifest before forking.
    exec::ExecutionManager suite_runner;
    exec::SuiteManifest invalid_manifest{};
    std::string invalid_dir;
    NX_CHECK(failed_with(suite_runner.start_suite(invalid_manifest, invalid_dir),
                         core::ErrorCode::InvalidArgument));

    // Invalid global logging settings fall back to defaults with a warning.
    const std::string log_root = unique_dir("suite_bad_log");
    const std::string log_artifacts = unique_dir("suite_bad_log_artifacts");
    const std::string log_settings = unique_dir("suite_bad_settings");
    std::filesystem::create_directories(log_root, error);
    std::filesystem::create_directories(log_settings, error);
    {
        std::ofstream output(log_settings + "/logging.json", std::ios::binary | std::ios::trunc);
        output << "{not-json";
    }
    exec::SuiteManifest bad_log = base_manifest(log_root, log_artifacts);
    bad_log.suite_id = "bad_log";
    bad_log.global_settings_path = log_settings;
    exec::AppSpec bad_log_app =
        app_spec("alpha", "1", "/bin/true", exec::ImpactOnAppSuiteOperability::Degrade);
    bad_log_app.type = exec::ProcessType::Oneshot;
    bad_log.apps.push_back(bad_log_app);
    std::string bad_log_dir;
    NX_CHECK(exec::start_suite(bad_log, bad_log_dir).has_value());
    NX_CHECK(exec::stop_suite(bad_log_dir, 1).has_value());
    remove_tree(log_root);
    remove_tree(log_artifacts);
    remove_tree(log_settings);

    // A live supervisor PID is terminated and a forced exit record is written.
    const pid_t live_supervisor = ::fork();
    if (live_supervisor == 0) {
        ::execl("/bin/sleep", "sleep", "60", nullptr);
        _exit(127);
    }
    NX_CHECK(live_supervisor > 0);
    if (live_supervisor > 0) {
        const std::string live_dir = root + "/.suite/live";
        std::filesystem::create_directories(live_dir, error);
        exec::SuiteMetadata live{};
        live.suite_id = "live";
        live.run_dir = root;
        live.artifacts_path = artifacts;
        live.pid = live_supervisor;
        exec::SuiteAppMetadata live_app{};
        live_app.app = "alpha";
        live_app.instance = "1";
        live_app.instance_dir = root + "/alpha/1";
        live_app.stop_grace_s = 0;
        live.apps.push_back(live_app);
        nlohmann::json live_json;
        exec::to_json(live_json, live);
        NX_CHECK(exec::write_json_file(live_dir + "/suite.json", live_json).has_value());
        NX_CHECK(exec::stop_suite(live_dir).has_value());
        NX_CHECK(file_exists(live_dir + "/suite-exit.json"));
        int live_status = 0;
        static_cast<void>(::waitpid(live_supervisor, &live_status, 0));
    }

    // A suite.json path that is a directory fails the supervisor's metadata
    // write and is reported back as a start failure.
    exec::SuiteManifest blocked = manifest;
    blocked.suite_id = "blocked";
    const std::string blocked_dir = root + "/.suite/blocked";
    std::filesystem::create_directories(blocked_dir + "/suite.json", error);
    std::string blocked_out;
    NX_CHECK(failed_with(exec::start_suite(blocked, blocked_out), core::ErrorCode::SystemError));

    remove_tree(directory);
    remove_tree(root);
    remove_tree(artifacts);
}

void test_relative_manifest_path() {
    const std::string root = unique_dir("relative_manifest");
    const std::string artifacts = unique_dir("relative_artifacts");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    exec::SuiteManifest manifest = two_app_manifest(root, artifacts);
    manifest.manifest_path = root + "/manifest.json";
    NX_CHECK(write_manifest(manifest.manifest_path, manifest));

    char previous[4096]{};
    NX_CHECK(::getcwd(previous, sizeof(previous)) != nullptr);
    NX_CHECK(::chdir(root.c_str()) == 0);
    exec::SuiteManifest loaded{};
    NX_CHECK(exec::load_suite_manifest("manifest.json", loaded).has_value());
    NX_CHECK(loaded.manifest_path == root + "/manifest.json");
    NX_CHECK(::chdir(previous) == 0);

    remove_tree(root);
    remove_tree(artifacts);
}

void test_env_affinity_and_log_level() {
    const std::string root = unique_dir("env");
    const std::string artifacts = unique_dir("env_artifacts");
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    exec::AppSpec spec =
        app_spec("envapp", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(spec).args = {"-c", "echo SUITE_ENV=$SUITE_ENV; "
                                     "echo LEVEL=$NEXIGEN_LOG_LEVEL; exec sleep 60"};
    first_config(spec).env = {{"SUITE_ENV", "visible"}, {"NEXIGEN_LOG_LEVEL", "debug"}};
    spec.cpus = core::CpuSet::from_core_mask(1ULL);
    spec.log_level = core::LogLevel::Error;
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(exec::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    exec::SuiteStatus status = read_status(suite_dir);
    const exec::SuiteAppStatus app = find_app(status, "envapp");
    NX_CHECK(wait_until(
        [&] {
            const std::string output = read_text(app.artifact_dir + "/stdout.log");
            return contains(output, "SUITE_ENV=visible") && contains(output, "LEVEL=error");
        },
        5000));

    const core::Result<nlohmann::json> resource =
        exec::read_json_file(app.instance_dir + "/resource.json");
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
        exec::read_json_file(app.instance_dir + "/settings/logging.json");
    NX_CHECK(logging_document.has_value());
    if (logging_document) {
        NX_CHECK((*logging_document)["initial_log_level"] == "error");
    }

    NX_CHECK(exec::stop_suite(suite_dir, 2).has_value());
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
        nlohmann::json document{
            {"log_root_directory", global}, {"app_name", "x"}, {"initial_log_level", "debug"}};
        NX_CHECK(exec::write_json_file(global + "/logging.json", document).has_value());
    }
    {
        nlohmann::json document{{"initial_log_level", "warning"}};
        NX_CHECK(exec::write_json_file(local + "/logging.json", document).has_value());
    }
    {
        nlohmann::json document{
            {"log_root_directory", app_global}, {"app_name", "x"}, {"initial_log_level", "fatal"}};
        NX_CHECK(exec::write_json_file(app_global + "/logging.json", document).has_value());
    }

    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    manifest.global_settings_path = global;
    exec::AppSpec inherited =
        app_spec("inherited", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(inherited).args = {"-c", "exec sleep 60"};
    inherited.local_settings_path = "local";
    exec::AppSpec overridden =
        app_spec("overridden", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(overridden).args = {"-c", "exec sleep 60"};
    overridden.local_settings_path = "local";
    overridden.log_level = core::LogLevel::Error;
    exec::AppSpec replaced =
        app_spec("replaced", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(replaced).args = {"-c", "exec sleep 60"};
    replaced.global_settings_path = app_global;
    manifest.apps.push_back(inherited);
    manifest.apps.push_back(overridden);
    manifest.apps.push_back(replaced);

    std::string suite_dir;
    NX_CHECK(exec::start_suite(manifest, suite_dir).has_value());
    if (file_exists(suite_dir + "/suite.json")) {
        const exec::SuiteStatus status = read_status(suite_dir);
        const auto inherited_logging = exec::read_json_file(
            find_app(status, "inherited").instance_dir + "/settings/logging.json");
        const auto overridden_logging = exec::read_json_file(
            find_app(status, "overridden").instance_dir + "/settings/logging.json");
        const auto replaced_logging = exec::read_json_file(
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
        NX_CHECK(exec::stop_suite(suite_dir, 2).has_value());
    }

    remove_tree(root);
    remove_tree(artifacts);
}

void test_not_impact_retries_new_attempts() {
    const std::string root = unique_dir("not_impact");
    const std::string artifacts = unique_dir("not_impact_artifacts");
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    exec::AppSpec spec =
        app_spec("crash", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(spec).args = {"-c", "exit 1"};
    first_config(spec).restart_attempts = 2;
    first_config(spec).restart_delay_s = 0;
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(exec::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    NX_CHECK(wait_until(
        [&] {
            const exec::SuiteStatus status = read_status(suite_dir);
            const exec::SuiteAppStatus app = find_app(status, "crash");
            return app.state == exec::AppState::Failed && app.attempt == 3;
        },
        15000));
    const exec::SuiteStatus status = read_status(suite_dir);
    NX_CHECK(status.state == exec::SuiteState::Running);
    NX_CHECK(find_app(status, "crash").state == exec::AppState::Failed);
    NX_CHECK(file_exists(exec::attempt_dir_path(artifacts, manifest.suite_id, "crash", "1", 1) +
                         "/stdout.log"));
    NX_CHECK(file_exists(exec::attempt_dir_path(artifacts, manifest.suite_id, "crash", "1", 2) +
                         "/stdout.log"));
    NX_CHECK(file_exists(exec::attempt_dir_path(artifacts, manifest.suite_id, "crash", "1", 3) +
                         "/stdout.log"));
    NX_CHECK(exec::stop_suite(suite_dir, 2).has_value());

    remove_tree(root);
    remove_tree(artifacts);
}

void test_failure_impact_restarts_suite() {
    const std::string root = unique_dir("failure");
    const std::string artifacts = unique_dir("failure_artifacts");
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    manifest.suite_restart_attempts = 1;
    manifest.suite_restart_delay_s = 0;
    exec::AppSpec crashing =
        app_spec("crashing", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Failure);
    first_config(crashing).args = {"-c", "exit 1"};
    first_config(crashing).restart_attempts = 5;
    first_config(crashing).restart_delay_s = 0;
    exec::AppSpec healthy =
        app_spec("healthy", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(healthy).args = {"-c", "exec sleep 60"};
    manifest.apps.push_back(crashing);
    manifest.apps.push_back(healthy);

    std::string suite_dir;
    NX_CHECK(exec::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    NX_CHECK(wait_until(
        [&] {
            return file_exists(suite_dir + "/suite-exit.json") &&
                   read_status(suite_dir).state == exec::SuiteState::Failed;
        },
        20000));

    const exec::SuiteStatus status = read_status(suite_dir);
    NX_CHECK(status.state == exec::SuiteState::Failed);
    NX_CHECK(file_exists(exec::attempt_dir_path(artifacts, manifest.suite_id, "healthy", "1", 2) +
                         "/stdout.log"));
    const core::Result<nlohmann::json> exit_document =
        exec::read_json_file(suite_dir + "/suite-exit.json");
    NX_CHECK(exit_document.has_value());
    if (exit_document) {
        NX_CHECK((*exit_document)["state"] == "failed");
        NX_CHECK((*exit_document)["reason"] == "restart_exhausted");
    }
    // Unrecoverable State (spec exec/process-types): the failure path must
    // terminate the still-running managed process and invoke the project hook.
    NX_CHECK(find_app(status, "healthy").state == exec::AppState::Stopped);
    const std::string log_path = artifacts + "/" + manifest.suite_id + "/execution_manager.jsonl";
    NX_CHECK(wait_until([&] { return contains(read_text(log_path), "event=unrecoverable_state"); },
                        5000));

    remove_tree(root);
    remove_tree(artifacts);
}

void test_clean_exit_is_final() {
    const std::string root = unique_dir("clean");
    const std::string artifacts = unique_dir("clean_artifacts");
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    exec::AppSpec spec =
        app_spec("clean", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(spec).args = {"-c", "exit 0"};
    spec.type = exec::ProcessType::Oneshot;
    first_config(spec).restart_attempts = 3;
    first_config(spec).restart_delay_s = 0;
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(exec::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    NX_CHECK(wait_until(
        [&] {
            const exec::SuiteAppStatus app = find_app(read_status(suite_dir), "clean");
            return app.state == exec::AppState::Exited;
        },
        10000));
    const exec::SuiteStatus status = read_status(suite_dir);
    NX_CHECK(status.state == exec::SuiteState::Running);
    NX_CHECK(find_app(status, "clean").attempt == 1);
    NX_CHECK(find_app(status, "clean").state == exec::AppState::Exited);
    NX_CHECK(exec::stop_suite(suite_dir, 2).has_value());

    remove_tree(root);
    remove_tree(artifacts);
}

void test_daemon_exit_is_unexpected() {
    const std::string root = unique_dir("daemon_exit");
    const std::string artifacts = unique_dir("daemon_exit_artifacts");
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    exec::AppSpec spec =
        app_spec("daemon", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(spec).args = {"-c", "exit 0"};
    spec.type = exec::ProcessType::Daemon;
    first_config(spec).restart_attempts = 0;
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(exec::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    // A daemon exiting 0 without a stop request is an unexpected
    // self-termination, not a normal exit.
    NX_CHECK(wait_until(
        [&] { return find_app(read_status(suite_dir), "daemon").state == exec::AppState::Failed; },
        10000));
    const exec::SuiteStatus status = read_status(suite_dir);
    NX_CHECK(find_app(status, "daemon").state == exec::AppState::Failed);
    NX_CHECK(find_app(status, "daemon").attempt == 1);
    NX_CHECK(exec::stop_suite(suite_dir, 2).has_value());

    remove_tree(root);
    remove_tree(artifacts);
}

void test_execution_state_tracker() {
    exec::ExecutionStateTracker tracker;
    tracker.register_process("sensor", exec::ProcessType::Daemon);
    tracker.register_process("setup", exec::ProcessType::Oneshot);
    NX_CHECK(tracker.process_count() == 2U);
    NX_CHECK(!tracker.ready("sensor"));
    NX_CHECK(tracker.state("sensor") == exec::AppState::Unknown);
    NX_CHECK(!tracker.run_confirmed());

    // A daemon that never reports stays non-running; a oneshot does not report.
    NX_CHECK(!tracker.report_execution_state("setup", exec::AppState::Running));
    NX_CHECK(!tracker.report_execution_state("missing", exec::AppState::Running));
    NX_CHECK(tracker.state("setup") == exec::AppState::Unknown);

    // A daemon reporting Run reaches running but the oneshot still blocks.
    tracker.on_launch("sensor");
    NX_CHECK(tracker.report_execution_state("sensor", exec::AppState::Running));
    NX_CHECK(tracker.ready("sensor"));
    NX_CHECK(tracker.state("sensor") == exec::AppState::Running);
    NX_CHECK(!tracker.run_confirmed());

    NX_CHECK(tracker.on_exit("setup", 0));
    NX_CHECK(tracker.state("setup") == exec::AppState::Exited);
    NX_CHECK(tracker.run_confirmed());
    const std::vector<std::string> bound{"sensor", "setup"};
    NX_CHECK(tracker.run_confirmed(bound));

    // A daemon exiting without a stop request is a failure.
    NX_CHECK(!tracker.on_exit("sensor", 0));
    NX_CHECK(tracker.state("sensor") == exec::AppState::Failed);
    NX_CHECK(!tracker.run_confirmed());

    // A requested stop resets readiness; re-registration keeps the record.
    tracker.on_stop("sensor");
    NX_CHECK(tracker.state("sensor") == exec::AppState::Unknown);
    tracker.register_process("sensor", exec::ProcessType::Daemon);
    NX_CHECK(tracker.process_count() == 2U);

    // A relaunch resets a stale `Exited` oneshot so the new launch must exit
    // again before the switch can be confirmed.
    NX_CHECK(tracker.state("setup") == exec::AppState::Exited);
    tracker.on_start("setup");
    NX_CHECK(tracker.state("setup") == exec::AppState::Unknown);
    NX_CHECK(!tracker.run_confirmed());
    tracker.on_launch("sensor");
    NX_CHECK(tracker.report_execution_state("sensor", exec::AppState::Running));
    NX_CHECK(tracker.on_exit("setup", 0));
    NX_CHECK(tracker.run_confirmed());

    // A non-zero oneshot exit is a failure too.
    tracker.register_process("setup2", exec::ProcessType::Oneshot);
    NX_CHECK(!tracker.on_exit("setup2", 3));
    NX_CHECK(tracker.state("setup2") == exec::AppState::Failed);
    NX_CHECK(std::format("{}", tracker) == tracker.to_string());

    // Reporting and non-reporting are independent of termination behavior.
    tracker.register_process("reporting_daemon", exec::ProcessType::Daemon, true);
    tracker.on_launch("reporting_daemon");
    NX_CHECK(tracker.process_state("reporting_daemon") == exec::ProcessState::Starting);
    NX_CHECK(tracker.report_execution_state("reporting_daemon", exec::AppState::Running));
    NX_CHECK(tracker.process_state("reporting_daemon") == exec::ProcessState::Running);

    tracker.register_process("nonreporting_daemon", exec::ProcessType::Daemon, false);
    tracker.on_launch("nonreporting_daemon");
    NX_CHECK(tracker.process_state("nonreporting_daemon") == exec::ProcessState::Running);
    NX_CHECK(tracker.state("nonreporting_daemon") == exec::AppState::Running);
    NX_CHECK(!tracker.report_execution_state("nonreporting_daemon", exec::AppState::Running));

    tracker.on_stop("reporting_daemon");
    NX_CHECK(tracker.process_state("reporting_daemon") == exec::ProcessState::Terminating);
    tracker.on_reaped("reporting_daemon");
    NX_CHECK(tracker.process_state("reporting_daemon") == exec::ProcessState::Terminated);
    NX_CHECK(tracker.state("reporting_daemon") == exec::AppState::Exited);
    NX_CHECK(!tracker.on_exit("nonreporting_daemon", 0));
    NX_CHECK(tracker.process_state("nonreporting_daemon") == exec::ProcessState::Terminated);

    exec::StateDependentStartupConfig config;
    config.state = "Run";
    tracker.register_process("configured_daemon", exec::ProcessType::Daemon, false, &config, 2);
    tracker.on_launch("configured_daemon", &config, 2);
    NX_CHECK(tracker.config("configured_daemon") == &config);
    NX_CHECK(tracker.attempt("configured_daemon") == 2U);
    NX_CHECK(std::format("{}", tracker) == tracker.to_string());
}

void test_unrecoverable_state() {
    exec::UnrecoverableState unrecoverable;
    int terminations = 0;
    int project_hooks = 0;
    unrecoverable.set_process_terminator([&] { ++terminations; });
    unrecoverable.set_project_hook([&] { ++project_hooks; });
    NX_CHECK(unrecoverable.active());
    NX_CHECK(!unrecoverable.entered());

    // An unresolvable failure terminates all managed processes and invokes the
    // project-specific hook exactly once.
    unrecoverable.enter();
    NX_CHECK(unrecoverable.entered());
    NX_CHECK(!unrecoverable.active());
    NX_CHECK(terminations == 1);
    NX_CHECK(project_hooks == 1);

    // Re-entering stops further action: the manager does not act again.
    unrecoverable.enter();
    NX_CHECK(terminations == 1);
    NX_CHECK(project_hooks == 1);
    NX_CHECK(std::format("{}", unrecoverable) == unrecoverable.to_string());
    NX_CHECK(unrecoverable.to_string().find("entered=true") != std::string::npos);

    // A state without installed hooks still latches cleanly.
    exec::UnrecoverableState bare;
    bare.enter();
    NX_CHECK(bare.entered());
    NX_CHECK(!bare.active());
}

void test_execution_manager_logging_and_affinity() {
    const std::string root = unique_dir("execution_manager_log");
    const std::string artifacts = unique_dir("execution_manager_log_artifacts");
    const int cpu = first_allowed_cpu();
    if (cpu < 0) {
        return;
    }
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    manifest.log_level = core::LogLevel::Debug;
    manifest.execution_manager_control_thread.set_cpus(core::CpuSet::from_core_mask(1ULL << cpu));
    manifest.execution_manager_logger_thread.set_cpus(core::CpuSet::from_core_mask(1ULL << cpu));
    exec::AppSpec spec =
        app_spec("logged", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(spec).args = {"-c", "exec sleep 60"};
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(exec::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    const std::string log_path = artifacts + "/" + manifest.suite_id + "/execution_manager.jsonl";
    NX_CHECK(wait_until([&] { return file_exists(log_path); }, 5000));
    NX_CHECK(
        wait_until([&] { return contains(read_text(log_path), "event=suite_started"); }, 5000));

    const exec::SuiteMetadata metadata = read_metadata(suite_dir);
    NX_CHECK(metadata.execution_manager_control_thread.cpus().count() == 1U);
    NX_CHECK(metadata.execution_manager_logger_thread.cpus().count() == 1U);
    NX_CHECK(metadata.pid > 0);

    cpu_set_t control_mask;
    CPU_ZERO(&control_mask);
    NX_CHECK(::sched_getaffinity(metadata.pid, sizeof(control_mask), &control_mask) == 0);
    NX_CHECK(CPU_COUNT(&control_mask) == 1);
    NX_CHECK(cpu_is_set(cpu, control_mask));

    const exec::SuiteStatus status = read_status(suite_dir);
    const exec::SuiteAppStatus app = find_app(status, "logged");
    NX_CHECK(app.state == exec::AppState::Running);
    const auto resource = exec::read_json_file(app.instance_dir + "/resource.json");
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

    NX_CHECK(exec::stop_suite(suite_dir, 2).has_value());
    NX_CHECK(
        wait_until([&] { return contains(read_text(log_path), "event=suite_stopped"); }, 5000));
    NX_CHECK(contains(read_text(log_path), "event=stop_requested"));

    remove_tree(root);
    remove_tree(artifacts);
}

void test_required_affinity_failure() {
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    const int cpu = first_allowed_cpu();
    NX_CHECK(cpu >= 0);
    if (cpu < 0) {
        return;
    }
    auto fail_affinity = [](nexigen::core::fault_injection::FaultPoint point) noexcept {
        return point == nexigen::core::fault_injection::FaultPoint::Thread_set_affinity;
    };

    for (int logger_case = 0; logger_case < 2; ++logger_case) {
        const std::string root = unique_dir("required_affinity");
        const std::string artifacts = unique_dir("required_affinity_artifacts");
        exec::SuiteManifest manifest = base_manifest(root, artifacts);
        core::ThreadSpec spec{};
        spec.set_cpus(core::CpuSet::from_core_mask(1ULL << cpu));
        spec.set_apply_mode(core::ThreadApplyMode::Required);
        if (logger_case == 0) {
            manifest.execution_manager_control_thread = spec;
        } else {
            manifest.execution_manager_logger_thread = spec;
        }
        exec::AppSpec spec_app = app_spec("must_not_start", "1", "/bin/sh",
                                          exec::ImpactOnAppSuiteOperability::Not_impact);
        first_config(spec_app).args = {"-c", "exec sleep 60"};
        manifest.apps.push_back(spec_app);

        nexigen::core::fault_injection::set_fault_hook(fail_affinity);
        std::string suite_dir;
        const core::Status started = exec::start_suite(manifest, suite_dir);
        nexigen::core::fault_injection::set_fault_hook(nullptr);
        NX_CHECK(!started.has_value());
        NX_CHECK(started.error() == core::ErrorCode::SystemError);
        NX_CHECK(!file_exists(suite_dir + "/suite.json"));
        remove_tree(root);
        remove_tree(artifacts);
    }
#else
    std::printf("required_affinity_failure: fault injection disabled; skipped\n");
#endif
}

void test_invalid_affinity_nonfatal() {
    const std::string root = unique_dir("bad_affinity");
    const std::string artifacts = unique_dir("bad_affinity_artifacts");
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    manifest.execution_manager_control_thread.set_cpus(invalid_cpu_set());
    manifest.execution_manager_logger_thread.set_cpus(invalid_cpu_set());
    exec::AppSpec spec =
        app_spec("bad_pin", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(spec).args = {"-c", "exec sleep 60"};
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(exec::start_suite(manifest, suite_dir).has_value());
    if (file_exists(suite_dir + "/suite.json")) {
        NX_CHECK(read_status(suite_dir).state == exec::SuiteState::Running);
        NX_CHECK(exec::stop_suite(suite_dir, 2).has_value());
        NX_CHECK(read_status(suite_dir).state == exec::SuiteState::Stopped);
    }
    remove_tree(root);
    remove_tree(artifacts);
}

void test_setup_failure() {
    const std::string root = unique_dir("setup");
    const std::string artifacts = unique_dir("setup_artifacts");
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    exec::AppSpec spec =
        app_spec("setup", "1", "/bin/true", exec::ImpactOnAppSuiteOperability::Not_impact);
    manifest.apps.push_back(spec);

    const app::RunIdentity identity{manifest.run_id, manifest.run_dir, "1", root + "/setup/1"};
    exec::StartOptions options{};
    options.artifact_dir =
        "/proc/nexigen_suite_denied_" + std::to_string(static_cast<long long>(::getpid()));
    exec::ExecutionManager execution_manager;
    NX_CHECK(failed_with(execution_manager.start(identity, "setup", {"/bin/true"}, 0, options),
                         core::ErrorCode::SystemError));
    NX_CHECK(!file_exists(root + "/setup/1/resource.json"));

    remove_tree(root);
    remove_tree(artifacts);
}

void test_forced_stop() {
    const std::string root = unique_dir("forced");
    const std::string artifacts = unique_dir("forced_artifacts");
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    exec::AppSpec spec =
        app_spec("forced", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    first_config(spec).args = {"-c", "exec sleep 60"};
    manifest.apps.push_back(spec);

    std::string suite_dir;
    NX_CHECK(exec::start_suite(manifest, suite_dir).has_value());
    if (!file_exists(suite_dir + "/suite.json")) {
        remove_tree(root);
        remove_tree(artifacts);
        return;
    }

    const auto metadata_document = exec::read_json_file(suite_dir + "/suite.json");
    NX_CHECK(metadata_document.has_value());
    if (metadata_document) {
        exec::SuiteMetadata metadata{};
        exec::from_json(*metadata_document, metadata);
        const exec::SuiteAppStatus app = find_app(read_status(suite_dir), "forced");
        const auto resource = exec::read_json_file(app.instance_dir + "/resource.json");
        NX_CHECK(resource.has_value());
        if (resource) {
            const std::int32_t child_pid = (*resource)["pid"].get<std::int32_t>();
            NX_CHECK(::kill(metadata.pid, SIGKILL) == 0);
            NX_CHECK(exec::stop_suite(suite_dir, 0).has_value());
            NX_CHECK(file_exists(suite_dir + "/suite-exit.json"));
            const auto exit_document = exec::read_json_file(suite_dir + "/suite-exit.json");
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

void test_root_function_group_validation() {
    const std::string root = unique_dir("machinefg");
    const std::string artifacts = unique_dir("machinefg_artifacts");
    const std::string root2 = unique_dir("machinefg_root2");
    const std::string artifacts2 = unique_dir("machinefg_artifacts2");

    exec::SuiteManifest good = base_manifest(root, artifacts);
    exec::FunctionGroupConfig machine{};
    machine.name = "RootFG";
    machine.states = {"Off", "Startup", "Verify", "Run", "Shutdown", "Restart"};
    machine.initial_state = "Startup";
    good.function_groups.push_back(machine);
    exec::AppSpec platform =
        app_spec("platform_init", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    platform.function_group = "RootFG";
    platform.reporting = false;
    platform.type = exec::ProcessType::Oneshot;
    first_config(platform).state = "Startup";
    first_config(platform).args = {"-c", "exit 0"};
    good.apps.push_back(std::move(platform));
    NX_CHECK(exec::validate_suite_manifest(good).has_value());

    exec::SuiteManifest missing = good;
    missing.function_groups.back().states = {"Off", "Startup", "Run", "Shutdown", "Restart"};
    NX_CHECK(failed_with(exec::validate_suite_manifest(missing), core::ErrorCode::InvalidArgument));

    exec::SuiteManifest bad_initial = good;
    bad_initial.function_groups.back().initial_state = "Run";
    NX_CHECK(
        failed_with(exec::validate_suite_manifest(bad_initial), core::ErrorCode::InvalidArgument));

    exec::SuiteManifest legacy = base_manifest(root2, artifacts2);
    legacy.apps.push_back(
        app_spec("legacy_app", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact));
    first_config(legacy.apps.front()).args = {"-c", "exec sleep 60"};
    NX_CHECK(exec::validate_suite_manifest(legacy).has_value());
}

void test_process_lifecycle_serialization() {
    for (const auto value :
         {exec::ProcessLifecycleState::Running, exec::ProcessLifecycleState::Terminating,
          exec::ProcessLifecycleState::Terminated}) {
        const std::string_view text = exec::to_string(value);
        exec::ProcessLifecycleState parsed = static_cast<exec::ProcessLifecycleState>(0xff);
        NX_CHECK(exec::from_string(text, parsed));
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == text);
        nlohmann::json json;
        exec::to_json(json, value);
        exec::from_json(json, parsed);
        NX_CHECK(parsed == value);
    }
    NX_CHECK(exec::to_string(static_cast<exec::ProcessLifecycleState>(0x7f)) == "Unknown");

    exec::ProcessLifecycleEvent event{};
    std::snprintf(event.app, sizeof(event.app), "%s", "alpha");
    event.state = exec::ProcessLifecycleState::Terminating;
    NX_CHECK(event.to_string().find("alpha") != std::string::npos);
    NX_CHECK(std::format("{}", event) == event.to_string());
    nlohmann::json json;
    exec::to_json(json, event);
    exec::ProcessLifecycleEvent parsed{};
    exec::from_json(json, parsed);
    NX_CHECK(std::string(parsed.app) == "alpha");
    NX_CHECK(parsed.state == exec::ProcessLifecycleState::Terminating);

    nlohmann::json hostile = "not-an-object";
    exec::from_json(hostile, parsed);
    NX_CHECK(std::string(parsed.app) == "alpha");
    NX_CHECK(parsed.state == exec::ProcessLifecycleState::Terminating);
}

void test_platform_controls_serialization() {
    exec::ResourceGroupConfig group;
    group.name = "rt";
    group.memory_limit_mb = 64;
    group.working_dir = "/tmp/opencode/rt";
    static_cast<void>(group.cpus.add(0));
    NX_CHECK(group.to_string().find("rt") != std::string::npos);
    nlohmann::json json;
    exec::to_json(json, group);
    exec::ResourceGroupConfig parsed;
    exec::from_json(json, parsed);
    NX_CHECK(parsed.name == "rt" && parsed.memory_limit_mb == 64);
    NX_CHECK(std::format("{}", group) == group.to_string());

    exec::TrustedPlatformConfig trusted;
    trusted.require_executable_digest = true;
    exec::to_json(json, trusted);
    exec::TrustedPlatformConfig trusted_parsed;
    exec::from_json(json, trusted_parsed);
    NX_CHECK(trusted_parsed.require_executable_digest);
    NX_CHECK(std::format("{}", trusted) == trusted.to_string());

    exec::FunctionGroupPortMapping mapping;
    mapping.requester_id = "sm";
    mapping.function_group = "default";
    exec::to_json(json, mapping);
    exec::FunctionGroupPortMapping mapping_parsed;
    exec::from_json(json, mapping_parsed);
    NX_CHECK(mapping_parsed.requester_id == "sm");
    NX_CHECK(mapping_parsed.function_group == "default");
    NX_CHECK(std::format("{}", mapping) == mapping.to_string());

    NX_CHECK(exec::is_sha256_hex(std::string(64, 'a')));
    NX_CHECK(!exec::is_sha256_hex("short"));
    NX_CHECK(!exec::is_sha256_hex(std::string(64, 'z')));
}

void test_platform_controls_validation() {
    const std::string root = unique_dir("p2_validation");
    const std::string artifacts = unique_dir("p2_validation_artifacts");
    exec::SuiteManifest manifest = base_manifest(root, artifacts);
    exec::ResourceGroupConfig group;
    group.name = "rt";
    group.memory_limit_mb = 64;
    group.working_dir = root + "/rt";
    manifest.resource_groups.push_back(group);
    exec::AppSpec app =
        app_spec("p2_app", "1", "/bin/true", exec::ImpactOnAppSuiteOperability::Not_impact);
    app.resource_group = "rt";
    first_config(app).args = {};
    manifest.apps.push_back(app);
    manifest.function_group_port_mapping.push_back({"sm", "default"});
    NX_CHECK(exec::validate_suite_manifest(manifest).has_value());

    exec::SuiteManifest duplicate = manifest;
    duplicate.resource_groups.push_back(group);
    NX_CHECK(
        failed_with(exec::validate_suite_manifest(duplicate), core::ErrorCode::InvalidArgument));

    exec::SuiteManifest missing = manifest;
    missing.apps.front().resource_group = "missing";
    NX_CHECK(failed_with(exec::validate_suite_manifest(missing), core::ErrorCode::InvalidArgument));

    exec::SuiteManifest bad_mapping = manifest;
    bad_mapping.function_group_port_mapping.front().function_group = "missing";
    NX_CHECK(
        failed_with(exec::validate_suite_manifest(bad_mapping), core::ErrorCode::InvalidArgument));

    exec::SuiteManifest trusted = manifest;
    trusted.trusted_platform.require_executable_digest = true;
    trusted.apps.front().executable_digest = std::string(64, 'a');
    NX_CHECK(exec::validate_suite_manifest(trusted).has_value());
    trusted.apps.front().command = "relative";
    NX_CHECK(failed_with(exec::validate_suite_manifest(trusted), core::ErrorCode::InvalidArgument));
}

void test_deterministic_client() {
    exec::DeterministicConfig config;
    config.cycle_period_ns = 1'000'000;
    config.seed = 42;
    NX_CHECK(config.to_string().find("42") != std::string::npos);
    nlohmann::json json;
    exec::to_json(json, config);
    exec::DeterministicConfig parsed;
    exec::from_json(json, parsed);
    NX_CHECK(parsed.cycle_period_ns == config.cycle_period_ns && parsed.seed == 42);

    exec::DeterministicClient first(config);
    exec::DeterministicClient second(config);
    for (int i = 0; i < 5; ++i) {
        NX_CHECK(first.random_u64() == second.random_u64());
    }
    exec::DeterministicConfig other = config;
    other.seed = 43;
    exec::DeterministicClient divergent(other);
    NX_CHECK(divergent.random_u64() != first.random_u64());

    NX_CHECK(first.cycle_index() == 0U);
    NX_CHECK(first.wait_for_next_cycle() == 1U);
    NX_CHECK(first.wait_for_next_cycle() == 2U);
    NX_CHECK(first.cycle_time_ns() > 0U);
    NX_CHECK(first.to_string().find("cycle=2") != std::string::npos);
    NX_CHECK(std::format("{}", first) == first.to_string());
}

void test_arxml_manifest() {
    NX_CHECK(exec::ManifestAccessor::detect("manifest.arxml", "") == exec::ManifestFormat::Arxml);
    NX_CHECK(exec::ManifestAccessor::detect("manifest.json", "{}") == exec::ManifestFormat::Json);
    NX_CHECK(exec::ManifestAccessor::detect("manifest.txt", "  <SUITE-MANIFEST/>") ==
             exec::ManifestFormat::Arxml);
    NX_CHECK(exec::to_string(exec::ManifestFormat::Arxml) == "Arxml");
    nlohmann::json format_json;
    exec::to_json(format_json, exec::ManifestFormat::Json);
    exec::ManifestFormat format_parsed = exec::ManifestFormat::Arxml;
    exec::from_json(format_json, format_parsed);
    NX_CHECK(format_parsed == exec::ManifestFormat::Json);

    const std::string root = unique_dir("arxml");
    const std::string artifacts = unique_dir("arxml_artifacts");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    std::filesystem::create_directories(artifacts, error);
    const std::string path = root + "/manifest.arxml";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output
        << "<?xml version=\"1.0\"?>\n"
           "<SUITE-MANIFEST xmlns=\"urn:nexigen:test\">\n"
           "  <VERSION>1</VERSION>\n"
           "  <SUITE-ID>arxml_suite</SUITE-ID>\n"
           "  <RUN-ID>run-arxml</RUN-ID>\n"
           "  <RUN-DIR>"
        << root
        << "</RUN-DIR>\n"
           "  <ARTIFACTS-PATH>"
        << artifacts
        << "</ARTIFACTS-PATH>\n"
           "  <FUNCTION-GROUPS>\n"
           "    <FUNCTION-GROUP>\n"
           "      <NAME>demo</NAME>\n"
           "      <STATES><STATE>Off</STATE><STATE>Running</STATE></STATES>\n"
           "      <INITIAL-STATE>Running</INITIAL-STATE>\n"
           "    </FUNCTION-GROUP>\n"
           "  </FUNCTION-GROUPS>\n"
           "  <APPLICATIONS>\n"
           "    <APPLICATION>\n"
           "      <APP>arxml_app</APP>\n"
           "      <INSTANCE>1</INSTANCE>\n"
           "      <COMMAND>/bin/sh</COMMAND>\n"
           "      <FUNCTION-GROUP>demo</FUNCTION-GROUP>\n"
           "      <REPORTING>false</REPORTING>\n"
           "      <IMPACT-ON-APP-SUITE-OPERABILITY>not_impact</IMPACT-ON-APP-SUITE-OPERABILITY>\n"
           "      <STARTUP-CONFIGS>\n"
           "        <STARTUP-CONFIG>\n"
           "          <STATE>Running</STATE>\n"
           "          <ARGS><ARG>-c</ARG><ARG>exit 0</ARG></ARGS>\n"
           "        </STARTUP-CONFIG>\n"
           "      </STARTUP-CONFIGS>\n"
           "    </APPLICATION>\n"
           "  </APPLICATIONS>\n"
           "</SUITE-MANIFEST>\n";
    output.close();

    exec::SuiteManifest loaded{};
    NX_CHECK(exec::load_suite_manifest_auto(path, loaded).has_value());
    NX_CHECK(loaded.suite_id == "arxml_suite");
    NX_CHECK(loaded.function_groups.size() == 1U);
    NX_CHECK(loaded.apps.size() == 1U);
    NX_CHECK(loaded.apps.front().app == "arxml_app");
    NX_CHECK(!loaded.apps.front().reporting);
    NX_CHECK(!loaded.manifest_path.empty());

    const std::string bad_path = root + "/bad.arxml";
    std::ofstream bad(bad_path, std::ios::binary | std::ios::trunc);
    bad << "<SUITE-MANIFEST><VERSION>1</VERSION></WRONG>";
    bad.close();
    exec::SuiteManifest rejected{};
    NX_CHECK(failed_with(exec::load_suite_manifest_auto(bad_path, rejected),
                         core::ErrorCode::InvalidArgument));

    remove_tree(root);
    remove_tree(artifacts);
}

void test_machine_manifest() {
    const std::string root = unique_dir("machine");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const std::string path = root + "/machine.arxml";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "<?xml version=\"1.0\"?>\n"
              "<MACHINE-MANIFEST>\n"
              "  <MACHINES>\n"
              "    <MACHINE>\n"
              "      <NAME>Host</NAME>\n"
              "      <FUNCTION-GROUPS>\n"
              "        <FUNCTION-GROUP>\n"
              "          <NAME>demo</NAME>\n"
              "          <ID>FG_Demo</ID>\n"
              "          <STATES><STATE>Off</STATE><STATE>Running</STATE></STATES>\n"
              "        </FUNCTION-GROUP>\n"
              "      </FUNCTION-GROUPS>\n"
              "      <PROCESSES>\n"
              "        <PROCESS>\n"
              "          <NAME>arxml_app</NAME>\n"
              "          <EXECUTABLE>/bin/sh</EXECUTABLE>\n"
              "          <FUNCTION-GROUP-REF>/Host/FunctionGroups/FG_Demo</FUNCTION-GROUP-REF>\n"
              "          <ARGS><ARG>-c</ARG><ARG>exit 0</ARG></ARGS>\n"
              "        </PROCESS>\n"
              "      </PROCESSES>\n"
              "    </MACHINE>\n"
              "  </MACHINES>\n"
              "</MACHINE-MANIFEST>\n";
    output.close();

    exec::MachineManifest machine{};
    NX_CHECK(exec::load_machine_manifest_auto(path, machine).has_value());
    NX_CHECK(machine.machines.size() == 1U);
    NX_CHECK(machine.machines.front().processes.front().function_group == "demo");
    NX_CHECK(machine.find_machine("Host") != nullptr);
    NX_CHECK(machine.machines.front().find_function_group("FG_Demo") != nullptr);

    exec::SuiteManifest execution = base_manifest(root + "/run", root + "/artifacts");
    execution.function_groups.front().name = "demo";
    exec::AppSpec app =
        app_spec("arxml_app", "1", "/bin/sh", exec::ImpactOnAppSuiteOperability::Not_impact);
    app.function_group = "demo";
    execution.apps.push_back(std::move(app));
    NX_CHECK(exec::validate_execution_against_machine(execution, machine).has_value());

    exec::SuiteManifest mismatch = execution;
    mismatch.apps.front().command = "/bin/true";
    NX_CHECK(failed_with(exec::validate_execution_against_machine(mismatch, machine),
                         core::ErrorCode::InvalidArgument));

    machine.machines.front().processes.front().function_group_ref = "/Host/FunctionGroups/Missing";
    NX_CHECK(failed_with(machine.resolve_references(), core::ErrorCode::InvalidArgument));

    exec::MachineManifest duplicate;
    duplicate.machines.push_back(machine.machines.front());
    duplicate.machines.push_back(machine.machines.front());
    NX_CHECK(failed_with(duplicate.validate(), core::ErrorCode::InvalidArgument));

    remove_tree(root);
}

/// @brief Stand-in factory proving the COM-free slot round-trips.
[[nodiscard]] std::unique_ptr<exec::ControlHost> dummy_control_factory(const exec::SuiteManifest&,
                                                                       const app::RunIdentity&) {
    return nullptr;
}

void test_control_host_factory_slot() {
    NX_CHECK(exec::control_host_factory() == nullptr);
    exec::set_control_host_factory(&dummy_control_factory);
    NX_CHECK(exec::control_host_factory() == &dummy_control_factory);
    NX_CHECK(exec::control_host_factory()(exec::SuiteManifest{}, app::RunIdentity{}) == nullptr);
    exec::set_control_host_factory(nullptr);
    NX_CHECK(exec::control_host_factory() == nullptr);
}

} // namespace

int main() {
    run_suite("control_host_factory", test_control_host_factory_slot);
    run_suite("machinefg_validation", test_root_function_group_validation);
    run_suite("process_lifecycle", test_process_lifecycle_serialization);
    run_suite("platform_controls_serialization", test_platform_controls_serialization);
    run_suite("platform_controls_validation", test_platform_controls_validation);
    run_suite("deterministic_client", test_deterministic_client);
    run_suite("arxml_manifest", test_arxml_manifest);
    run_suite("machine_manifest", test_machine_manifest);
    run_suite("enums_serialization", test_enums_and_serialization);
    run_suite("manifest_validation", test_manifest_validation);
    run_suite("manifest_digest", test_manifest_digest);
    run_suite("settings_digests", test_settings_digests);
    run_suite("app_credentials", test_app_credentials);
    run_suite("start_status_stop", test_start_status_stop);
    run_suite("initial_state", test_initial_state_selection);
    run_suite("suite_edge_paths", test_suite_edge_paths);
    run_suite("relative_manifest", test_relative_manifest_path);
    run_suite("env_affinity_level", test_env_affinity_and_log_level);
    run_suite("settings_precedence", test_settings_precedence);
    run_suite("not_impact_retries", test_not_impact_retries_new_attempts);
    run_suite("failure_restarts_suite", test_failure_impact_restarts_suite);
    run_suite("clean_exit_is_final", test_clean_exit_is_final);
    run_suite("daemon_exit_unexpected", test_daemon_exit_is_unexpected);
    run_suite("execution_state_tracker", test_execution_state_tracker);
    run_suite("unrecoverable_state", test_unrecoverable_state);
    run_suite("execution_manager_logging_affinity", test_execution_manager_logging_and_affinity);
    run_suite("invalid_affinity", test_invalid_affinity_nonfatal);
    run_suite("required_affinity_failure", test_required_affinity_failure);
    run_suite("setup_failure", test_setup_failure);
    run_suite("forced_stop", test_forced_stop);

    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
