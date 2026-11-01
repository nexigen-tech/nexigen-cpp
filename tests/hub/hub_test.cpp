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

        runner::Suite_metadata metadata{};
        metadata.suite_id = "hil";
        metadata.run_id = "run-1";
        metadata.run_dir = root;
        metadata.artifacts_path = artifacts;
        metadata.pid = 1234;
        metadata.start_time_rfc3339 = "2026-11-01T00:00:00.000000000Z";
        metadata.start_time_mono_ns = 1;
        runner::Suite_app_metadata app{};
        app.app = "alpha";
        app.instance = "1";
        app.instance_dir = instance_dir;
        metadata.apps.push_back(app);
        nlohmann::json metadata_json;
        runner::to_json(metadata_json, metadata);
        write_json(suite_dir + "/suite.json", metadata_json);

        runner::Suite_status status{};
        status.state = runner::Suite_state::Running;
        status.suite_id = "hil";
        status.run_dir = root;
        status.artifacts_path = artifacts;
        runner::Suite_app_status app_status{};
        app_status.app = "alpha";
        app_status.instance = "1";
        app_status.instance_dir = instance_dir;
        app_status.artifact_dir = attempt_dir;
        app_status.attempt = 1;
        app_status.state = runner::App_state::Running;
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
        write_text(artifacts + "/runner.jsonl", "{\"event\":\"suite_started\"}\n");
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
         {hub::Stream_kind::App, hub::Stream_kind::Metrics,
          hub::Stream_kind::Traces, hub::Stream_kind::Chrono,
          hub::Stream_kind::Stdout, hub::Stream_kind::Stderr,
          hub::Stream_kind::Runner}) {
        hub::Stream_kind parsed = static_cast<hub::Stream_kind>(0xff);
        NX_CHECK(hub::from_string(hub::to_string(kind), parsed));
        NX_CHECK(parsed == kind);
        NX_CHECK(std::format("{}", kind) == hub::to_string(kind));
        nlohmann::json json;
        hub::to_json(json, kind);
        hub::from_json(json, parsed);
        NX_CHECK(parsed == kind);
    }

    hub::Stream_record record{};
    record.stream_id = "run-1:hil:alpha:1:1:app";
    record.kind = hub::Stream_kind::App;
    record.sequence = 7;
    record.received_at_rfc3339 = "2026-11-01T00:00:00.000000000Z";
    record.line = "hello";
    nlohmann::json json;
    hub::to_json(json, record);
    hub::Stream_record round;
    hub::from_json(json, round);
    NX_CHECK(round.sequence == 7);
    NX_CHECK(round.line == "hello");
    NX_CHECK(round.kind == hub::Stream_kind::App);

    hub::Gap_marker gap{};
    gap.stream_id = record.stream_id;
    gap.dropped = 2;
    gap.first_missing_sequence = 3;
    gap.last_missing_sequence = 4;
    hub::to_json(json, gap);
    hub::Gap_marker gap_round;
    hub::from_json(json, gap_round);
    NX_CHECK(gap_round.dropped == 2);
    NX_CHECK(gap_round.first_missing_sequence == 3);

    hub::Hub_tree tree{};
    hub::Hub_run run{};
    run.run_key = "run-1";
    run.run_dir = "/run";
    hub::Hub_suite suite{};
    suite.suite_id = "hil";
    suite.state = runner::Suite_state::Running;
    hub::Hub_app app{};
    app.app = "alpha";
    app.state = runner::App_state::Running;
    hub::Hub_instance instance{};
    instance.instance = "1";
    hub::Hub_attempt attempt{};
    attempt.attempt = 1;
    hub::Hub_stream stream{};
    stream.id = record.stream_id;
    stream.kind = hub::Stream_kind::App;
    attempt.streams.push_back(stream);
    instance.attempts.push_back(attempt);
    app.instances.push_back(instance);
    suite.apps.push_back(app);
    run.suites.push_back(suite);
    tree.runs.push_back(run);

    hub::to_json(json, tree);
    hub::Hub_tree round_tree;
    hub::from_json(json, round_tree);
    NX_CHECK(round_tree.runs.size() == 1);
    NX_CHECK(round_tree.runs[0].suites[0].apps[0].instances[0].attempts[0]
                 .streams[0]
                 .kind == hub::Stream_kind::App);

    hub::Hub_tree hostile = round_tree;
    hub::from_json(nlohmann::json::array(), hostile);
    NX_CHECK(hostile.runs.size() == 1);
}

