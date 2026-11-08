// Split from hub_test.cpp: nexigen::hub model serialization.

#include "hub_test_support.hpp"

void test_model_serialization() {
    for (const auto kind : {hub::StreamKind::App, hub::StreamKind::Metrics, hub::StreamKind::Traces,
                            hub::StreamKind::Chrono, hub::StreamKind::Stdout,
                            hub::StreamKind::Stderr, hub::StreamKind::Runner}) {
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
    NX_CHECK(round_tree.runs[0].suites[0].apps[0].instances[0].attempts[0].streams[0].kind ==
             hub::StreamKind::App);

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
    hub::from_json(nlohmann::json{{"id", 5}, {"kind", 7}, {"path", false}}, bad_stream);
    NX_CHECK(bad_stream.id == "keep");

    hub::HubAttempt bad_attempt;
    bad_attempt.attempt = 9;
    hub::from_json(nlohmann::json::array(), bad_attempt);
    hub::from_json(nlohmann::json{{"attempt", "x"}, {"artifact_dir", 4}, {"streams", 3}},
                   bad_attempt);
    NX_CHECK(bad_attempt.attempt == 9);

    hub::HubInstance bad_instance;
    bad_instance.instance = "keep";
    hub::from_json(nlohmann::json::array(), bad_instance);
    hub::from_json(
        nlohmann::json{{"instance", 1}, {"instance_dir", true}, {"state", 4}, {"attempts", 3}},
        bad_instance);
    NX_CHECK(bad_instance.instance == "keep");

    hub::HubApp bad_app;
    bad_app.app = "keep";
    hub::from_json(nlohmann::json::array(), bad_app);
    hub::from_json(nlohmann::json{{"app", 1}, {"state", false}, {"instances", 2}}, bad_app);
    NX_CHECK(bad_app.app == "keep");

    hub::HubSuite bad_suite;
    bad_suite.suite_id = "keep";
    hub::from_json(nlohmann::json::array(), bad_suite);
    hub::from_json(nlohmann::json{{"suite_id", 1},
                                  {"suite_dir", 2},
                                  {"artifacts_path", 3},
                                  {"state", 4},
                                  {"suite_restarts_used", std::uint64_t{4294967296ULL}},
                                  {"reason", 5},
                                  {"runner_log", 6},
                                  {"apps", 7}},
                   bad_suite);
    NX_CHECK(bad_suite.suite_id == "keep");

    hub::HubRun bad_run;
    bad_run.run_key = "keep";
    hub::from_json(nlohmann::json::array(), bad_run);
    hub::from_json(
        nlohmann::json{{"run_key", 1}, {"run_dir", 2}, {"suites", 3}, {"standalone_instances", 4}},
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

void run_hub_model_suites() {
    run_suite("model_serialization", test_model_serialization);
}
