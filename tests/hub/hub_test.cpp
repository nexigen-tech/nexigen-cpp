// nexigen::hub tests — model, discovery, streaming, and HTTP/SSE.
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
#include <nexigen/hub/model.hpp>
#include <nexigen/hub/streaming.hpp>
#include <nexigen/hub/web_assets.hpp>
#include <nexigen/runner/app_suite.hpp>
#include <nexigen/runner/instance_lifecycle.hpp>

namespace core = nexigen::core;
namespace hub = nexigen::hub;
namespace runner = nexigen::runner;

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
    return "/tmp/opencode/nexigen_hub_" + std::string(tag) + "_" +
           std::to_string(static_cast<long long>(::getpid())) + "_" +
           std::to_string(counter);
}

void remove_tree(const std::string& path) {
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(path, error));
}

void make_dirs(const std::string& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
}

void write_text(const std::string& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
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

struct DiagnosticCapture {
    mutable std::mutex mutex{};
    std::vector<std::string> messages{};
};

void capture_diagnostic(void* context, std::string_view message) noexcept {
    auto* capture = static_cast<DiagnosticCapture*>(context);
    const std::lock_guard<std::mutex> lock(capture->mutex);
    capture->messages.emplace_back(message);
}

bool diagnostic_contains(const DiagnosticCapture& capture,
                         std::string_view needle) {
    const std::lock_guard<std::mutex> lock(capture.mutex);
    for (const std::string& message : capture.messages) {
        if (contains(message, needle)) {
            return true;
        }
    }
    return false;
}

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
core::detail::FaultPoint g_hub_fault_point =
    core::detail::FaultPoint::Hub_socket;
bool g_hub_fault_enabled = false;

bool hub_fault_hook(core::detail::FaultPoint point) noexcept {
    return g_hub_fault_enabled && point == g_hub_fault_point;
}
#endif

template <class Predicate>
bool wait_until(Predicate predicate, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

void write_json(const std::string& path, const nlohmann::json& document) {
    static_cast<void>(runner::write_json_file(path, document));
}

struct Fixture {
    std::string root{};
    std::string artifacts{};
    std::string suite_dir{};
    std::string instance_dir{};
    std::string attempt_dir{};

    explicit Fixture(std::string_view tag) {
        root = unique_dir(tag);
        artifacts = unique_dir(std::string(tag) + "_artifacts");
        suite_dir = root + "/.suite/hil";
        instance_dir = root + "/alpha/1";
        attempt_dir = artifacts + "/hil/alpha/1/attempt-0001";
        make_dirs(suite_dir);
        make_dirs(instance_dir);
        make_dirs(attempt_dir);

        runner::SuiteMetadata metadata{};
        metadata.suite_id = "hil";
        metadata.run_id = "run-1";
        metadata.run_dir = root;
        metadata.artifacts_path = artifacts;
        metadata.pid = 1234;
        metadata.start_time_rfc3339 = "2026-11-01T00:00:00.000000000Z";
        metadata.start_time_mono_ns = 1;
        runner::SuiteAppMetadata app{};
        app.app = "alpha";
        app.instance = "1";
        app.instance_dir = instance_dir;
        metadata.apps.push_back(app);
        nlohmann::json metadata_json;
        runner::to_json(metadata_json, metadata);
        write_json(suite_dir + "/suite.json", metadata_json);

        runner::SuiteStatus status{};
        status.state = runner::SuiteState::Running;
        status.suite_id = "hil";
        status.run_dir = root;
        status.artifacts_path = artifacts;
        runner::SuiteAppStatus app_status{};
        app_status.app = "alpha";
        app_status.instance = "1";
        app_status.instance_dir = instance_dir;
        app_status.artifact_dir = attempt_dir;
        app_status.attempt = 1;
        app_status.state = runner::AppState::Running;
        status.apps.push_back(app_status);
        nlohmann::json status_json;
        runner::to_json(status_json, status);
        write_json(suite_dir + "/suite-state.json", status_json);

        write_text(attempt_dir + "/app.jsonl", "{\"a\":1}\n{\"a\":2}\n");
        write_text(attempt_dir + "/stdout.log", "hello\n");
        write_text(attempt_dir + "/stderr.log", "err\n");
        write_text(attempt_dir + "/metrics.jsonl", "");
        write_text(attempt_dir + "/traces.jsonl", "");
        write_text(attempt_dir + "/chrono.jsonl", "");
        write_text(artifacts + "/hil/runner.jsonl", "{\"event\":\"suite_started\"}\n");
    }

    ~Fixture() {
        remove_tree(root);
        remove_tree(artifacts);
    }
};

std::string stream_id(const Fixture& fixture, std::string_view kind) {
    static_cast<void>(fixture);
    return "run-1:hil:alpha:1:1:" + std::string(kind);
}

void test_model_serialization() {
    for (const auto kind :
         {hub::StreamKind::App, hub::StreamKind::Metrics,
          hub::StreamKind::Traces, hub::StreamKind::Chrono,
          hub::StreamKind::Stdout, hub::StreamKind::Stderr,
          hub::StreamKind::Runner}) {
        hub::StreamKind parsed = static_cast<hub::StreamKind>(0xff);
        NX_CHECK(hub::from_string(hub::to_string(kind), parsed));
        NX_CHECK(parsed == kind);
        NX_CHECK(std::format("{}", kind) == hub::to_string(kind));
        nlohmann::json json;
        hub::to_json(json, kind);
        hub::from_json(json, parsed);
        NX_CHECK(parsed == kind);
    }

    hub::StreamRecord record{};
    record.stream_id = "run-1:hil:alpha:1:1:app";
    record.kind = hub::StreamKind::App;
    record.sequence = 7;
    record.received_at_rfc3339 = "2026-11-01T00:00:00.000000000Z";
    record.line = "hello";
    nlohmann::json json;
    hub::to_json(json, record);
    hub::StreamRecord round;
    hub::from_json(json, round);
    NX_CHECK(round.sequence == 7);
    NX_CHECK(round.line == "hello");
    NX_CHECK(round.kind == hub::StreamKind::App);

    hub::GapMarker gap{};
    gap.stream_id = record.stream_id;
    gap.dropped = 2;
    gap.first_missing_sequence = 3;
    gap.last_missing_sequence = 4;
    hub::to_json(json, gap);
    hub::GapMarker gap_round;
    hub::from_json(json, gap_round);
    NX_CHECK(gap_round.dropped == 2);
    NX_CHECK(gap_round.first_missing_sequence == 3);

    hub::HubTree tree{};
    hub::HubRun run{};
    run.run_key = "run-1";
    run.run_dir = "/run";
    hub::HubSuite suite{};
    suite.suite_id = "hil";
    suite.state = runner::SuiteState::Running;
    hub::HubApp app{};
    app.app = "alpha";
    app.state = runner::AppState::Running;
    hub::HubInstance instance{};
    instance.instance = "1";
    hub::HubAttempt attempt{};
    attempt.attempt = 1;
    hub::HubStream stream{};
    stream.id = record.stream_id;
    stream.kind = hub::StreamKind::App;
    attempt.streams.push_back(stream);
    instance.attempts.push_back(attempt);
    app.instances.push_back(instance);
    suite.apps.push_back(app);
    run.suites.push_back(suite);
    tree.runs.push_back(run);

    hub::to_json(json, tree);
    hub::HubTree round_tree;
    hub::from_json(json, round_tree);
    NX_CHECK(round_tree.runs.size() == 1);
    NX_CHECK(round_tree.runs[0].suites[0].apps[0].instances[0].attempts[0]
                 .streams[0]
                 .kind == hub::StreamKind::App);

    hub::HubTree hostile = round_tree;
    hub::from_json(nlohmann::json::array(), hostile);
    NX_CHECK(hostile.runs.size() == 1);

    NX_CHECK(!stream.to_string().empty());
    NX_CHECK(!attempt.to_string().empty());
    NX_CHECK(!instance.to_string().empty());
    NX_CHECK(!app.to_string().empty());
    NX_CHECK(!suite.to_string().empty());
    NX_CHECK(!run.to_string().empty());
    NX_CHECK(!tree.to_string().empty());
    NX_CHECK(!record.to_string().empty());
    NX_CHECK(!gap.to_string().empty());
    NX_CHECK(!std::format("{}", tree).empty());
    NX_CHECK(!std::format("{}", record).empty());

    // Standalone instances participate in the tree and in stream lookup.
    hub::HubTree standalone_tree{};
    hub::HubRun standalone_run{};
    standalone_run.run_key = "standalone";
    hub::HubInstance standalone_instance{};
    standalone_instance.instance = "2";
    standalone_instance.instance_dir = "/run/alpha/2";
    hub::HubAttempt standalone_attempt{};
    standalone_attempt.attempt = 1;
    standalone_attempt.streams.push_back(
        hub::HubStream{"standalone:app", hub::StreamKind::App, "/p"});
    standalone_instance.attempts.push_back(standalone_attempt);
    standalone_run.standalone_instances.push_back(standalone_instance);
    standalone_tree.runs.push_back(standalone_run);
    nlohmann::json standalone_json;
    hub::to_json(standalone_json, standalone_tree);
    hub::HubTree standalone_round;
    hub::from_json(standalone_json, standalone_round);
    NX_CHECK(standalone_round.runs[0].standalone_instances.size() == 1);
    NX_CHECK(hub::find_stream(standalone_tree, "standalone:app") != nullptr);
    NX_CHECK(hub::find_stream(standalone_tree, "missing") == nullptr);

    // Non-object and wrong-typed fields leave the existing values untouched.
    hub::HubStream bad_stream;
    bad_stream.id = "keep";
    hub::from_json(nlohmann::json::array(), bad_stream);
    hub::from_json(nlohmann::json{{"id", 5}, {"kind", 7}, {"path", false}},
                   bad_stream);
    NX_CHECK(bad_stream.id == "keep");

    hub::HubAttempt bad_attempt;
    bad_attempt.attempt = 9;
    hub::from_json(nlohmann::json::array(), bad_attempt);
    hub::from_json(nlohmann::json{{"attempt", "x"},
                                  {"artifact_dir", 4},
                                  {"streams", 3}},
                   bad_attempt);
    NX_CHECK(bad_attempt.attempt == 9);

    hub::HubInstance bad_instance;
    bad_instance.instance = "keep";
    hub::from_json(nlohmann::json::array(), bad_instance);
    hub::from_json(nlohmann::json{{"instance", 1},
                                  {"instance_dir", true},
                                  {"state", 4},
                                  {"attempts", 3}},
                   bad_instance);
    NX_CHECK(bad_instance.instance == "keep");

    hub::HubApp bad_app;
    bad_app.app = "keep";
    hub::from_json(nlohmann::json::array(), bad_app);
    hub::from_json(nlohmann::json{{"app", 1}, {"state", false},
                                  {"instances", 2}},
                   bad_app);
    NX_CHECK(bad_app.app == "keep");

    hub::HubSuite bad_suite;
    bad_suite.suite_id = "keep";
    hub::from_json(nlohmann::json::array(), bad_suite);
    hub::from_json(nlohmann::json{{"suite_id", 1},
                                  {"suite_dir", 2},
                                  {"artifacts_path", 3},
                                  {"state", 4},
                                  {"suite_restarts_used",
                                   std::uint64_t{4294967296ULL}},
                                  {"reason", 5},
                                  {"runner_log", 6},
                                  {"apps", 7}},
                   bad_suite);
    NX_CHECK(bad_suite.suite_id == "keep");

    hub::HubRun bad_run;
    bad_run.run_key = "keep";
    hub::from_json(nlohmann::json::array(), bad_run);
    hub::from_json(nlohmann::json{{"run_key", 1}, {"run_dir", 2},
                                  {"suites", 3},
                                  {"standalone_instances", 4}},
                   bad_run);
    NX_CHECK(bad_run.run_key == "keep");

    hub::HubTree bad_tree;
    bad_tree.runs.push_back(run);
    hub::from_json(nlohmann::json{{"runs", 3}}, bad_tree);
    NX_CHECK(bad_tree.runs.size() == 1);

    hub::StreamRecord bad_record;
    bad_record.sequence = 7;
    hub::from_json(nlohmann::json::array(), bad_record);
    hub::from_json(nlohmann::json{{"stream_id", 1},
                                  {"kind", 2},
                                  {"sequence", "x"},
                                  {"received_at_rfc3339", 3},
                                  {"line", 4},
                                  {"truncated", "no"}},
                   bad_record);
    NX_CHECK(bad_record.sequence == 7);

    hub::GapMarker bad_gap;
    bad_gap.dropped = 7;
    hub::from_json(nlohmann::json::array(), bad_gap);
    hub::from_json(nlohmann::json{{"stream_id", 1},
                                  {"dropped", "x"},
                                  {"first_missing_sequence", false},
                                  {"last_missing_sequence", 3}},
                   bad_gap);
    NX_CHECK(bad_gap.dropped == 7);
}

void test_discovery() {
    Fixture fixture("discovery");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    const hub::HubTree& tree = discovery.tree();
    NX_CHECK(tree.runs.size() == 1);
    if (tree.runs.empty()) return;
    NX_CHECK(tree.runs[0].suites.size() == 1);
    if (tree.runs[0].suites.empty()) return;
    const hub::HubSuite& suite = tree.runs[0].suites[0];
    NX_CHECK(suite.suite_id == "hil");
    NX_CHECK(suite.state == runner::SuiteState::Running);
    NX_CHECK(suite.apps.size() == 1);
    NX_CHECK(suite.apps[0].instances.size() == 1);
    NX_CHECK(suite.apps[0].instances[0].attempts.size() == 1);
    NX_CHECK(suite.runner_log.id == "run-1:hil:suite:runner:0:runner");
    NX_CHECK(suite.runner_log.path == fixture.artifacts + "/hil/runner.jsonl");
    NX_CHECK(hub::all_streams(tree).size() == 7);
    NX_CHECK(hub::find_stream(tree, stream_id(fixture, "stdout")) != nullptr);

    // A new attempt is discovered on rescan and gets its own streams.
    make_dirs(fixture.artifacts + "/hil/alpha/1/attempt-0002");
    NX_CHECK(discovery.rescan().has_value());
    const auto& instances = discovery.tree().runs[0].suites[0].apps[0].instances;
    NX_CHECK(instances[0].attempts.size() == 2);

    // A new suite is discovered through the inotify watch on .suite.
    const std::string extra_dir = fixture.root + "/.suite/extra";
    make_dirs(extra_dir);
    runner::SuiteMetadata extra{};
    extra.suite_id = "extra";
    extra.run_dir = fixture.root;
    extra.artifacts_path = fixture.artifacts;
    nlohmann::json extra_json;
    runner::to_json(extra_json, extra);
    write_json(extra_dir + "/suite.json", extra_json);
    NX_CHECK(wait_until(
        [&] {
            discovery.handle_inotify();
            return discovery.dirty();
        },
        2000));
    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].suites.size() == 2);

    // Removing it is reflected after the next event/rescan.
    remove_tree(extra_dir);
    NX_CHECK(wait_until(
        [&] {
            discovery.handle_inotify();
            return discovery.dirty();
        },
        2000));
    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].suites.size() == 1);

    // A suite with no artifact metadata is visible but exposes no streams.
    const std::string empty_dir = fixture.root + "/.suite/empty_artifacts";
    make_dirs(empty_dir);
    runner::SuiteMetadata empty_suite{};
    empty_suite.suite_id = "empty_artifacts";
    empty_suite.run_dir = fixture.root;
    nlohmann::json empty_json;
    runner::to_json(empty_json, empty_suite);
    write_json(empty_dir + "/suite.json", empty_json);
    NX_CHECK(discovery.rescan().has_value());
    bool found_empty = false;
    for (const hub::HubSuite& candidate : discovery.tree().runs[0].suites) {
        if (candidate.suite_id == "empty_artifacts") {
            found_empty = true;
            NX_CHECK(candidate.runner_log.id.empty());
        }
    }
    NX_CHECK(found_empty);
    remove_tree(empty_dir);
    NX_CHECK(discovery.rescan().has_value());

    // Multiple roots merge; duplicate roots collapse; invalid roots fail.
    Fixture second("roots2");
    hub::Discovery multiple;
    std::vector<std::string> multiple_roots{fixture.root, second.root,
                                             fixture.root};
    NX_CHECK(multiple.initialize(multiple_roots).has_value());
    NX_CHECK(multiple.tree().runs.size() == 2);
    hub::Discovery invalid;
    std::vector<std::string> bad{fixture.root + "/missing"};
    NX_CHECK(!invalid.initialize(bad).has_value());
    const std::vector<std::string> no_roots;
    NX_CHECK(!invalid.initialize(no_roots).has_value());

    hub::RunRoot root_label{"run-9", fixture.root};
    NX_CHECK(!root_label.to_string().empty());

    // Attempts with non-numeric names are ignored.
    make_dirs(fixture.artifacts + "/hil/alpha/1/attempt-bogus");
    make_dirs(fixture.artifacts + "/hil/alpha/1/attempt-");
    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].suites[0].apps[0].instances[0]
                 .attempts.size() == 2);

    // Standalone instance directories become their own run entries.
    const std::string standalone = fixture.root + "/solo";
    make_dirs(standalone);
    runner::ResourceMetadata solo{};
    solo.run_id = "run-1";
    solo.app_name = "solo";
    solo.instance_id = "1";
    solo.instance_dir = standalone;
    solo.pid = 999999;
    nlohmann::json solo_json;
    runner::to_json(solo_json, solo);
    write_json(standalone + "/resource.json", solo_json);
    write_text(standalone + "/app.jsonl", "{}\n");
    write_text(standalone + "/metrics.jsonl", "");
    write_text(standalone + "/traces.jsonl", "");
    write_text(standalone + "/chrono.jsonl", "");
    write_text(standalone + "/stdout.log", "");
    write_text(standalone + "/stderr.log", "");
    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].standalone_instances.size() == 1);
    NX_CHECK(hub::find_stream(discovery.tree(),
                              "run-1:standalone:solo:1:1:app") != nullptr);

    // Standalone states derive from the same runner status contract.
    const std::string running_dir = fixture.root + "/alive";
    make_dirs(running_dir);
    runner::ResourceMetadata alive{};
    alive.pid = ::getpid();
    nlohmann::json alive_json;
    runner::to_json(alive_json, alive);
    write_json(running_dir + "/resource.json", alive_json);

    const std::string stopped_dir = fixture.root + "/stopped";
    make_dirs(stopped_dir);
    runner::ResourceMetadata stopped{};
    stopped.pid = 999999;
    nlohmann::json stopped_json;
    runner::to_json(stopped_json, stopped);
    write_json(stopped_dir + "/resource.json", stopped_json);
    runner::ExitMetadata stopped_exit{};
    stopped_exit.terminated_by = runner::TerminatedBy::Runner;
    nlohmann::json stopped_exit_json;
    runner::to_json(stopped_exit_json, stopped_exit);
    write_json(stopped_dir + "/exit.json", stopped_exit_json);

    const std::string unknown_dir = fixture.root + "/unknown";
    make_dirs(unknown_dir);
    write_text(unknown_dir + "/resource.json", "{not-json");

    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].standalone_instances.size() == 4);
    for (const hub::HubInstance& instance :
         discovery.tree().runs[0].standalone_instances) {
        if (instance.instance_dir == running_dir) {
            NX_CHECK(instance.state == runner::AppState::Running);
        } else if (instance.instance_dir == stopped_dir) {
            NX_CHECK(instance.state == runner::AppState::Stopped);
        } else if (instance.instance_dir == unknown_dir) {
            NX_CHECK(instance.state == runner::AppState::Unknown);
        }
    }

    // Read-only: no source file was modified by scan/watch/reconcile.
    const std::string before = read_text(fixture.attempt_dir + "/app.jsonl");
    static_cast<void>(discovery.rescan());
    NX_CHECK(read_text(fixture.attempt_dir + "/app.jsonl") == before);
}

