// Shared harness and fixtures for the nexigen::hub test suite.
//
// Split per entity (model, discovery, streaming, web assets, HTTP server) so a
// change recompiles only the affected TU and a failure names its owner.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "../support/test_temp.hpp"

#include <nexigen/core/result.hpp>
#include <nexigen/core/detail/fault_injection.hpp>
#include <nexigen/hub/discovery.hpp>
#include <nexigen/hub/http_server.hpp>
#include <nexigen/hub/streaming.hpp>
#include <nexigen/hub/web_assets.hpp>
#include <nexigen/exec/app_suite.hpp>
#include <nexigen/exec/instance_lifecycle.hpp>

namespace core = nexigen::core;
namespace hub = nexigen::hub;
namespace exec = nexigen::exec;

inline int g_checks = 0;
inline int g_failures = 0;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

inline void run_suite(const char* name, void (*body)()) {
    const int before_checks = g_checks;
    const int before_failures = g_failures;
    body();
    std::printf("[%-24s] %d checks, %d failed\n", name, g_checks - before_checks,
                g_failures - before_failures);
}

inline std::string unique_dir(std::string_view tag) {
    static int counter = 0;
    ++counter;
    return "/tmp/opencode/nexigen_hub_" + std::string(tag) + "_" +
           std::to_string(static_cast<long long>(::getpid())) + "_" + std::to_string(counter);
}

inline void remove_tree(const std::string& path) {
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(path, error));
}

inline void make_dirs(const std::string& path) {
    std::error_code error;
    static_cast<void>(std::filesystem::create_directories(path, error));
}

inline void write_text(const std::string& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

inline std::string read_text(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

inline bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

struct DiagnosticCapture {
    mutable std::mutex mutex{};
    std::vector<std::string> messages{};
};

inline void capture_diagnostic(void* context, std::string_view message) noexcept {
    auto* capture = static_cast<DiagnosticCapture*>(context);
    const std::lock_guard<std::mutex> lock(capture->mutex);
    capture->messages.emplace_back(message);
}

inline bool diagnostic_contains(const DiagnosticCapture& capture, std::string_view needle) {
    const std::lock_guard<std::mutex> lock(capture.mutex);
    for (const std::string& message : capture.messages) {
        if (contains(message, needle)) {
            return true;
        }
    }
    return false;
}

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
inline core::fault_injection::FaultPoint g_hub_fault_point =
    core::fault_injection::FaultPoint::Hub_socket;
inline bool g_hub_fault_enabled = false;

inline bool hub_fault_hook(core::fault_injection::FaultPoint point) noexcept {
    return g_hub_fault_enabled && point == g_hub_fault_point;
}
#endif

template <class Predicate> bool wait_until(Predicate predicate, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

inline void write_json(const std::string& path, const nlohmann::json& document) {
    static_cast<void>(exec::write_json_file(path, document));
}

struct Fixture {
    std::string root{};
    std::string artifacts{};
    std::string suite_dir{};
    std::string instance_dir{};
    std::string attempt_dir{};

    explicit Fixture(std::string_view tag) {
        root = unique_dir(tag);
        // ExecutionManager-realistic layout (change fix-p0-critical-defects): the
        // artifacts tree lives inside the run root ("artifacts/"), or the
        // new containment contract would reject it.
        artifacts = root + "/artifacts";
        suite_dir = root + "/.suite/hil";
        instance_dir = root + "/alpha/1";
        attempt_dir = artifacts + "/hil/alpha/1/attempt-0001";
        make_dirs(suite_dir);
        make_dirs(instance_dir);
        make_dirs(attempt_dir);

        exec::SuiteMetadata metadata{};
        metadata.suite_id = "hil";
        metadata.run_id = "run-1";
        metadata.run_dir = root;
        metadata.artifacts_path = artifacts;
        metadata.pid = 1234;
        metadata.start_time_rfc3339 = "2026-11-01T00:00:00.000000000Z";
        metadata.start_time_mono_ns = 1;
        exec::SuiteAppMetadata app{};
        app.app = "alpha";
        app.instance = "1";
        app.instance_dir = instance_dir;
        metadata.apps.push_back(app);
        nlohmann::json metadata_json;
        exec::to_json(metadata_json, metadata);
        write_json(suite_dir + "/suite.json", metadata_json);

        exec::SuiteStatus status{};
        status.state = exec::SuiteState::Running;
        status.suite_id = "hil";
        status.run_dir = root;
        status.artifacts_path = artifacts;
        exec::SuiteAppStatus app_status{};
        app_status.app = "alpha";
        app_status.instance = "1";
        app_status.instance_dir = instance_dir;
        app_status.artifact_dir = attempt_dir;
        app_status.attempt = 1;
        app_status.state = exec::AppState::Running;
        status.apps.push_back(app_status);
        nlohmann::json status_json;
        exec::to_json(status_json, status);
        write_json(suite_dir + "/suite-state.json", status_json);

        write_text(attempt_dir + "/app.jsonl", "{\"a\":1}\n{\"a\":2}\n");
        write_text(attempt_dir + "/stdout.log", "hello\n");
        write_text(attempt_dir + "/stderr.log", "err\n");
        write_text(attempt_dir + "/metrics.jsonl", "");
        write_text(attempt_dir + "/traces.jsonl", "");
        write_text(attempt_dir + "/chrono.jsonl", "");
        write_text(artifacts + "/hil/execution_manager.jsonl", "{\"event\":\"suite_started\"}\n");
    }

    ~Fixture() {
        remove_tree(root); // artifacts live inside the root now
    }
};

inline std::string stream_id(const Fixture& fixture, std::string_view kind) {
    static_cast<void>(fixture);
    return "run-1:hil:alpha:1:1:" + std::string(kind);
}