void test_discovery() {
    Fixture fixture("discovery");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    const hub::Hub_tree& tree = discovery.tree();
    NX_CHECK(tree.runs.size() == 1);
    if (tree.runs.empty()) return;
    NX_CHECK(tree.runs[0].suites.size() == 1);
    if (tree.runs[0].suites.empty()) return;
    const hub::Hub_suite& suite = tree.runs[0].suites[0];
    NX_CHECK(suite.suite_id == "hil");
    NX_CHECK(suite.state == runner::Suite_state::Running);
    NX_CHECK(suite.apps.size() == 1);
    NX_CHECK(suite.apps[0].instances.size() == 1);
    NX_CHECK(suite.apps[0].instances[0].attempts.size() == 1);
    NX_CHECK(suite.runner_log.id == "run-1:hil:suite:runner:0:runner");
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
    runner::Suite_metadata extra{};
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
    runner::Suite_metadata empty_suite{};
    empty_suite.suite_id = "empty_artifacts";
    empty_suite.run_dir = fixture.root;
    nlohmann::json empty_json;
    runner::to_json(empty_json, empty_suite);
    write_json(empty_dir + "/suite.json", empty_json);
    NX_CHECK(discovery.rescan().has_value());
    bool found_empty = false;
    for (const hub::Hub_suite& candidate : discovery.tree().runs[0].suites) {
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

    runner::Suite_status degraded{};
    degraded.state = runner::Suite_state::Degraded;
    runner::Suite_app_status app{};
    app.app = "alpha";
    app.instance = "1";
    app.instance_dir = fixture.instance_dir;
    app.state = runner::App_state::Failed;
    degraded.apps.push_back(app);
    nlohmann::json document;
    runner::to_json(document, degraded);
    write_json(fixture.suite_dir + "/suite-state.json", document);
    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].suites[0].state ==
             runner::Suite_state::Degraded);
    NX_CHECK(discovery.tree().runs[0].suites[0].apps[0].state ==
             runner::App_state::Failed);

    runner::Suite_exit exit{};
    exit.state = runner::Suite_state::Stopped;
    exit.reason = "operator_stop";
    runner::to_json(document, exit);
    write_json(fixture.suite_dir + "/suite-exit.json", document);
    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].suites[0].state ==
             runner::Suite_state::Stopped);
    NX_CHECK(discovery.tree().runs[0].suites[0].reason == "operator_stop");

    std::error_code error;
    std::filesystem::remove(fixture.suite_dir + "/suite-state.json", error);
    std::filesystem::remove(fixture.suite_dir + "/suite-exit.json", error);
    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].suites[0].state ==
             runner::Suite_state::Unknown);
}