void test_discovery_states() {
    Fixture fixture("states");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());

    runner::SuiteStatus degraded{};
    degraded.state = runner::SuiteState::Degraded;
    runner::SuiteAppStatus app{};
    app.app = "alpha";
    app.instance = "1";
    app.instance_dir = fixture.instance_dir;
    app.state = runner::AppState::Failed;
    degraded.apps.push_back(app);
    nlohmann::json document;
    runner::to_json(document, degraded);
    write_json(fixture.suite_dir + "/suite-state.json", document);
    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].suites[0].state ==
             runner::SuiteState::Degraded);
    NX_CHECK(discovery.tree().runs[0].suites[0].apps[0].state ==
             runner::AppState::Failed);

    runner::SuiteExit exit{};
    exit.state = runner::SuiteState::Stopped;
    exit.reason = "operator_stop";
    runner::to_json(document, exit);
    write_json(fixture.suite_dir + "/suite-exit.json", document);
    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].suites[0].state ==
             runner::SuiteState::Stopped);
    NX_CHECK(discovery.tree().runs[0].suites[0].reason == "operator_stop");

    std::error_code error;
    std::filesystem::remove(fixture.suite_dir + "/suite-state.json", error);
    std::filesystem::remove(fixture.suite_dir + "/suite-exit.json", error);
    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].suites[0].state ==
             runner::SuiteState::Unknown);

    auto combined_state = [&](runner::AppState app_state) {
        runner::SuiteStatus recorded{};
        recorded.state = runner::SuiteState::Unknown;
        runner::SuiteAppStatus entry{};
        entry.app = "alpha";
        entry.instance = "1";
        entry.instance_dir = fixture.instance_dir;
        entry.state = app_state;
        recorded.apps.push_back(entry);
        nlohmann::json json;
        runner::to_json(json, recorded);
        write_json(fixture.suite_dir + "/suite-state.json", json);
        static_cast<void>(discovery.rescan());
        return discovery.tree().runs[0].suites[0].state;
    };
    NX_CHECK(combined_state(runner::AppState::Restarting) ==
             runner::SuiteState::Running);
    NX_CHECK(combined_state(runner::AppState::Failed) ==
             runner::SuiteState::Failed);
    NX_CHECK(combined_state(runner::AppState::Stopped) ==
             runner::SuiteState::Unknown);
    NX_CHECK(combined_state(runner::AppState::Exited) ==
             runner::SuiteState::Unknown);
    NX_CHECK(combined_state(runner::AppState::Running) ==
             runner::SuiteState::Running);
}

