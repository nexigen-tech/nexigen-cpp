// Split from hub_test.cpp: nexigen::hub discovery tree and containment.

#include "hub_test_support.hpp"

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
    std::vector<std::string> multiple_roots{fixture.root, second.root, fixture.root};
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
    NX_CHECK(discovery.tree().runs[0].suites[0].apps[0].instances[0].attempts.size() == 2);

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
    NX_CHECK(hub::find_stream(discovery.tree(), "run-1:standalone:solo:1:1:app") != nullptr);

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
    for (const hub::HubInstance& instance : discovery.tree().runs[0].standalone_instances) {
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
    NX_CHECK(discovery.tree().runs[0].suites[0].state == runner::SuiteState::Degraded);
    NX_CHECK(discovery.tree().runs[0].suites[0].apps[0].state == runner::AppState::Failed);

    runner::SuiteExit exit{};
    exit.state = runner::SuiteState::Stopped;
    exit.reason = "operator_stop";
    runner::to_json(document, exit);
    write_json(fixture.suite_dir + "/suite-exit.json", document);
    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].suites[0].state == runner::SuiteState::Stopped);
    NX_CHECK(discovery.tree().runs[0].suites[0].reason == "operator_stop");

    std::error_code error;
    std::filesystem::remove(fixture.suite_dir + "/suite-state.json", error);
    std::filesystem::remove(fixture.suite_dir + "/suite-exit.json", error);
    NX_CHECK(discovery.rescan().has_value());
    NX_CHECK(discovery.tree().runs[0].suites[0].state == runner::SuiteState::Unknown);

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
    NX_CHECK(combined_state(runner::AppState::Restarting) == runner::SuiteState::Running);
    NX_CHECK(combined_state(runner::AppState::Failed) == runner::SuiteState::Failed);
    NX_CHECK(combined_state(runner::AppState::Stopped) == runner::SuiteState::Unknown);
    NX_CHECK(combined_state(runner::AppState::Exited) == runner::SuiteState::Unknown);
    NX_CHECK(combined_state(runner::AppState::Running) == runner::SuiteState::Running);
}

