// Split from hub_test.cpp: nexigen::hub stream management.

#include "hub_test_support.hpp"

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
    write_text(fixture.attempt_dir + "/app.jsonl", "{\"a\":1}\n{\"a\":2}\n{\"a\":3}\n");
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
    static_cast<void>(streaming.subscribe(stream_id(fixture, "metrics"), 0, metrics_snapshot, gap));
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
    std::filesystem::rename(replacement, fixture.attempt_dir + "/stdout.log", rename_error);
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
        slow.subscribe(stream_id(fixture, "stdout"), 0, other_snapshot, other_gap);
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
    const std::uint64_t long_id =
        long_lines.subscribe(stream_id(fixture, "app"), 0, long_snapshot, long_gap);
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
    const std::uint64_t unknown_id =
        long_lines.subscribe("missing-stream", 5, unknown_snapshot, unknown_gap);
    NX_CHECK(unknown_snapshot.empty());
    NX_CHECK(unknown_id > 0);
    long_lines.unsubscribe(unknown_id);

    // A descriptor path change reopens the stream at the new file.
    const std::string copy_path = fixture.attempt_dir + "/app-copy.jsonl";
    write_text(copy_path, "copied-line\n");
    hub::HubTree changed = discovery.tree();
    auto& changed_streams = changed.runs[0].suites[0].apps[0].instances[0].attempts[0].streams;
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
        hub::HubStream{"missing:app", hub::StreamKind::App, fixture.root + "/missing-app.jsonl"});
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
    transient_run.standalone_instances.push_back(std::move(transient_instance));
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
    directory_attempt.streams.push_back(
        hub::HubStream{"directory:app", hub::StreamKind::App, fixture.attempt_dir});
    directory_instance.attempts.push_back(std::move(directory_attempt));
    directory_run.standalone_instances.push_back(std::move(directory_instance));
    directory_tree.runs.push_back(std::move(directory_run));
    NX_CHECK(directory_streams.sync(directory_tree).has_value());
    directory_streams.poll();
    NX_CHECK(diagnostic_contains(read_capture, "event=stream_read_failed"));
}

void run_hub_streaming_suites() {
    run_suite("streaming", test_streaming);
}