void test_streaming() {
    Fixture fixture("streaming");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::StreamLimits limits{};
    limits.ring_records = 2;
    limits.ring_bytes = 4096;
    hub::StreamManager streaming(limits);
    NX_CHECK(streaming.sync(discovery.tree()).has_value());
    streaming.poll();

    const std::string id = stream_id(fixture, "app");
    std::vector<hub::StreamRecord> snapshot;
    std::optional<hub::GapMarker> gap;
    std::uint64_t subscriber = streaming.subscribe(id, 0, snapshot, gap);
    NX_CHECK(subscriber > 0);
    NX_CHECK(snapshot.size() == 2);
    NX_CHECK(snapshot.back().line == "{\"a\":2}");

    // Live append is delivered after the snapshot.
    write_text(fixture.attempt_dir + "/app.jsonl",
               "{\"a\":1}\n{\"a\":2}\n{\"a\":3}\n");
    streaming.poll();
    std::vector<hub::StreamRecord> live;
    std::vector<hub::GapMarker> gaps;
    streaming.take(subscriber, live, gaps);
    NX_CHECK(!live.empty());
    NX_CHECK(live.back().line == "{\"a\":3}");
    NX_CHECK(gaps.empty());

    // Ring overflow: newest records survive and an older cursor gets a gap.
    std::vector<hub::StreamRecord> recent;
    std::optional<hub::GapMarker> recent_gap;
    streaming.recent(id, 2, recent, recent_gap);
    NX_CHECK(recent.size() == 2);
    snapshot.clear();
    gap.reset();
    const std::uint64_t second = streaming.subscribe(id, 0, snapshot, gap);
    NX_CHECK(second > 0);
    NX_CHECK(gap.has_value());

    // Partial lines wait for the newline.
    write_text(fixture.attempt_dir + "/metrics.jsonl", "part");
    streaming.poll();
    std::vector<hub::StreamRecord> metrics_snapshot;
    gap.reset();
    static_cast<void>(streaming.subscribe(stream_id(fixture, "metrics"), 0,
                                          metrics_snapshot, gap));
    NX_CHECK(metrics_snapshot.empty());
    write_text(fixture.attempt_dir + "/metrics.jsonl", "partial\n");
    streaming.poll();
    streaming.recent(stream_id(fixture, "metrics"), 2, recent, recent_gap);
    NX_CHECK(!recent.empty());
    NX_CHECK(recent.back().line == "partial");

    // Truncation and replacement are followed without changing identity.
    write_text(fixture.attempt_dir + "/stdout.log", "one\n");
    streaming.poll();
    recent.clear();
    streaming.recent(stream_id(fixture, "stdout"), 2, recent, recent_gap);
    NX_CHECK(!recent.empty() && recent.back().line == "one");
    const std::string replacement = fixture.attempt_dir + "/stdout.new";
    write_text(replacement, "two\n");
    std::error_code rename_error;
    std::filesystem::rename(replacement, fixture.attempt_dir + "/stdout.log",
                            rename_error);
    NX_CHECK(!rename_error);
    streaming.poll();
    recent.clear();
    streaming.recent(stream_id(fixture, "stdout"), 2, recent, recent_gap);
    NX_CHECK(!recent.empty() && recent.back().line == "two");

    // A slow subscriber is bounded rather than growing without limit.
    hub::StreamLimits small{};
    small.subscriber_bytes = 1024;
    hub::StreamManager slow(small);
    NX_CHECK(slow.sync(discovery.tree()).has_value());
    slow.poll();
    std::vector<hub::StreamRecord> slow_snapshot;
    std::optional<hub::GapMarker> slow_gap;
    const std::uint64_t slow_id =
        slow.subscribe(stream_id(fixture, "app"), 0, slow_snapshot, slow_gap);
    std::string many;
    for (int index = 0; index < 40; ++index) {
        many += "{\"slow\":" + std::to_string(index) + "}\n";
    }
    write_text(fixture.attempt_dir + "/app.jsonl", many);
    slow.poll();
    NX_CHECK(slow.overflowed(slow_id));
    std::vector<hub::StreamRecord> other_snapshot;
    std::optional<hub::GapMarker> other_gap;
    const std::uint64_t other_id =
        slow.subscribe(stream_id(fixture, "stdout"), 0, other_snapshot,
                       other_gap);
    NX_CHECK(!slow.overflowed(other_id));
    NX_CHECK(!other_snapshot.empty());

    // Byte capacity drops oldest records even when the count fits.
    write_text(fixture.attempt_dir + "/metrics.jsonl",
               std::string(100, 'a') + "\n" + std::string(100, 'b') + "\n");
    hub::StreamLimits byte_limits{};
    byte_limits.ring_bytes = 256;
    hub::StreamManager bytes(byte_limits);
    NX_CHECK(bytes.sync(discovery.tree()).has_value());
    bytes.poll();
    std::vector<hub::StreamRecord> byte_records;
    std::optional<hub::GapMarker> byte_gap;
    bytes.recent(stream_id(fixture, "metrics"), 10, byte_records, byte_gap);
    NX_CHECK(byte_records.size() == 1);

    // Bounds are clamped to their documented minimums and string forms work.
    hub::StreamLimits clamped_limits{0, 0, 0, 0, 0};
    hub::StreamManager clamped(clamped_limits);
    NX_CHECK(clamped.limits().ring_records == 1);
    NX_CHECK(clamped.limits().ring_bytes >= 256);
    NX_CHECK(clamped.limits().max_line_bytes >= 64);
    NX_CHECK(clamped.limits().read_chunk_bytes >= 1024);
    NX_CHECK(clamped.limits().subscriber_bytes >= 1024);
    NX_CHECK(!clamped_limits.to_string().empty());
    NX_CHECK(!clamped.to_string().empty());
    NX_CHECK(!std::format("{}", clamped_limits).empty());

    // Long lines become gaps, and a small subscriber budget overflows.
    hub::StreamLimits line_limits{};
    line_limits.max_line_bytes = 64;
    line_limits.subscriber_bytes = 1024;
    hub::StreamManager long_lines(line_limits);
    NX_CHECK(long_lines.sync(discovery.tree()).has_value());
    long_lines.poll();
    std::vector<hub::StreamRecord> long_snapshot;
    std::optional<hub::GapMarker> long_gap;
    const std::uint64_t long_id = long_lines.subscribe(
        stream_id(fixture, "app"), 0, long_snapshot, long_gap);
    NX_CHECK(long_lines.subscriber_count() == 1);
    std::string long_text;
    for (int index = 0; index < 40; ++index) {
        long_text += std::string(200, 'x') + "\n";
    }
    long_text += "short-after-gap\n";
    write_text(fixture.attempt_dir + "/app.jsonl", long_text);
    long_lines.poll();
    std::vector<hub::StreamRecord> long_records;
    std::vector<hub::GapMarker> long_gaps;
    long_lines.take(long_id, long_records, long_gaps);
    NX_CHECK(!long_gaps.empty());
    NX_CHECK(long_lines.overflowed(long_id));
    long_lines.unsubscribe(long_id);
    NX_CHECK(long_lines.subscriber_count() == 0);
    long_lines.take(long_id, long_records, long_gaps);
    long_lines.recent("missing-stream", 1, long_records, long_gap);
    NX_CHECK(!long_lines.overflowed(long_id));

    // Subscribing to an unknown stream records the cursor without a snapshot.
    std::vector<hub::StreamRecord> unknown_snapshot;
    std::optional<hub::GapMarker> unknown_gap;
    const std::uint64_t unknown_id = long_lines.subscribe(
        "missing-stream", 5, unknown_snapshot, unknown_gap);
    NX_CHECK(unknown_snapshot.empty());
    NX_CHECK(unknown_id > 0);
    long_lines.unsubscribe(unknown_id);

    // A descriptor path change reopens the stream at the new file.
    const std::string copy_path = fixture.attempt_dir + "/app-copy.jsonl";
    write_text(copy_path, "copied-line\n");
    hub::HubTree changed = discovery.tree();
    auto& changed_streams = changed.runs[0]
                                .suites[0]
                                .apps[0]
                                .instances[0]
                                .attempts[0]
                                .streams;
    for (hub::HubStream& stream : changed_streams) {
        if (stream.kind == hub::StreamKind::App) {
            stream.path = copy_path;
        }
    }
    hub::StreamManager moved;
    NX_CHECK(moved.sync(discovery.tree()).has_value());
    NX_CHECK(moved.sync(changed).has_value());
    moved.poll();
    std::vector<hub::StreamRecord> moved_records;
    std::optional<hub::GapMarker> moved_gap;
    moved.recent(stream_id(fixture, "app"), 10, moved_records, moved_gap);
    NX_CHECK(!moved_records.empty());
    NX_CHECK(moved_records.back().line == "copied-line");

    // A missing stream is reported through the diagnostic sink and makes sync
    // fail without disturbing other tailers.
    DiagnosticCapture capture;
    hub::StreamManager diagnosed;
    diagnosed.set_diagnostic_sink(capture_diagnostic, &capture);
    hub::HubTree missing_tree{};
    hub::HubRun missing_run{};
    missing_run.run_key = "run-missing";
    hub::HubInstance missing_instance{};
    missing_instance.instance = "1";
    hub::HubAttempt missing_attempt{};
    missing_attempt.attempt = 1;
    missing_attempt.streams.push_back(
        hub::HubStream{"missing:app", hub::StreamKind::App,
                        fixture.root + "/missing-app.jsonl"});
    missing_instance.attempts.push_back(std::move(missing_attempt));
    missing_run.standalone_instances.push_back(std::move(missing_instance));
    missing_tree.runs.push_back(std::move(missing_run));
    const core::Status missing_sync = diagnosed.sync(missing_tree);
    NX_CHECK(!missing_sync.has_value());
    NX_CHECK(missing_sync.error() == core::ErrorCode::SystemError);
    NX_CHECK(diagnostic_contains(capture, "event=stream_open_failed"));
    NX_CHECK(diagnostic_contains(capture, "missing:app"));
    NX_CHECK(diagnosed.sync(hub::HubTree{}).has_value());

    // A file that disappears after being tailed is reported on the next poll.
    const std::string transient = fixture.attempt_dir + "/transient.jsonl";
    write_text(transient, "x\n");
    DiagnosticCapture stat_capture;
    hub::StreamManager transient_streams;
    transient_streams.set_diagnostic_sink(capture_diagnostic, &stat_capture);
    hub::HubTree transient_tree{};
    hub::HubRun transient_run{};
    transient_run.run_key = "run-transient";
    hub::HubInstance transient_instance{};
    transient_instance.instance = "1";
    hub::HubAttempt transient_attempt{};
    transient_attempt.attempt = 1;
    transient_attempt.streams.push_back(
        hub::HubStream{"transient:app", hub::StreamKind::App, transient});
    transient_instance.attempts.push_back(std::move(transient_attempt));
    transient_run.standalone_instances.push_back(
        std::move(transient_instance));
    transient_tree.runs.push_back(std::move(transient_run));
    NX_CHECK(transient_streams.sync(transient_tree).has_value());
    transient_streams.poll();
    std::error_code remove_error;
    std::filesystem::remove(transient, remove_error);
    transient_streams.poll();
    NX_CHECK(diagnostic_contains(stat_capture, "event=stream_stat_failed"));

    // A directory used as a stream path opens but fails to read; both the
    // initial seed and the poll path report the read error.
    DiagnosticCapture read_capture;
    hub::StreamManager directory_streams;
    directory_streams.set_diagnostic_sink(capture_diagnostic, &read_capture);
    hub::HubTree directory_tree{};
    hub::HubRun directory_run{};
    directory_run.run_key = "run-directory";
    hub::HubInstance directory_instance{};
    directory_instance.instance = "1";
    hub::HubAttempt directory_attempt{};
    directory_attempt.attempt = 1;
    directory_attempt.streams.push_back(hub::HubStream{
        "directory:app", hub::StreamKind::App, fixture.attempt_dir});
    directory_instance.attempts.push_back(std::move(directory_attempt));
    directory_run.standalone_instances.push_back(
        std::move(directory_instance));
    directory_tree.runs.push_back(std::move(directory_run));
    NX_CHECK(directory_streams.sync(directory_tree).has_value());
    directory_streams.poll();
    NX_CHECK(diagnostic_contains(read_capture, "event=stream_read_failed"));
}

