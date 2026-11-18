// Consumer continuity, dispatch, latency, watchdog, and telemetry tests
// (change add-synchro-event-sync; spec synchro/consumer; tasks 5.1-5.7).
#include "synchro_test_support.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event_consumer_set.hpp>
#include <nexigen/com/loss_info.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/metrics/concurrency_policy.hpp>
#include <nexigen/synchro/detail/sync_faults.hpp>
#include <nexigen/synchro/late_event_policy.hpp>
#include <nexigen/synchro/sync_consumer.hpp>
#include <nexigen/synchro/sync_consumer_state.hpp>
#include <nexigen/synchro/sync_context.hpp>
#include <nexigen/synchro/sync_error_kind.hpp>

namespace {

using namespace nexigen;
using synchro::SyncConsumer;

static_assert(std::is_base_of_v<com::IEventConsumer, SyncConsumer<CaptureLogger>>);
static_assert(!std::is_copy_constructible_v<SyncConsumer<CaptureLogger>>);
static_assert(!std::is_move_constructible_v<SyncConsumer<CaptureLogger>>);

std::uint64_t now_ns() {
    const auto now = chrono::Timestamp::now();
    return now.has_value() ? now->nanoseconds() : 0U;
}

synchro::SyncPayload make_payload(std::uint64_t cycle, std::uint32_t phase,
                                  std::uint32_t phases_total, std::uint64_t scheduled,
                                  std::uint64_t cycle_period) {
    synchro::SyncPayload payload{};
    payload.cycle_number = cycle;
    payload.phase_index = phase;
    payload.phases_total = phases_total;
    payload.scheduled_time_ns = scheduled;
    payload.cycle_period_ns = cycle_period;
    return payload;
}

bool publish(com::EventProducer<synchro::SyncPayload>& producer,
             const synchro::SyncPayload& payload, std::uint64_t header_ns) {
    auto outcome = producer.publish(payload, 0x53594E43U,
                                    com::EventTime{header_ns, com::TimestampDomain::MonotonicRaw});
    return outcome.has_value();
}

void test_create_rejects_invalid_config() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    auto config = make_consumer_config(unique_service("con-invalid"));
    config.channel.phases_per_cycle = 0U;

    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(!created.has_value());
    if (!created.has_value()) {
        NX_CHECK(created.error().kind == synchro::SyncErrorKind::InvalidConfig);
    }

    auto policy = make_consumer_config(unique_service("con-policy"));
    policy.late_policy = static_cast<synchro::LateEventPolicy>(42);
    auto rejected = SyncConsumer<CaptureLogger>::create(*node, policy, logger, *meter.meter);
    NX_CHECK(!rejected.has_value());
}

void test_create_rejects_duplicate_metrics() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    NX_CHECK(meter.meter
                 ->create_counter<metrics::ConcurrencyPolicy::MultiThreaded>(
                     "sync_consumer_received_total")
                 .has_value());

    const auto service = unique_service("con-metrics");
    auto config = make_consumer_config(service);
    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(!created.has_value());
    if (!created.has_value()) {
        NX_CHECK(created.error().kind == synchro::SyncErrorKind::Metrics);
        NX_CHECK(created.error().core == core::ErrorCode::InvalidArgument);
    }

    MeterFixture fresh{};
    auto retried = SyncConsumer<CaptureLogger>::create(*node, config, logger, *fresh.meter);
    NX_CHECK(retried.has_value());
    if (retried.has_value()) {
        NX_CHECK((*retried)->state() == synchro::SyncConsumerState::Unsynced);
        NX_CHECK_EQ((*retried)->received(), 0U);
    }
}

void test_consumer_instruments_are_registered() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    auto created = SyncConsumer<CaptureLogger>::create(
        *node, make_consumer_config(unique_service("con-instruments")), logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }

    using metrics::ConcurrencyPolicy;
    constexpr auto mt = ConcurrencyPolicy::MultiThreaded;
    NX_CHECK(!meter.meter->create_counter<mt>("sync_consumer_received_total").has_value());
    NX_CHECK(!meter.meter->create_counter<mt>("sync_consumer_dropped_late_total").has_value());
    NX_CHECK(!meter.meter->create_counter<mt>("sync_consumer_transport_lost_total").has_value());
    NX_CHECK(!meter.meter->create_counter<mt>("sync_consumer_generator_skips_total").has_value());
    static constexpr double bounds[] = {-1'000.0, 0.0, 1'000.0};
    NX_CHECK(!meter.meter->create_explicit_histogram<mt>("sync_consumer_ipc_latency_ns", bounds)
                  .has_value());
    NX_CHECK(!meter.meter->create_explicit_histogram<mt>("sync_consumer_e2e_latency_ns", bounds)
                  .has_value());
    NX_CHECK((!meter.meter->create_gauge<std::int64_t, mt>("sync_consumer_locked").has_value()));
}

