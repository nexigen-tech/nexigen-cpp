// Runner ownership, wait outcomes, and stop behaviour tests
// (change add-synchro-event-sync; spec synchro/consumer; tasks 6.1-6.4).
#include "synchro_test_support.hpp"

#include <chrono>
#include <cstdint>
#include <stop_token>
#include <string>
#include <thread>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/com/event_consumer_set.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/wait_outcome.hpp>
#include <nexigen/synchro/sync_runner.hpp>

namespace {

using namespace nexigen;
using synchro::SyncRunner;

std::uint64_t runner_now_ns() {
    const auto now = chrono::Timestamp::now();
    return now.has_value() ? now->nanoseconds() : 0U;
}

synchro::SyncPayload runner_payload(std::uint32_t phase) {
    synchro::SyncPayload payload{};
    payload.cycle_number = 0U;
    payload.phase_index = phase;
    payload.phases_total = 10U;
    payload.scheduled_time_ns = runner_now_ns();
    payload.cycle_period_ns = 10'000'000U;
    return payload;
}

void test_create_and_ownership() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    const auto service = unique_service("run-create");
    auto created = SyncRunner<CaptureLogger>::create(*node, make_consumer_config(service), logger,
                                                     *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& runner = **created;

    NX_CHECK_EQ(runner.wait_set().size(), 1U);
    NX_CHECK_EQ(runner.wait_set().capacity(), 16U);
    NX_CHECK(runner.consumer().attached_to_set());
    NX_CHECK(runner.to_string().find("consumers=1") != std::string::npos);
    NX_CHECK(std::format("{}", runner) == runner.to_string());
    NX_CHECK_EQ(runner.stats().received, 0U);
    NX_CHECK(runner.consumer().to_string().find(service) != std::string::npos);

    // The consumer is already owned by this runner's set: a second set must
    // reject the same ownership slot.
    com::EventConsumerSet<1> rival{*node};
    NX_CHECK(!rival.attach(runner.consumer()).has_value());

    NX_CHECK(!SyncRunner<CaptureLogger>::create(
                  *node,
                  [&] {
                      auto bad = make_consumer_config(unique_service("run-bad"));
                      bad.channel.cycle_period_ns = 0U;
                      return bad;
                  }(),
                  logger, *meter.meter)
                  .has_value());
}

void test_run_once_timeout_and_wake() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    auto config = make_consumer_config(unique_service("run-timeout"));
    config.initial_sync_timeout_ns = 20'000'000U;

    auto created = SyncRunner<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& runner = **created;

    const auto begin = std::chrono::steady_clock::now();
    auto outcome = runner.run_once();
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    NX_CHECK(outcome.has_value());
    if (outcome.has_value()) {
        NX_CHECK(*outcome == com::WaitOutcome::Timeout);
    }
    // The wait honors the watchdog-derived budget instead of busy-spinning; the
    // 20 ms window may elapse, but the call must not block for seconds.
    NX_CHECK(elapsed < std::chrono::milliseconds{1'000});

    // A pending wake is consumed by the next wait.
    runner.wake_up();
    outcome = runner.run_once();
    NX_CHECK(outcome.has_value());
    if (outcome.has_value()) {
        NX_CHECK(*outcome == com::WaitOutcome::WokenUp);
    }
}

void test_run_once_events() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    const auto service = unique_service("run-events");
    auto config = make_consumer_config(service);
    config.initial_sync_timeout_ns = 200'000'000U;

    auto channel = com::EventChannel<synchro::SyncPayload>::create(
        *node, *com::ServiceName::create(service.c_str()), config.channel.channel);
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    auto produced = channel->producer();
    NX_CHECK(produced.has_value());
    if (!produced.has_value()) {
        return;
    }

    auto created = SyncRunner<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& runner = **created;

    std::uint32_t delivered = 0U;
    NX_CHECK(runner.consumer()
                 .on_any_phase([&delivered](const synchro::SyncContext&) { ++delivered; })
                 .has_value());

    const std::uint64_t header = runner_now_ns();
    auto payload = runner_payload(0U);
    payload.scheduled_time_ns = header;
    NX_CHECK(produced
                 ->publish(payload, 0x53594E43U,
                           com::EventTime{header, com::TimestampDomain::MonotonicRaw})
                 .has_value());

    auto outcome = runner.run_once();
    NX_CHECK(outcome.has_value());
    if (outcome.has_value()) {
        NX_CHECK(*outcome == com::WaitOutcome::Events);
    }
    NX_CHECK_EQ(delivered, 1U);
    NX_CHECK_EQ(runner.stats().received, 1U);
    NX_CHECK(runner.consumer().state() == synchro::SyncConsumerState::Locked);
}

void test_run_stops_promptly() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    const auto service = unique_service("run-stop");
    auto config = make_consumer_config(service);
    config.initial_sync_timeout_ns = 60'000'000'000ULL; // 60 s: the wake must win
    config.sync_loss_timeout_ns = 60'000'000'000ULL;

    auto channel = com::EventChannel<synchro::SyncPayload>::create(
        *node, *com::ServiceName::create(service.c_str()), config.channel.channel);
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    auto produced = channel->producer();
    NX_CHECK(produced.has_value());
    if (!produced.has_value()) {
        return;
    }

    auto created = SyncRunner<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& runner = **created;

    std::atomic<std::uint32_t> delivered{0};
    NX_CHECK(runner.consumer()
                 .on_any_phase([&delivered](const synchro::SyncContext&) {
                     delivered.fetch_add(1U, std::memory_order_relaxed);
                 })
                 .has_value());

    std::stop_source stop{};
    std::jthread thread{[&runner, token = stop.get_token()] { runner.run(token); }};

    // Give the loop a moment to enter the wait, then publish an event.
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    const std::uint64_t header = runner_now_ns();
    auto payload = runner_payload(0U);
    payload.scheduled_time_ns = header;
    NX_CHECK(produced
                 ->publish(payload, 0x53594E43U,
                           com::EventTime{header, com::TimestampDomain::MonotonicRaw})
                 .has_value());

    NX_CHECK(wait_for([&delivered] { return delivered.load(std::memory_order_relaxed) >= 1U; },
                      std::chrono::milliseconds{1'000}));

    const auto stop_begin = std::chrono::steady_clock::now();
    stop.request_stop();
    thread.join();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_begin;
    NX_CHECK(stop_elapsed < std::chrono::milliseconds{1'000});
    NX_CHECK(delivered.load(std::memory_order_relaxed) >= 1U);
    NX_CHECK(captured_contains("sync runner started"));
    NX_CHECK(captured_contains("sync runner stopped"));
}

} // namespace

void run_runner_suite() {
    run_suite("runner ownership", test_create_and_ownership);
    run_suite("runner timeout/wake", test_run_once_timeout_and_wake);
    run_suite("runner events", test_run_once_events);
    run_suite("runner stop", test_run_stops_promptly);
}