// --- Suite-metadata containment (change fix-p0-critical-defects, spec
// hub/discovery): uncontained suite.json paths degrade gracefully — the
// suite stays visible with no streams, nothing outside the run root is
// read, and affected instances report unknown state.
void test_discovery_containment() {
    hub::Discovery discovery;
    Fixture fixture("containment");
    std::vector<std::string> roots{fixture.root};
    NX_CHECK(discovery.initialize(roots).has_value());

    // The discovered hostile suite must stay visible while nothing outside
    // the root is stored in the tree.
    auto scanned = [&](const hub::HubTree& tree) -> const hub::HubSuite* {
        if (tree.runs.size() != 1U) {
            return nullptr;
        }
        for (const hub::HubSuite& suite : tree.runs[0].suites) {
            if (suite.suite_id == "hostile") {
                return &suite;
            }
        }
        return nullptr;
    };
    auto write_hostile = [&](const std::string& name, runner::SuiteMetadata& metadata) {
        nlohmann::json json;
        runner::to_json(json, metadata);
        make_dirs(fixture.root + "/.suite/" + name);
        write_json(fixture.root + "/.suite/" + name + "/suite.json", json);
    };
    auto remove_hostile = [&](const std::string& name) {
        remove_tree(fixture.root + "/.suite/" + name);
    };

    // (a) Absolute external artifacts root (/etc exists and is readable —
    // only the containment gate stops the hub from listing it).
    runner::SuiteMetadata metadata{};
    metadata.suite_id = "hostile";
    metadata.run_dir = fixture.root;
    metadata.artifacts_path = "/etc";
    write_hostile("external", metadata);
    NX_CHECK(discovery.rescan().has_value());
    const hub::HubSuite* external_suite = scanned(discovery.tree());
    NX_CHECK(external_suite != nullptr);
    if (external_suite != nullptr) {
        NX_CHECK(external_suite->artifacts_path.empty());
        NX_CHECK(external_suite->runner_log.id.empty());
        NX_CHECK(external_suite->runner_log.path.empty());
        // Only the contained fixture suite exposes streams (6 attempt + 1
        // runner); the external root contributes none.
        NX_CHECK(hub::all_streams(discovery.tree()).size() == 7);
    }
    remove_hostile("external");
    NX_CHECK(discovery.rescan().has_value());

    // (b) `..` escape from inside the root: the artifact root is recorded as
    // a root-relative path that climbs out (`/.suite/../..`), resolving to a
    // sibling of the unique test root.
    const std::string escape_anchor =
        "/tmp/opencode/containment_escape_" + std::to_string(static_cast<long long>(::getpid()));
    metadata.artifacts_path = fixture.root + "/.suite/../../containment_escape_" +
                              std::to_string(static_cast<long long>(::getpid()));
    make_dirs(escape_anchor);
    write_hostile("escape", metadata);
    NX_CHECK(discovery.rescan().has_value());
    const hub::HubSuite* escape_suite = scanned(discovery.tree());
    NX_CHECK(escape_suite != nullptr);
    if (escape_suite != nullptr) {
        NX_CHECK(escape_suite->artifacts_path.empty());
        NX_CHECK(escape_suite->runner_log.id.empty());
        NX_CHECK(escape_suite->runner_log.path.empty());
        NX_CHECK(hub::all_streams(discovery.tree()).size() == 7);
    }
    remove_hostile("escape");
    remove_tree(escape_anchor);
    NX_CHECK(discovery.rescan().has_value());

    // (c+d) Hostile app content: an escaped per-app `instance_dir` (/etc)
    // must not be read and reports unknown state, and an `instance` string
    // that composes the attempts base outside the root yields no attempts.
    metadata.artifacts_path = fixture.artifacts;
    make_dirs(fixture.artifacts + "/hostile/leaker");
    runner::SuiteAppMetadata leaker{};
    leaker.app = "leaker";
    leaker.instance = "../../../../../etc"; // base escapes the root
    leaker.instance_dir = "/etc";           // instance_dir outside root
    metadata.apps.push_back(leaker);
    write_hostile("hostile", metadata);
    NX_CHECK(discovery.rescan().has_value());
    const hub::HubSuite* hostile_suite = scanned(discovery.tree());
    NX_CHECK(hostile_suite != nullptr);
    if (hostile_suite != nullptr) {
        NX_CHECK(hostile_suite->apps.size() == 1);
        // The contained artifacts root still yields the runner log once
        // runner.jsonl exists — until then the derived target is missing and
        // stays empty (spec: missing targets are not read).
        NX_CHECK(hostile_suite->runner_log.id.empty());
        if (!hostile_suite->apps.empty() && !hostile_suite->apps[0].instances.empty()) {
            NX_CHECK(hostile_suite->apps[0].instances[0].instance_dir.empty());
            NX_CHECK(hostile_suite->apps[0].instances[0].state == runner::AppState::Unknown);
            NX_CHECK(hostile_suite->apps[0].instances[0].attempts.empty());
        }
        // No foreign stream may exist for the hostile app's attempts.
        for (const hub::HubStream& stream : hub::all_streams(discovery.tree())) {
            NX_CHECK(stream.id.find(":leaker:") == std::string_view::npos);
        }
    }

    // Creating the runner log inside the contained artifacts root attaches
    // the runner stream on the next scan (derived-path containment positive).
    write_text(fixture.artifacts + "/hostile/runner.jsonl", "{}\n");
    NX_CHECK(discovery.rescan().has_value());
    hostile_suite = scanned(discovery.tree());
    NX_CHECK(hostile_suite != nullptr);
    if (hostile_suite != nullptr) {
        NX_CHECK(hostile_suite->runner_log.id == "run-1:hostile:suite:runner:0:runner");
        NX_CHECK(hostile_suite->runner_log.path == fixture.artifacts + "/hostile/runner.jsonl");
    }
    remove_hostile("hostile");
    remove_tree(fixture.artifacts + "/hostile");
    NX_CHECK(discovery.rescan().has_value());

    // The contained fixture suite still discovers everything (containment
    // follows the runner-created default per the spec scenario).
    const hub::HubTree& tree = discovery.tree();
    NX_CHECK(tree.runs.size() == 1);
    NX_CHECK(hub::all_streams(tree).size() == 7);
    NX_CHECK(tree.runs[0].suites[0].runner_log.path == fixture.artifacts + "/hil/runner.jsonl");
    NX_CHECK(tree.runs[0].suites[0].apps[0].instances[0].attempts.size() == 1);

    // Missing artifact root is tolerated (retained spec scenario): suite
    // visible, no streams, no startup failure.
    runner::SuiteMetadata missing{};
    missing.suite_id = "missing";
    missing.run_dir = fixture.root;
    missing.artifacts_path = fixture.root + "/.suite/no_such_artifacts";
    write_hostile("missing", missing);
    NX_CHECK(discovery.rescan().has_value());
    bool found_missing = false;
    for (const hub::HubSuite& suite : discovery.tree().runs[0].suites) {
        if (suite.suite_id == "missing") {
            found_missing = true;
            NX_CHECK(suite.apps.empty());
            NX_CHECK(suite.runner_log.id.empty());
        }
    }
    NX_CHECK(found_missing);
    NX_CHECK(hub::all_streams(discovery.tree()).size() == 7);
    remove_hostile("missing");

    std::printf("containment: /etc artifacts, root escape, escaped instance "
                "state unknown, missing root tolerated\n");
}

void run_hub_discovery_suites() {
    run_suite("discovery", test_discovery);
    run_suite("discovery_containment", test_discovery_containment);
    run_suite("discovery_states", test_discovery_states);
}