void test_http_start_errors() {
    Fixture fixture("http_errors");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::StreamManager streaming;
    hub::HttpServer server(discovery, streaming);

    NX_CHECK(!server.start("").has_value());
    NX_CHECK(!server.start("noport").has_value());
    NX_CHECK(!server.start(":80").has_value());
    NX_CHECK(!server.start("127.0.0.1:").has_value());
    NX_CHECK(!server.start("localhost:abc").has_value());
    NX_CHECK(!server.start("127.0.0.1:70000").has_value());
    NX_CHECK(!server.start("bad host:80").has_value());
    server.set_diagnostic_sink(nullptr, nullptr);
    NX_CHECK(!server.to_string().empty());

    hub::HttpServer localhost(discovery, streaming);
    NX_CHECK(localhost.start("localhost:0").has_value());
    NX_CHECK(localhost.port() != 0);

    const int occupied = ::socket(AF_INET, SOCK_STREAM, 0);
    NX_CHECK(occupied >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr));
    NX_CHECK(::bind(occupied, reinterpret_cast<sockaddr*>(&address),
                    sizeof(address)) == 0);
    NX_CHECK(::listen(occupied, 1) == 0);
    sockaddr_in bound{};
    socklen_t bound_length = sizeof(bound);
    NX_CHECK(::getsockname(occupied, reinterpret_cast<sockaddr*>(&bound),
                           &bound_length) == 0);
    hub::HttpServer busy(discovery, streaming);
    NX_CHECK(!busy.start("127.0.0.1:" +
                         std::to_string(ntohs(bound.sin_port)))
                  .has_value());
    static_cast<void>(::close(occupied));

    // A failed initial stream reconciliation is reported but does not prevent
    // the listener from starting.
    std::error_code remove_error;
    std::filesystem::remove(fixture.attempt_dir + "/traces.jsonl", remove_error);
    DiagnosticCapture capture;
    hub::StreamManager late;
    hub::HttpServer with_failure(discovery, late, capture_diagnostic, &capture);
    NX_CHECK(with_failure.start("127.0.0.1:0").has_value());
    NX_CHECK(diagnostic_contains(capture, "event=hub_sync_failed"));
}