void test_attachment_and_delegation() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    const auto service = unique_service("con-attach");
    auto config = make_consumer_config(service);

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

    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& consumer = **created;

    NX_CHECK(!consumer.attached_to_set());
    NX_CHECK_EQ(consumer.received(), 0U);
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK(consumer.to_string().find(service) != std::string::npos);
    NX_CHECK(std::format("{}", consumer) == consumer.to_string());

    // Drain without a set delivers queued events through the handlers.
    std::uint32_t seen = 0U;
    NX_CHECK(consumer.on_any_phase([&seen](const synchro::SyncContext&) { ++seen; }).has_value());
    NX_CHECK(publish(*produced, make_payload(0U, 0U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK_EQ(seen, 1U);
    NX_CHECK_EQ(consumer.received(), 1U);

    com::EventConsumerSet<1> first_set{*node};
    com::EventConsumerSet<1> second_set{*node};
    NX_CHECK(first_set.attach(consumer).has_value());
    NX_CHECK(consumer.attached_to_set());
    NX_CHECK_EQ(first_set.size(), 1U);
    NX_CHECK(!second_set.attach(consumer).has_value());

    // While attached, the set owns waiting and draining.
    NX_CHECK(publish(*produced, make_payload(0U, 1U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(first_set.wait_for(std::chrono::milliseconds{50}).has_value());
    NX_CHECK_EQ(seen, 2U);
    NX_CHECK_EQ(consumer.received(), 2U);

    NX_CHECK(first_set.detach(consumer).has_value());
    NX_CHECK(!consumer.attached_to_set());
    NX_CHECK_EQ(first_set.size(), 0U);
}

void test_handler_registration_and_dispatch_order() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    const auto service = unique_service("con-handlers");
    auto config = make_consumer_config(service);

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
    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& consumer = **created;

    std::vector<std::string> order{};
    NX_CHECK(
        consumer.on_any_phase([&order](const synchro::SyncContext&) { order.emplace_back("any"); })
            .has_value());
    NX_CHECK(
        consumer
            .on_cycle_start([&order](const synchro::SyncContext&) { order.emplace_back("cycle"); })
            .has_value());
    NX_CHECK(consumer
                 .on_phase(1U,
                           [&order](const synchro::SyncContext& context) {
                               order.emplace_back(context.is_late ? "one-late" : "one");
                           })
                 .has_value());

    const auto rejected = consumer.on_phase(10U, [](const synchro::SyncContext&) {});
    NX_CHECK(!rejected.has_value());
    if (!rejected.has_value()) {
        NX_CHECK(rejected.error().kind == synchro::SyncErrorKind::InvalidConfig);
    }

    // Phase 0: on_any_phase runs before on_cycle_start (alias for phase zero).
    NX_CHECK(publish(*produced, make_payload(0U, 0U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK_EQ(order.size(), 2U);
    NX_CHECK(order.size() == 2U && order[0] == "any" && order[1] == "cycle");

    // Phase 1: only the phase handler is added.
    order.clear();
    NX_CHECK(publish(*produced, make_payload(0U, 1U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK_EQ(order.size(), 2U);
    NX_CHECK(order.size() == 2U && order[0] == "any" && order[1] == "one");

    // Phase 2 has no dedicated handler: only the any-phase handler runs.
    order.clear();
    NX_CHECK(publish(*produced, make_payload(0U, 2U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK_EQ(order.size(), 1U);
    NX_CHECK(order.size() == 1U && order[0] == "any");

    // The context carries the payload identity and measured latencies.
    synchro::SyncContext last{};
    NX_CHECK(consumer.on_any_phase([&last](const synchro::SyncContext& context) { last = context; })
                 .has_value());
    const std::uint64_t header = now_ns();
    const auto payload = make_payload(3U, 4U, 10U, header, 10'000'000U);
    NX_CHECK(publish(*produced, payload, header));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK_EQ(last.cycle_number, 3U);
    NX_CHECK_EQ(last.phase_index, 4U);
    NX_CHECK_EQ(last.phases_total, 10U);
    NX_CHECK_EQ(last.scheduled_time_ns, header);
    NX_CHECK_EQ(last.publish_time_ns, header);
    NX_CHECK(last.receive_time_ns >= header);
    NX_CHECK(last.e2e_latency_ns >= 0);
    NX_CHECK(last.ipc_latency_ns >= 0);
    NX_CHECK(!last.is_late);
}

void test_continuity_and_generator_skips() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    const auto service = unique_service("con-continuity");
    auto config = make_consumer_config(service);

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
    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& consumer = **created;

    NX_CHECK(publish(*produced, make_payload(0U, 0U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(publish(*produced, make_payload(0U, 1U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK_EQ(consumer.received(), 2U);
    NX_CHECK_EQ(consumer.stats().generator_skips, 0U);
    NX_CHECK_EQ(consumer.stats().resyncs, 0U);
    NX_CHECK_EQ(consumer.stats().absolute_phase, 1U);

    // A contiguous transport sequence with a logical jump is a generator skip.
    NX_CHECK(publish(*produced, make_payload(0U, 3U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK_EQ(consumer.stats().generator_skips, 1U);
    NX_CHECK_EQ(consumer.stats().resyncs, 0U);
    NX_CHECK_EQ(consumer.stats().absolute_phase, 3U);
    NX_CHECK(captured_contains("skipped phase slots"));

    // A phase-count mismatch restarts continuity without inventing a skip.
    const auto skips_before = consumer.stats().generator_skips;
    NX_CHECK(publish(*produced, make_payload(1U, 0U, 20U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK_EQ(consumer.stats().resyncs, 1U);
    NX_CHECK_EQ(consumer.stats().generator_skips, skips_before);
}

void test_transport_gap_reconciliation() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    const auto service = unique_service("con-gap");
    auto config = make_consumer_config(service);
    config.channel.channel.queue_size = 1U;
    config.channel.channel.history_size = 0U;

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
    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& consumer = **created;

    // Baseline sample establishes the transport sequence.
    NX_CHECK(publish(*produced, make_payload(0U, 0U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK_EQ(consumer.received(), 1U);

    // Overfill the one-slot consumer queue: the transport drops old samples.
    NX_CHECK(publish(*produced, make_payload(0U, 1U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(publish(*produced, make_payload(0U, 2U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(publish(*produced, make_payload(0U, 3U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(consumer.drain().has_value());

    const auto stats = consumer.stats();
    NX_CHECK(stats.transport_lost >= 2U);
    NX_CHECK_EQ(stats.generator_skips, 0U);
    NX_CHECK_EQ(stats.resyncs, 0U);
    NX_CHECK_EQ(stats.absolute_phase, 3U);
}

void test_late_policy_drop() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    MeterFixture meter{};
    const auto service = unique_service("con-late-drop");
    auto config = make_consumer_config(service);
    config.max_allowed_latency_ns = 1'000'000U;
    config.late_policy = synchro::LateEventPolicy::DropAndWarn;

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
    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& consumer = **created;

    std::uint32_t delivered = 0U;
    NX_CHECK(consumer.on_any_phase([&delivered](const synchro::SyncContext&) { ++delivered; })
                 .has_value());

    const std::uint64_t header = now_ns();
    const auto late = make_payload(0U, 0U, 10U, header - 50'000'000U, 10'000'000U);
    NX_CHECK(publish(*produced, late, header));
    NX_CHECK(consumer.drain().has_value());

    NX_CHECK_EQ(delivered, 0U);
    NX_CHECK_EQ(consumer.received(), 1U);
    NX_CHECK_EQ(consumer.stats().dropped_late, 1U);
    NX_CHECK_EQ(consumer.stats().late_delivered, 0U);
    NX_CHECK(consumer.stats().last_e2e_latency_ns >= 50'000'000);
    NX_CHECK(captured_contains("dropped a late phase"));
}

void test_late_policy_deliver_and_negative_latency() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    const auto service = unique_service("con-late-deliver");
    auto config = make_consumer_config(service);
    config.max_allowed_latency_ns = 1'000'000U;
    config.late_policy = synchro::LateEventPolicy::DeliverWithFlag;

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
    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& consumer = **created;

    std::uint32_t delivered = 0U;
    bool last_late = false;
    NX_CHECK(consumer
                 .on_any_phase([&delivered, &last_late](const synchro::SyncContext& context) {
                     ++delivered;
                     last_late = context.is_late;
                 })
                 .has_value());

    const std::uint64_t header = now_ns();
    NX_CHECK(
        publish(*produced, make_payload(0U, 0U, 10U, header - 50'000'000U, 10'000'000U), header));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK_EQ(delivered, 1U);
    NX_CHECK(last_late);
    NX_CHECK_EQ(consumer.stats().late_delivered, 1U);

    // A future scheduled time yields a negative end-to-end latency, never late.
    NX_CHECK(
        publish(*produced, make_payload(0U, 1U, 10U, header + 5'000'000U, 10'000'000U), header));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK_EQ(delivered, 2U);
    NX_CHECK(!last_late);
    NX_CHECK(consumer.stats().last_e2e_latency_ns < 0);
    NX_CHECK_EQ(consumer.stats().late_delivered, 1U);
}

void test_watchdog_lock_loss_reacquire() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    MeterFixture meter{};
    const auto service = unique_service("con-watchdog");
    auto config = make_consumer_config(service);
    config.initial_sync_timeout_ns = 1U;
    config.sync_loss_timeout_ns = 1U;

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
    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& consumer = **created;

    NX_CHECK(consumer.state() == synchro::SyncConsumerState::Unsynced);
    NX_CHECK(consumer.check_sync_loss(now_ns()));
    NX_CHECK_EQ(consumer.stats().sync_losses, 1U);
    NX_CHECK(consumer.state() == synchro::SyncConsumerState::Unsynced);
    // The initial timeout is reported once, not on every check.
    NX_CHECK(!consumer.check_sync_loss(now_ns()));
    NX_CHECK_EQ(consumer.stats().sync_losses, 1U);

    NX_CHECK(publish(*produced, make_payload(0U, 0U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK(consumer.state() == synchro::SyncConsumerState::Locked);

    const std::uint64_t deadline = consumer.watchdog_deadline_ns();
    NX_CHECK(deadline <= now_ns() + 1U);
    NX_CHECK(consumer.check_sync_loss(deadline + 1U));
    NX_CHECK(consumer.state() == synchro::SyncConsumerState::Unsynced);
    NX_CHECK_EQ(consumer.stats().sync_losses, 2U);
    NX_CHECK(!consumer.check_sync_loss(deadline + 2U));

    NX_CHECK(publish(*produced, make_payload(0U, 1U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK(consumer.state() == synchro::SyncConsumerState::Locked);

    const auto timeout = consumer.next_wait_timeout();
    NX_CHECK(timeout.count() >= 1);
    NX_CHECK(timeout.count() <= 1'000);

    NX_CHECK(captured_contains("initial synchronization timeout"));
    NX_CHECK(captured_contains("lost synchronization"));
    NX_CHECK(captured_contains("acquired synchronization"));
    NX_CHECK(captured_contains("reacquired synchronization"));
}

void test_lock_gauge_follows_state() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    RecordingMeterFixture registry{};
    NX_CHECK(registry.meter != nullptr);
    if (registry.meter == nullptr) {
        return;
    }

    const auto service = unique_service("con-gauge");
    auto config = make_consumer_config(service);
    config.initial_sync_timeout_ns = 1U;
    config.sync_loss_timeout_ns = 1U;

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
    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *registry.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& consumer = **created;

    std::int64_t value = -1;
    NX_CHECK(registry.gauge_value("sync_consumer_locked", value));
    NX_CHECK_EQ(value, 0);

    // The initial timeout keeps the consumer unsynced and the gauge at zero.
    NX_CHECK(consumer.check_sync_loss(now_ns()));
    NX_CHECK(registry.gauge_value("sync_consumer_locked", value));
    NX_CHECK_EQ(value, 0);

    // A delivered event locks the consumer and flips the gauge to one.
    NX_CHECK(publish(*produced, make_payload(0U, 0U, 10U, now_ns(), 10'000'000U), now_ns()));
    NX_CHECK(consumer.drain().has_value());
    NX_CHECK(consumer.state() == synchro::SyncConsumerState::Locked);
    NX_CHECK(registry.gauge_value("sync_consumer_locked", value));
    NX_CHECK_EQ(value, 1);

    // Watchdog expiry returns to unsynced and the gauge follows.
    const std::uint64_t deadline = consumer.watchdog_deadline_ns();
    NX_CHECK(consumer.check_sync_loss(deadline + 1U));
    NX_CHECK(consumer.state() == synchro::SyncConsumerState::Unsynced);
    NX_CHECK(registry.gauge_value("sync_consumer_locked", value));
    NX_CHECK_EQ(value, 0);
}

void test_loss_counters() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    RecordingMeterFixture registry{};
    NX_CHECK(registry.meter != nullptr);
    if (registry.meter == nullptr) {
        return;
    }

    auto config = make_consumer_config(unique_service("con-loss"));
    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *registry.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& consumer = **created;

    com::LossInfo duplicate{};
    duplicate.reason = com::LossReason::Duplicate;
    duplicate.expected_sequence = 4U;
    duplicate.actual_sequence = 4U;
    consumer.inject_loss(duplicate);

    com::LossInfo reset{};
    reset.reason = com::LossReason::UnexpectedSequenceReset;
    reset.expected_sequence = 5U;
    reset.actual_sequence = 1U;
    consumer.inject_loss(reset);

    com::LossInfo gap{};
    gap.reason = com::LossReason::TransportGap;
    gap.expected_sequence = 6U;
    gap.actual_sequence = 9U;
    gap.lost_count = 3U;
    consumer.inject_loss(gap);

    const auto stats = consumer.stats();
    NX_CHECK_EQ(stats.duplicates, 1U);
    NX_CHECK_EQ(stats.unexpected_resets, 1U);
    NX_CHECK_EQ(stats.transport_lost, 3U);
    NX_CHECK_EQ(stats.producer_restarts, 0U);
    NX_CHECK_EQ(captured_count("dropped a duplicate"), 1U);
    NX_CHECK_EQ(captured_count("unexpected sequence reset"), 1U);
    NX_CHECK_EQ(captured_count("transport gap"), 1U);

    std::uint64_t metric_lost = 0U;
    NX_CHECK(registry.counter_value("sync_consumer_transport_lost_total", metric_lost));
    NX_CHECK_EQ(metric_lost, 3U);
}

void test_create_maps_service_name_failure() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    RecordingMeterFixture registry{};
    NX_CHECK(registry.meter != nullptr);
    if (registry.meter == nullptr) {
        return;
    }

    // Non-empty, so config validation passes, but beyond the transport limit.
    const std::string oversized(4'096U, 'a');
    auto config = make_consumer_config(oversized);
    NX_CHECK(config.validate().has_value());

    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *registry.meter);
    NX_CHECK(!created.has_value());
    if (!created.has_value()) {
        NX_CHECK(created.error().kind == synchro::SyncErrorKind::Transport);
        NX_CHECK(created.error().transport == com::Error::InvalidArgument);
    }
}

void test_wait_timeout_rounding() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    auto config = make_consumer_config(unique_service("con-timeout"));
    config.initial_sync_timeout_ns = 2'500'000U; // 2.5 ms -> ceil to 3 ms

    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& consumer = **created;
    const auto timeout = consumer.next_wait_timeout();
    NX_CHECK(timeout.count() >= 1);
    NX_CHECK(timeout.count() <= 3);

    // A long window clamps to the 1 s ceiling instead of sleeping forever.
    MeterFixture long_meter{};
    auto long_config = make_consumer_config(unique_service("con-timeout-long"));
    long_config.initial_sync_timeout_ns = 60'000'000'000ULL;
    auto long_created =
        SyncConsumer<CaptureLogger>::create(*node, long_config, logger, *long_meter.meter);
    NX_CHECK(long_created.has_value());
    if (long_created.has_value()) {
        NX_CHECK_EQ((*long_created)->next_wait_timeout().count(), 1'000);
    }
}

void test_clock_fault_in_check_timeout() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    auto config = make_consumer_config(unique_service("con-clock"));
    config.initial_sync_timeout_ns = 60'000'000'000ULL;
    auto created = SyncConsumer<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    synchro::detail::SyncFaults::fail_clock = true;
    NX_CHECK(!(*created)->check_timeout());
    synchro::detail::SyncFaults::fail_clock = false;
    NX_CHECK(!(*created)->check_timeout());
}

} // namespace

void run_consumer_suite() {
    run_suite("consumer config", test_create_rejects_invalid_config);
    run_suite("consumer metrics failure", test_create_rejects_duplicate_metrics);
    run_suite("consumer instruments", test_consumer_instruments_are_registered);
    run_suite("consumer attachment", test_attachment_and_delegation);
    run_suite("consumer handlers", test_handler_registration_and_dispatch_order);
    run_suite("consumer continuity", test_continuity_and_generator_skips);
    run_suite("consumer transport gap", test_transport_gap_reconciliation);
    run_suite("consumer late drop", test_late_policy_drop);
    run_suite("consumer late deliver", test_late_policy_deliver_and_negative_latency);
    run_suite("consumer watchdog", test_watchdog_lock_loss_reacquire);
    run_suite("consumer lock gauge", test_lock_gauge_follows_state);
    run_suite("consumer loss counters", test_loss_counters);
    run_suite("consumer transport mapping", test_create_maps_service_name_failure);
    run_suite("consumer timeout rounding", test_wait_timeout_rounding);
    run_suite("consumer clock fault", test_clock_fault_in_check_timeout);
}