void test_streaming() {
    Fixture fixture("streaming");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::Stream_limits limits{};
    limits.ring_records = 2;
    limits.ring_bytes = 4096;
    hub::Stream_manager streaming(limits);
    streaming.sync(discovery.tree());
    streaming.poll();

    const std::string id = stream_id(fixture, "app");
    std::vector<hub::Stream_record> snapshot;
    std::optional<hub::Gap_marker> gap;
    std::uint64_t subscriber = streaming.subscribe(id, 0, snapshot, gap);
    NX_CHECK(subscriber > 0);
    NX_CHECK(snapshot.size() == 2);
    NX_CHECK(snapshot.back().line == "{\"a\":2}");

    // Live append is delivered after the snapshot.
    write_text(fixture.attempt_dir + "/app.jsonl",
               "{\"a\":1}\n{\"a\":2}\n{\"a\":3}\n");
    streaming.poll();
    std::vector<hub::Stream_record> live;
    std::vector<hub::Gap_marker> gaps;
    streaming.take(subscriber, live, gaps);
    NX_CHECK(!live.empty());
    NX_CHECK(live.back().line == "{\"a\":3}");
    NX_CHECK(gaps.empty());

    // Ring overflow: newest records survive and an older cursor gets a gap.
    std::vector<hub::Stream_record> recent;
    std::optional<hub::Gap_marker> recent_gap;
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
    std::vector<hub::Stream_record> metrics_snapshot;
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
    hub::Stream_limits small{};
    small.subscriber_bytes = 1024;
    hub::Stream_manager slow(small);
    slow.sync(discovery.tree());
    slow.poll();
    std::vector<hub::Stream_record> slow_snapshot;
    std::optional<hub::Gap_marker> slow_gap;
    const std::uint64_t slow_id =
        slow.subscribe(stream_id(fixture, "app"), 0, slow_snapshot, slow_gap);
    std::string many;
    for (int index = 0; index < 40; ++index) {
        many += "{\"slow\":" + std::to_string(index) + "}\n";
    }
    write_text(fixture.attempt_dir + "/app.jsonl", many);
    slow.poll();
    NX_CHECK(slow.overflowed(slow_id));
    std::vector<hub::Stream_record> other_snapshot;
    std::optional<hub::Gap_marker> other_gap;
    const std::uint64_t other_id =
        slow.subscribe(stream_id(fixture, "stdout"), 0, other_snapshot,
                       other_gap);
    NX_CHECK(!slow.overflowed(other_id));
    NX_CHECK(!other_snapshot.empty());

    // Byte capacity drops oldest records even when the count fits.
    write_text(fixture.attempt_dir + "/metrics.jsonl",
               std::string(100, 'a') + "\n" + std::string(100, 'b') + "\n");
    hub::Stream_limits byte_limits{};
    byte_limits.ring_bytes = 256;
    hub::Stream_manager bytes(byte_limits);
    bytes.sync(discovery.tree());
    bytes.poll();
    std::vector<hub::Stream_record> byte_records;
    std::optional<hub::Gap_marker> byte_gap;
    bytes.recent(stream_id(fixture, "metrics"), 10, byte_records, byte_gap);
    NX_CHECK(byte_records.size() == 1);
}

void test_web_assets() {
    NX_CHECK(contains(hub::index_html(), "/app.js"));
    NX_CHECK(contains(hub::index_html(), "/style.css"));
    NX_CHECK(contains(hub::app_js(), "pausedLines"));
    NX_CHECK(contains(hub::app_js(), "scheduleReconnect"));
    NX_CHECK(contains(hub::app_js(), "lastSequence"));
    NX_CHECK(contains(hub::app_js(), "EventSource"));
    NX_CHECK(!contains(hub::app_js(), "/api/start"));
    NX_CHECK(contains(hub::style_css(), "--accent"));
}

void test_http_server() {
    Fixture fixture("http");
    hub::Discovery discovery;
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());
    hub::Stream_manager streaming;
    streaming.sync(discovery.tree());
    streaming.poll();
    hub::Http_server server(discovery, streaming);
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

    NX_CHECK(contains(request("GET", "/api/health"), "\"status\":\"ok\""));
    NX_CHECK(contains(request("GET", "/api/tree"), "\"suite_id\":\"hil\""));
    NX_CHECK(contains(request("GET", "/api/streams"), "run-1:hil:alpha:1:1:app"));
    NX_CHECK(contains(request("GET", "/api/streams/" + stream_id(fixture, "app") +
                                  "/recent?limit=1"),
                              "\"records\""));
    NX_CHECK(contains(request("GET", "/"), "<!doctype html>"));
    NX_CHECK(contains(request("GET", "/app.js"), "EventSource"));
    NX_CHECK(contains(request("GET", "/style.css"), "--accent"));
    NX_CHECK(contains(request("POST", "/api/tree"), "405"));
    NX_CHECK(contains(request("GET", "/api/missing"), "404"));
    NX_CHECK(contains(request("GET", "/" + std::string(5000, 'a')), "400"));
    NX_CHECK(contains(request("GET", "/api/health",
                              std::string(9000, 'x')), "400"));

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
    run_suite("http_server", test_http_server);
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