void test_http_sse_gaps() {
    Fixture fixture("http_gaps");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::StreamLimits limits{};
    limits.ring_records = 1;
    limits.max_line_bytes = 64;
    limits.subscriber_bytes = 4096;
    hub::StreamManager streaming(limits);
    NX_CHECK(streaming.sync(discovery.tree()).has_value());
    streaming.poll();
    write_text(fixture.attempt_dir + "/app.jsonl", "one\ntwo\nthree\n");
    streaming.poll();

    hub::HttpServer server(discovery, streaming);
    NX_CHECK(server.start("127.0.0.1:0").has_value());
    std::atomic<bool> stop{false};
    std::thread thread([&] { server.run(&stop); });
    const std::uint16_t port = server.port();

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    NX_CHECK(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr));
    NX_CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&address),
                       sizeof(address)) == 0);
    timeval timeout{};
    timeout.tv_sec = 2;
    static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                    sizeof(timeout)));
    const std::string request =
        "GET /api/events?stream=" + stream_id(fixture, "app") +
        "&since=0 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    static_cast<void>(::send(fd, request.data(), request.size(), 0));
    std::string text;
    char buffer[4096];
    NX_CHECK(wait_until(
        [&] {
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count > 0) {
                text.append(buffer, static_cast<std::size_t>(count));
            }
            return contains(text, "event: snapshot_end");
        },
        3000));
    NX_CHECK(contains(text, "event: gap"));
    NX_CHECK(contains(text, "\"sequence\":"));

    // A long line produces a live gap after the snapshot boundary.
    write_text(fixture.attempt_dir + "/app.jsonl",
               std::string(200, 'x') + "\n");
    std::string live;
    NX_CHECK(wait_until(
        [&] {
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count > 0) {
                live.append(buffer, static_cast<std::size_t>(count));
            }
            return contains(live, "event: gap");
        },
        3000));
    static_cast<void>(::close(fd));

    stop.store(true);
    server.stop();
    thread.join();
}

