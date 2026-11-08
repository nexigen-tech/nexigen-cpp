// Split from metrics_test.cpp: nexigen::metrics StateGauge.

#include "metrics_test_support.hpp"

namespace {

void test_state_gauge() {
    metrics::NullMetricSink sink;
    metrics::MetricProviderConfig config{};
    config.max_states = 8;
    metrics::MetricProvider provider{config, sink};
    const Result<metrics::Meter*> meter = provider.get_meter("engine", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter) {
        return;
    }

    // Recording surface: valid set, and empty / over-long rejection with no
    // side effect on the previous value.
    const auto state = (*meter)->create_state<ST, Readable, Cumulative>("mode", "state");
    NX_CHECK(state.has_value());
    if (!state) {
        return;
    }
    NX_CHECK((*state)->set("active"));
    NX_CHECK((*state)->read() == "active");
    NX_CHECK(!(*state)->set(""));
    NX_CHECK((*state)->read() == "active");
    NX_CHECK(!(*state)->set(std::string_view(std::string(64, 'x'))));
    NX_CHECK((*state)->read() == "active");
    NX_CHECK((*state)->set(std::string_view(std::string(63, 'y'))));
    NX_CHECK((*state)->read().size() == 63);

    // Snapshot carries the kind and the value.
    (*state)->set("degraded");
    const metrics::MetricRecord record = (*state)->snapshot();
    NX_CHECK(record.kind == metrics::MetricKind::State);
    NX_CHECK(metrics::detail::view_name(record.state.value) == "degraded");
    NX_CHECK(metrics::detail::view_name(record.identity.unit) == "state");

    // Single-threaded last-writer-wins.
    (*state)->set("first");
    (*state)->set("second");
    NX_CHECK((*state)->read() == "second");

    // Cumulative observations persist until the next set.
    (*state)->set("persist");
    NX_CHECK((*state)->read() == "persist");
    NX_CHECK((*state)->read() == "persist");

    // Reset-on-read reports the value and then clears to empty.
    const auto windowed = (*meter)->create_state<ST, Readable, ResetOnRead>("phase");
    NX_CHECK(windowed.has_value());
    if (!windowed) {
        return;
    }
    (*windowed)->set("armed");
    NX_CHECK((*windowed)->read() == "armed");
    NX_CHECK((*windowed)->read().empty());

    // Serialization contract: member/free to_string, std::format and write-only
    // JSON all render the value and the classification.
    (*state)->set("active");
    const std::string text = (*state)->to_string();
    NX_CHECK(!text.empty());
    NX_CHECK(text.find("State") != std::string::npos);
    NX_CHECK(metrics::to_string(**state) == text);
    NX_CHECK(std::format("{}", **state) == text);
    nlohmann::json j = **state;
    NX_CHECK(j.is_object());
    NX_CHECK(j["kind"] == "State");
    NX_CHECK(j["value"] == "active");

    // Multi-threaded last-writer-wins: two writers race distinct full-length
    // values while a reader never observes a torn mix.
    const auto mt = (*meter)->create_state<MT, Readable, Cumulative>("state_mt");
    NX_CHECK(mt.has_value());
    if (!mt) {
        return;
    }
    const std::string a = std::string(63, 'A');
    const std::string b = std::string(63, 'B');
    std::atomic<bool> stop{false};
    std::jthread writer_a([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            (*mt)->set(a);
        }
    });
    std::jthread writer_b([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            (*mt)->set(b);
        }
    });
    const auto is_clean = [](std::string_view v, char c) {
        for (const char ch : v) {
            if (ch != c) {
                return false;
            }
        }
        return true;
    };
    for (int i = 0; i < 200'000; ++i) {
        const std::string_view observed = (*mt)->read();
        NX_CHECK(is_clean(observed, 'A') || is_clean(observed, 'B'));
    }
    stop.store(true, std::memory_order_relaxed);
    writer_a.join();
    writer_b.join();
    (*mt)->set("final");
    NX_CHECK((*mt)->read() == "final");

    // Duplicate names across kinds under one meter are rejected.
    const auto dup = (*meter)->create_state<ST, Readable, Cumulative>("mode");
    NX_CHECK(!dup.has_value());
    NX_CHECK(dup.error() == ErrorCode::InvalidArgument);

    // State record JSON round-trip (spec metrics/metric-provider).
    {
        metrics::MetricRecord round{};
        round.kind = metrics::MetricKind::State;
        NX_CHECK(metrics::detail::copy_name(round.state.value, "running"));
        nlohmann::json json;
        metrics::to_json(json, round);
        metrics::MetricRecord back{};
        metrics::from_json(json, back);
        NX_CHECK(back.kind == metrics::MetricKind::State);
        NX_CHECK(metrics::detail::view_name(back.state.value) == "running");
        NX_CHECK(round.to_string().find("State") != std::string::npos);
    }
}

// --- descriptive statistics (spec metrics/descriptive-statistics).

} // namespace

void suite_state_gauge() {
    run_suite("state_gauge", test_state_gauge);
}