void test_http_fuzz_corpus() {
    Fixture fixture("http_fuzz");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::StreamManager streaming;
    NX_CHECK(streaming.sync(discovery.tree()).has_value());
    streaming.poll();
    hub::HttpServer server(discovery, streaming);
    NX_CHECK(server.start("127.0.0.1:0").has_value());
    std::atomic<bool> stop{false};
    std::thread thread([&] { server.run(&stop); });
    const std::uint16_t port = server.port();
    NX_CHECK(port != 0);

    auto exchange = [&](std::string_view payload) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        NX_CHECK(fd >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr));
        NX_CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&address),
                           sizeof(address)) == 0);
        timeval timeout{};
        timeout.tv_usec = 200000;
        static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                        sizeof(timeout)));
        static_cast<void>(::send(fd, payload.data(), payload.size(), 0));
        static_cast<void>(::shutdown(fd, SHUT_WR));
        std::string response;
        char buffer[4096];
        for (;;) {
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count <= 0) break;
            response.append(buffer, static_cast<std::size_t>(count));
        }
        static_cast<void>(::close(fd));
        return response;
    };

    // Fixed-seed bytes exercise request-line, header, URL, and body edges.
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    auto next = [&]() -> std::uint32_t {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 33);
    };
    static constexpr char alphabet[] =
        "GET /api/health?stream=abc&since=1&limit=2 HTTP/1.1\r\n"
        "Host: localhost\r\nConnection: close\r\nLast-Event-ID: a:1\r\n"
        ":%?%\0\x01\x7f";
    constexpr std::size_t alphabet_size = sizeof(alphabet) - 1;
    for (int case_index = 0; case_index < 120; ++case_index) {
        const std::size_t length = next() % 512U;
        std::string payload;
        payload.reserve(length + 32U);
        if (case_index % 3 == 0) {
            payload += "GET /api/health HTTP/1.1\r\nHost: localhost\r\n";
        }
        for (std::size_t index = 0; index < length; ++index) {
            payload.push_back(alphabet[next() % alphabet_size]);
        }
        payload += "\r\n\r\n";
        const std::string response = exchange(payload);
        NX_CHECK(response.empty() || contains(response, "HTTP/1.1"));
    }

    const std::string health = exchange(
        "GET /api/health HTTP/1.1\r\nHost: localhost\r\n"
        "Connection: close\r\n\r\n");
    NX_CHECK(contains(health, "200"));
    NX_CHECK(contains(health, "\"status\":\"ok\""));

    stop.store(true);
    server.stop();
    thread.join();
}

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
void test_http_fault_injection() {
    Fixture fixture("http_faults");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::StreamManager streaming;

    core::detail::set_fault_hook(hub_fault_hook);
    for (const auto point : {core::detail::FaultPoint::Hub_socket,
                             core::detail::FaultPoint::Hub_bind,
                             core::detail::FaultPoint::Hub_epoll,
                             core::detail::FaultPoint::Hub_eventfd}) {
        g_hub_fault_point = point;
        g_hub_fault_enabled = true;
        hub::HttpServer server(discovery, streaming);
        NX_CHECK(!server.start("127.0.0.1:0").has_value());
        g_hub_fault_enabled = false;
    }
    core::detail::set_fault_hook(nullptr);
}
#endif

void test_web_assets() {
    NX_CHECK(contains(hub::index_html(), "/app.js"));
    NX_CHECK(contains(hub::index_html(), "/style.css"));
    NX_CHECK(contains(hub::app_js(), "pausedLines"));
    NX_CHECK(contains(hub::app_js(), "scheduleReconnect"));
    NX_CHECK(contains(hub::app_js(), "lastSequence"));
    NX_CHECK(contains(hub::app_js(), "EventSource"));
    NX_CHECK(!contains(hub::app_js(), "/api/start"));
    NX_CHECK(contains(hub::style_css(), "--accent"));
    // app.js is an IIFE `(() => { ... })();`. A missing wrapper parenthesis is
    // a JavaScript syntax error that leaves the connection badge stuck on
    // "connecting" (spec hub/web-ui "Embedded script parses and bootstraps").
    NX_CHECK(contains(hub::app_js(), "(() => {"));
    NX_CHECK(contains(hub::app_js(), "})();"));
}

void test_http_server() {
    Fixture fixture("http");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::StreamManager streaming;
    NX_CHECK(streaming.sync(discovery.tree()).has_value());
    streaming.poll();
    DiagnosticCapture capture;
    hub::HttpServer server(discovery, streaming, capture_diagnostic, &capture);
    NX_CHECK(server.start("127.0.0.1:0").has_value());
    std::atomic<bool> stop{false};
    std::thread thread([&] { server.run(&stop); });
    const std::uint16_t port = server.port();
    NX_CHECK(port != 0);

    auto request = [&](std::string_view method, std::string_view path,
                       std::string_view extra_headers = {}) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        NX_CHECK(fd >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr));
        NX_CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&address),
                           sizeof(address)) == 0);
        const std::string payload = std::string(method) + " " +
                                    std::string(path) +
                                    " HTTP/1.1\r\nHost: localhost\r\n" +
                                    std::string(extra_headers) +
                                    "Connection: close\r\n\r\n";
        static_cast<void>(::send(fd, payload.data(), payload.size(), 0));
        std::string response;
        char buffer[4096];
        for (;;) {
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count <= 0) break;
            response.append(buffer, static_cast<std::size_t>(count));
        }
        static_cast<void>(::close(fd));
        return response;
    };

    auto raw = [&](std::string_view payload) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        NX_CHECK(fd >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr));
        NX_CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&address),
                           sizeof(address)) == 0);
        timeval timeout{};
        timeout.tv_sec = 2;
        static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                        sizeof(timeout)));
        static_cast<void>(::send(fd, payload.data(), payload.size(), 0));
        std::string response;
        char buffer[4096];
        for (;;) {
            const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
            if (count <= 0) break;
            response.append(buffer, static_cast<std::size_t>(count));
        }
        static_cast<void>(::close(fd));
        return response;
    };

    NX_CHECK(contains(request("GET", "/api/health"), "\"status\":\"ok\""));
    NX_CHECK(contains(request("GET", "/api/tree"), "\"suite_id\":\"hil\""));
    NX_CHECK(contains(request("GET", "/api/streams"), "run-1:hil:alpha:1:1:app"));
    NX_CHECK(contains(request("GET", "/api/streams/" + stream_id(fixture, "app") +
                                  "/recent?limit=1"),
                              "\"records\""));
    NX_CHECK(contains(request("GET", "/"), "<!doctype html>"));
    NX_CHECK(contains(request("GET", "/app.js"), "EventSource"));
    NX_CHECK(contains(request("GET", "/style.css"), "--accent"));
    NX_CHECK(contains(request("GET", "/api/missing"), "404"));
    NX_CHECK(contains(request("GET", "/" + std::string(5000, 'a')), "400"));
    NX_CHECK(contains(request("GET", "/api/health",
                              std::string(9000, 'x')), "400"));

    // Malformed request lines and unsupported versions get a bounded 400.
    NX_CHECK(contains(raw("GET\r\n\r\n"), "400"));
    NX_CHECK(contains(raw("GET /api/health HTTP/2.0\r\n\r\n"), "400"));
    NX_CHECK(contains(raw("GET /api/health\r\n\r\n"), "400"));
    NX_CHECK(contains(raw(std::string(13000, 'x')), "400"));

    // Every non-GET method is rejected without reaching a route.
    for (const std::string_view method : {"POST", "PUT", "DELETE", "PATCH"}) {
        NX_CHECK(contains(request(method, "/api/tree"), "405"));
    }

    // Invalid query and escape values fall back to defaults or a 404.
    NX_CHECK(contains(request("GET", "/api/streams/" + stream_id(fixture, "app") +
                                  "/recent?limit=abc"),
                              "200"));
    NX_CHECK(contains(request("GET", "/api/streams/" + stream_id(fixture, "app") +
                                  "/recent?limit=999999"),
                              "200"));
    NX_CHECK(contains(request("GET", "/api/streams/%zz/recent"), "404"));
    NX_CHECK(contains(request("GET", "/api/streams/unknown/recent"), "404"));
    // Percent-decoding is exercised by encoding every separator manually.
    std::string encoded_path;
    for (const char c : stream_id(fixture, "app")) {
        if (c == ':') {
            encoded_path += "%3A";
        } else {
            encoded_path.push_back(c);
        }
    }
    NX_CHECK(contains(request("GET", "/api/streams/" + encoded_path + "/recent"),
                      "200"));
    NX_CHECK(contains(raw("GET /api/events?stream=unknown HTTP/1.1\r\n"
                          "Host: localhost\r\n\r\n"),
                      "404"));
    NX_CHECK(contains(raw("GET /api/events?stream=" + stream_id(fixture, "app") +
                          "&since=not-a-number HTTP/1.1\r\n"
                          "Host: localhost\r\n\r\n"),
                      "event: snapshot_end"));

    // HTTP/1.1 keep-alive serves two requests on one connection.
    {
        const int keep_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in keep_address{};
        keep_address.sin_family = AF_INET;
        keep_address.sin_port = htons(port);
        static_cast<void>(
            ::inet_pton(AF_INET, "127.0.0.1", &keep_address.sin_addr));
        NX_CHECK(::connect(keep_fd,
                           reinterpret_cast<sockaddr*>(&keep_address),
                           sizeof(keep_address)) == 0);
        timeval keep_timeout{};
        keep_timeout.tv_sec = 2;
        static_cast<void>(::setsockopt(keep_fd, SOL_SOCKET, SO_RCVTIMEO,
                                        &keep_timeout, sizeof(keep_timeout)));
        const std::string first_request =
            "GET /api/health HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: keep-alive\r\n\r\n";
        static_cast<void>(::send(keep_fd, first_request.data(),
                                 first_request.size(), 0));
        std::string buffer;
        auto read_one = [&](std::string& response) -> bool {
            for (;;) {
                const std::size_t header_end = buffer.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    const std::string header = buffer.substr(0, header_end + 4);
                    const std::size_t length_pos =
                        header.find("Content-Length: ");
                    if (length_pos != std::string::npos) {
                        std::size_t length = 0;
                        std::size_t index = length_pos + 16;
                        while (index < header.size() && header[index] >= '0' &&
                               header[index] <= '9') {
                            length = length * 10U +
                                     static_cast<std::size_t>(
                                         header[index] - '0');
                            ++index;
                        }
                        if (buffer.size() >= header_end + 4 + length) {
                            response = buffer.substr(0, header_end + 4 + length);
                            buffer.erase(0, header_end + 4 + length);
                            return true;
                        }
                    }
                }
                char chunk[4096];
                const ssize_t count = ::recv(keep_fd, chunk, sizeof(chunk), 0);
                if (count <= 0) return false;
                buffer.append(chunk, static_cast<std::size_t>(count));
            }
        };
        std::string first_response;
        NX_CHECK(read_one(first_response));
        NX_CHECK(contains(first_response, "HTTP/1.1 200 OK"));
        const std::string second_request =
            "GET /api/health HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n";
        static_cast<void>(::send(keep_fd, second_request.data(),
                                 second_request.size(), 0));
        std::string second_response;
        NX_CHECK(read_one(second_response));
        NX_CHECK(contains(second_response, "HTTP/1.1 200 OK"));
        static_cast<void>(::close(keep_fd));
    }

    // SSE snapshot headers and boundary over a raw socket.
    const int sse_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    static_cast<void>(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr));
    NX_CHECK(::connect(sse_fd, reinterpret_cast<sockaddr*>(&address),
                       sizeof(address)) == 0);
    timeval timeout{};
    timeout.tv_usec = 100000;
    static_cast<void>(::setsockopt(sse_fd, SOL_SOCKET, SO_RCVTIMEO,
                                    &timeout, sizeof(timeout)));
    const std::string sse_request =
        "GET /api/events?stream=" + stream_id(fixture, "app") +
        "&since=0 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    static_cast<void>(::send(sse_fd, sse_request.data(), sse_request.size(), 0));
    std::string sse;
    char sse_buffer[4096];
    NX_CHECK(wait_until(
        [&] {
            const ssize_t count = ::recv(sse_fd, sse_buffer, sizeof(sse_buffer), 0);
            if (count > 0) sse.append(sse_buffer, static_cast<std::size_t>(count));
            return contains(sse, "event: snapshot_end");
        },
        3000));
    NX_CHECK(contains(sse, "Content-Type: text/event-stream"));
    NX_CHECK(contains(sse, "Transfer-Encoding: chunked"));
    NX_CHECK(contains(sse, "event: snapshot_begin"));
    NX_CHECK(contains(sse, "event: record"));

    // A new line is delivered live after the snapshot boundary.
    write_text(fixture.attempt_dir + "/app.jsonl",
               "{\"a\":1}\n{\"a\":2}\n{\"a\":3}\n");
    NX_CHECK(wait_until(
        [&] {
            const ssize_t count = ::recv(sse_fd, sse_buffer, sizeof(sse_buffer), 0);
            if (count > 0) sse.append(sse_buffer, static_cast<std::size_t>(count));
            return contains(sse, "\\\"a\\\":3");
        },
        3000));
    static_cast<void>(::close(sse_fd));

    // Tree topic emits the current snapshot.
    {
        const int tree_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in tree_address{};
        tree_address.sin_family = AF_INET;
        tree_address.sin_port = htons(port);
        static_cast<void>(
            ::inet_pton(AF_INET, "127.0.0.1", &tree_address.sin_addr));
        NX_CHECK(::connect(tree_fd,
                           reinterpret_cast<sockaddr*>(&tree_address),
                           sizeof(tree_address)) == 0);
        timeval tree_timeout{};
        tree_timeout.tv_sec = 2;
        static_cast<void>(::setsockopt(tree_fd, SOL_SOCKET, SO_RCVTIMEO,
                                        &tree_timeout, sizeof(tree_timeout)));
        const std::string tree_request =
            "GET /api/events?tree=1 HTTP/1.1\r\nHost: localhost\r\n\r\n";
        static_cast<void>(::send(tree_fd, tree_request.data(),
                                 tree_request.size(), 0));
        std::string tree_text;
        char tree_buffer[4096];
        NX_CHECK(wait_until(
            [&] {
                const ssize_t count =
                    ::recv(tree_fd, tree_buffer, sizeof(tree_buffer), 0);
                if (count > 0) {
                    tree_text.append(tree_buffer,
                                     static_cast<std::size_t>(count));
                }
                return contains(tree_text, "event: tree");
            },
            3000));
        NX_CHECK(contains(tree_text, "\"suite_id\":\"hil\""));
        // A tree change is pushed to the live tree topic.
        make_dirs(fixture.artifacts + "/hil/alpha/1/attempt-0009");
        NX_CHECK(wait_until(
            [&] {
                const ssize_t count =
                    ::recv(tree_fd, tree_buffer, sizeof(tree_buffer), 0);
                if (count > 0) {
                    tree_text.append(tree_buffer,
                                     static_cast<std::size_t>(count));
                }
                const std::size_t first = tree_text.find("event: tree");
                return first != std::string::npos &&
                       tree_text.find("event: tree", first + 1) !=
                           std::string::npos;
            },
            5000));
        static_cast<void>(::close(tree_fd));
    }

    // All-stream SSE includes records from discovered streams.
    {
        const int all_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in all_address{};
        all_address.sin_family = AF_INET;
        all_address.sin_port = htons(port);
        static_cast<void>(
            ::inet_pton(AF_INET, "127.0.0.1", &all_address.sin_addr));
        NX_CHECK(::connect(all_fd,
                           reinterpret_cast<sockaddr*>(&all_address),
                           sizeof(all_address)) == 0);
        timeval all_timeout{};
        all_timeout.tv_sec = 2;
        static_cast<void>(::setsockopt(all_fd, SOL_SOCKET, SO_RCVTIMEO,
                                        &all_timeout, sizeof(all_timeout)));
        const std::string all_request =
            "GET /api/events HTTP/1.1\r\nHost: localhost\r\n\r\n";
        static_cast<void>(::send(all_fd, all_request.data(),
                                 all_request.size(), 0));
        std::string all_text;
        char all_buffer[4096];
        NX_CHECK(wait_until(
            [&] {
                const ssize_t count =
                    ::recv(all_fd, all_buffer, sizeof(all_buffer), 0);
                if (count > 0) {
                    all_text.append(all_buffer, static_cast<std::size_t>(count));
                }
                return contains(all_text, "event: snapshot_end");
            },
            3000));
        NX_CHECK(contains(all_text, "event: record"));
        static_cast<void>(::close(all_fd));
    }

    // Last-Event-ID resumes a stream cursor.
    {
        const int cursor_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in cursor_address{};
        cursor_address.sin_family = AF_INET;
        cursor_address.sin_port = htons(port);
        static_cast<void>(
            ::inet_pton(AF_INET, "127.0.0.1", &cursor_address.sin_addr));
        NX_CHECK(::connect(cursor_fd,
                           reinterpret_cast<sockaddr*>(&cursor_address),
                           sizeof(cursor_address)) == 0);
        timeval cursor_timeout{};
        cursor_timeout.tv_sec = 2;
        static_cast<void>(::setsockopt(cursor_fd, SOL_SOCKET, SO_RCVTIMEO,
                                        &cursor_timeout, sizeof(cursor_timeout)));
        const std::string cursor_request =
            "GET /api/events?stream=" + stream_id(fixture, "app") +
            " HTTP/1.1\r\nHost: localhost\r\nLast-Event-ID: " +
            stream_id(fixture, "app") + ":1\r\n\r\n";
        static_cast<void>(::send(cursor_fd, cursor_request.data(),
                                 cursor_request.size(), 0));
        std::string cursor_text;
        char cursor_buffer[4096];
        NX_CHECK(wait_until(
            [&] {
                const ssize_t count =
                    ::recv(cursor_fd, cursor_buffer, sizeof(cursor_buffer), 0);
                if (count > 0) {
                    cursor_text.append(cursor_buffer,
                                       static_cast<std::size_t>(count));
                }
                return contains(cursor_text, "event: snapshot_end");
            },
            3000));
        NX_CHECK(!contains(cursor_text, "\"sequence\":1"));
        NX_CHECK(contains(cursor_text, "\"sequence\":2"));
        static_cast<void>(::close(cursor_fd));
    }

    // Header names are case-insensitive, values tolerate whitespace, and a
    // similarly named header is not treated as the cursor.
    auto sse_with_header = [&](std::string_view header) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        NX_CHECK(fd >= 0);
        sockaddr_in sse_address{};
        sse_address.sin_family = AF_INET;
        sse_address.sin_port = htons(port);
        static_cast<void>(
            ::inet_pton(AF_INET, "127.0.0.1", &sse_address.sin_addr));
        NX_CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&sse_address),
                           sizeof(sse_address)) == 0);
        timeval sse_timeout{};
        sse_timeout.tv_sec = 2;
        static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &sse_timeout,
                                        sizeof(sse_timeout)));
        const std::string payload =
            "GET /api/events?stream=" + stream_id(fixture, "app") +
            " HTTP/1.1\r\nHost: localhost\r\n" + std::string(header) + "\r\n";
        static_cast<void>(::send(fd, payload.data(), payload.size(), 0));
        std::string response;
        char buffer[4096];
        NX_CHECK(wait_until(
            [&] {
                const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
                if (count > 0) {
                    response.append(buffer, static_cast<std::size_t>(count));
                }
                return contains(response, "event: snapshot_end");
            },
            3000));
        static_cast<void>(::close(fd));
        return response;
    };

    const std::vector<std::string> cursor_headers{
        "last-event-id: " + stream_id(fixture, "app") + ":2\r\n",
        "LAST-EVENT-ID: " + stream_id(fixture, "app") + ":2\r\n",
        "Last-Event-ID:    " + stream_id(fixture, "app") + ":2   \r\n"};
    for (const std::string& header : cursor_headers) {
        const std::string resumed = sse_with_header(header);
        NX_CHECK(!contains(resumed, "\"sequence\":1"));
        NX_CHECK(contains(resumed, "\"sequence\":3"));
    }
    const std::string invalid_cursor = sse_with_header(
        "Last-Event-ID: not-a-sequence\r\n");
    NX_CHECK(contains(invalid_cursor, "\"sequence\":1"));
    const std::string similar_header = sse_with_header(
        "X-Last-Event-ID: " + stream_id(fixture, "app") + ":2\r\n");
    NX_CHECK(contains(similar_header, "\"sequence\":1"));

    // Connection header matching is case-insensitive as well.
    const std::string connection_close =
        raw("GET /api/health HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: CLOSE\r\n\r\n");
    NX_CHECK(contains(connection_close, "200"));

    // A disappearing run root is reported as a failed refresh while the
    // listener keeps serving.
    remove_tree(fixture.root);
    NX_CHECK(wait_until(
        [&] { return diagnostic_contains(capture, "event=discovery_rescan_failed"); },
        4000));
    NX_CHECK(contains(request("GET", "/api/health"), "200"));

    stop.store(true);
    server.stop();
    thread.join();
}

} // namespace

int main() {
    run_suite("model_serialization", test_model_serialization);
    run_suite("discovery", test_discovery);
    run_suite("discovery_states", test_discovery_states);
    run_suite("streaming", test_streaming);
    run_suite("web_assets", test_web_assets);
    run_suite("http_start_errors", test_http_start_errors);
    run_suite("http_sse_gaps", test_http_sse_gaps);
    run_suite("http_fuzz_corpus", test_http_fuzz_corpus);
    run_suite("http_server", test_http_server);
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    run_suite("http_fault_injection", test_http_fault_injection);
#endif
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
