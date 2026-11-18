// End-to-end integration, allocation, boundary, and observability tests
// (change add-synchro-event-sync; spec synchro/*; tasks 7.1-7.5).
#include "synchro_test_support.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/com/event_consumer_set.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/metric_record.hpp>
#include <nexigen/synchro/sync_consumer.hpp>
#include <nexigen/synchro/sync_consumer_state.hpp>
#include <nexigen/synchro/sync_context.hpp>
#include <nexigen/synchro/sync_generator.hpp>
#include <nexigen/synchro/sync_runner.hpp>

namespace {

using namespace nexigen;

/// @brief Fixed-buffer sink that keeps the last harvested batch.
struct RecordingSink {
    std::array<metrics::MetricRecord, 64> records{};
    std::uint32_t count{0};

    core::Status on_batch(const metrics::MetricBatchSnapshot& batch) noexcept {
        count = 0;
        if (batch.records == nullptr) {
            return {};
        }
        const std::uint32_t capacity = static_cast<std::uint32_t>(records.size());
        const std::uint32_t copy_count = std::min(batch.record_count, capacity);
        for (std::uint32_t index = 0U; index < copy_count; ++index) {
            records[index] = batch.records[index];
        }
        count = copy_count;
        return {};
    }

    core::Status flush() noexcept { return {}; }
};

const metrics::MetricRecord* find_record(const RecordingSink& sink, std::string_view name) {
    for (std::uint32_t index = 0U; index < sink.count; ++index) {
        if (metrics::detail::view_name(sink.records[index].identity.metric_name) == name) {
            return &sink.records[index];
        }
    }
    return nullptr;
}

void test_end_to_end_grid() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    const auto service = unique_service("int-grid");
    auto generator_config = make_generator_config(service, 100'000'000U, 10U);
    auto consumer_config = make_consumer_config(service, 100'000'000U, 10U);
    consumer_config.initial_sync_timeout_ns = 5'000'000'000ULL;
    consumer_config.sync_loss_timeout_ns = 5'000'000'000ULL;

    MeterFixture generator_meter{};
    MeterFixture consumer_meter{};

    auto generator = synchro::SyncGenerator<CaptureLogger>::create(
        *node, generator_config, logger, *generator_meter.meter);
    NX_CHECK(generator.has_value());
    auto runner = synchro::SyncRunner<CaptureLogger>::create(*node, consumer_config, logger,
                                                             *consumer_meter.meter);
    NX_CHECK(runner.has_value());
    if (!generator.has_value() || !runner.has_value()) {
        return;
    }

    std::atomic<std::uint64_t> count{0};
    std::atomic<std::uint64_t> last_absolute{0};
    std::atomic<bool> monotonic{true};
    std::atomic<bool> saw_late{false};
    std::atomic<std::int64_t> max_e2e{0};

    NX_CHECK((*runner)
                  ->consumer()
                  .on_any_phase([&](const synchro::SyncContext& context) {
                      const std::uint64_t absolute =
                          context.cycle_number * context.phases_total + context.phase_index;
                      const std::uint64_t previous_count =
                          count.load(std::memory_order_relaxed);
                      if (previous_count != 0U &&
                          absolute != last_absolute.load(std::memory_order_relaxed) + 1U) {
                          monotonic.store(false, std::memory_order_relaxed);
                      }
                      if (context.is_late) {
                          saw_late.store(true, std::memory_order_relaxed);
                      }
                      if (context.e2e_latency_ns >
                          max_e2e.load(std::memory_order_relaxed)) {
                          max_e2e.store(context.e2e_latency_ns, std::memory_order_relaxed);
                      }
                      last_absolute.store(absolute, std::memory_order_relaxed);
                      count.fetch_add(1U, std::memory_order_relaxed);
                  })
                  .has_value());

    NX_CHECK((*generator)->start().has_value());
    std::stop_source stop{};
    std::jthread thread{[&runner, token = stop.get_token()] { (*runner)->run(token); }};

    const bool reached = wait_for(
        [&count] { return count.load(std::memory_order_relaxed) >= 60U; },
        std::chrono::milliseconds{5'000});
    stop.request_stop();
    thread.join();
    (*generator)->stop();

    NX_CHECK(reached);
    NX_CHECK_EQ(count.load(std::memory_order_relaxed),
                (*runner)->stats().received);
    NX_CHECK(count.load(std::memory_order_relaxed) >= 60U);
    NX_CHECK(monotonic.load(std::memory_order_relaxed));
    NX_CHECK(!saw_late.load(std::memory_order_relaxed));

    const auto consumer_stats = (*runner)->stats();
    NX_CHECK_EQ(consumer_stats.transport_lost, 0U);
    NX_CHECK_EQ(consumer_stats.resyncs, 0U);
    NX_CHECK_EQ(consumer_stats.duplicates, 0U);
    NX_CHECK_EQ(consumer_stats.producer_restarts, 0U);

    const auto generator_stats = (*generator)->stats();
    NX_CHECK(generator_stats.phases_published >= 60U);
    NX_CHECK(generator_stats.cycles_completed >= 6U);
    // Every logical jump is a real generator overrun, never a false gap.
    NX_CHECK_EQ(consumer_stats.generator_skips, generator_stats.overrun_slots);
    NX_CHECK(generator_stats.overrun_slots <= 1U);
    NX_CHECK(generator_stats.max_jitter_ns < 5'000'000U);
    NX_CHECK(max_e2e.load(std::memory_order_relaxed) < 100'000'000);
}

void test_restart_resync() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    const auto service = unique_service("int-restart");
    auto generator_config = make_generator_config(service, 50'000'000U, 5U);
    auto consumer_config = make_consumer_config(service, 50'000'000U, 5U);
    consumer_config.initial_sync_timeout_ns = 5'000'000'000ULL;
    consumer_config.sync_loss_timeout_ns = 5'000'000'000ULL;

    MeterFixture generator_meter{};
    MeterFixture consumer_meter{};

    auto generator = synchro::SyncGenerator<CaptureLogger>::create(
        *node, generator_config, logger, *generator_meter.meter);
    NX_CHECK(generator.has_value());
    auto runner = synchro::SyncRunner<CaptureLogger>::create(*node, consumer_config, logger,
                                                             *consumer_meter.meter);
    NX_CHECK(runner.has_value());
    if (!generator.has_value() || !runner.has_value()) {
        return;
    }

    std::atomic<std::uint64_t> count{0};
    std::atomic<std::uint64_t> epoch_changes{0};
    std::atomic<std::uint64_t> last_epoch{0};
    std::atomic<std::uint64_t> first_sequence_of_new_epoch{1U};
    std::atomic<bool> saw_cycle_zero{false};

    NX_CHECK((*runner)
                  ->consumer()
                  .on_any_phase([&](const synchro::SyncContext& context) {
                      const std::uint64_t previous = count.load(std::memory_order_relaxed);
                      if (previous != 0U &&
                          context.producer_epoch !=
                              last_epoch.load(std::memory_order_relaxed)) {
                          epoch_changes.fetch_add(1U, std::memory_order_relaxed);
                          first_sequence_of_new_epoch.store(context.sequence_id,
                                                            std::memory_order_relaxed);
                          saw_cycle_zero.store(context.cycle_number == 0U &&
                                                   context.phase_index == 0U,
                                               std::memory_order_relaxed);
                      }
                      last_epoch.store(context.producer_epoch, std::memory_order_relaxed);
                      count.fetch_add(1U, std::memory_order_relaxed);
                  })
                  .has_value());

    NX_CHECK((*generator)->start().has_value());
    std::stop_source stop{};
    std::jthread thread{[&runner, token = stop.get_token()] { (*runner)->run(token); }};

    NX_CHECK(wait_for([&count] { return count.load(std::memory_order_relaxed) >= 5U; },
                      std::chrono::milliseconds{3'000}));
    (*generator)->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    NX_CHECK((*generator)->start().has_value());
    NX_CHECK(wait_for(
        [&epoch_changes] { return epoch_changes.load(std::memory_order_relaxed) >= 1U; },
        std::chrono::milliseconds{3'000}));

    stop.request_stop();
    thread.join();
    (*generator)->stop();

    NX_CHECK_EQ(epoch_changes.load(std::memory_order_relaxed), 1U);
    NX_CHECK_EQ(first_sequence_of_new_epoch.load(std::memory_order_relaxed), 0U);
    NX_CHECK(saw_cycle_zero.load(std::memory_order_relaxed));

    const auto stats = (*runner)->stats();
    NX_CHECK_EQ(stats.producer_restarts, 1U);
    NX_CHECK(stats.resyncs >= 1U);
    NX_CHECK_EQ(stats.generator_skips, 0U);
    NX_CHECK(stats.received >= 5U);
}

void test_allocation_free_steady_state() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    const auto service = unique_service("int-alloc");
    auto generator_config = make_generator_config(service, 20'000'000U, 4U);
    auto consumer_config = make_consumer_config(service, 20'000'000U, 4U);

    MeterFixture generator_meter{};
    MeterFixture consumer_meter{};
    auto generator = synchro::SyncGenerator<CaptureLogger>::create(
        *node, generator_config, logger, *generator_meter.meter);
    NX_CHECK(generator.has_value());
    auto consumer = synchro::SyncConsumer<CaptureLogger>::create(*node, consumer_config, logger,
                                                                 *consumer_meter.meter);
    NX_CHECK(consumer.has_value());
    if (!generator.has_value() || !consumer.has_value()) {
        return;
    }

    std::atomic<std::uint64_t> delivered{0};
    NX_CHECK((*consumer)
                  ->on_any_phase([&delivered](const synchro::SyncContext&) {
                      delivered.fetch_add(1U, std::memory_order_relaxed);
                  })
                  .has_value());

    NX_CHECK((*generator)->start().has_value());
    const bool locked = wait_for(
        [&consumer] {
            static_cast<void>((*consumer)->drain());
            return (*consumer)->state() == synchro::SyncConsumerState::Locked;
        },
        std::chrono::milliseconds{3'000});
    NX_CHECK(locked);

    const std::uint64_t before = delivered.load(std::memory_order_relaxed);
    allocation_counter_arm();
    for (int iteration = 0; iteration < 2'000; ++iteration) {
        static_cast<void>((*consumer)->drain());
        if (delivered.load(std::memory_order_relaxed) >= before + 20U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds{500});
    }
    const std::uint64_t allocations = allocation_counter_disarm();
    (*generator)->stop();

    NX_CHECK(delivered.load(std::memory_order_relaxed) >= before + 20U);
    NX_CHECK_EQ(allocations, 0U);
}

void test_sixteen_consumer_boundary() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }

    std::array<CollectedEvents, 16> states{};
    std::vector<RawSyncConsumer> consumers{};
    consumers.reserve(16);
    for (std::size_t index = 0U; index < 16U; ++index) {
        const auto service = unique_service("int-bound");
        auto channel = com::EventChannel<synchro::SyncPayload>::create(
            *node, *com::ServiceName::create(service.c_str()), com::ChannelConfig{});
        NX_CHECK(channel.has_value());
        if (!channel.has_value()) {
            return;
        }
        auto endpoint = channel->consumer(CollectingEventCallback{&states[index]},
                                          CollectingLossCallback{&states[index]});
        NX_CHECK(endpoint.has_value());
        if (!endpoint.has_value()) {
            return;
        }
        consumers.push_back(std::move(*endpoint));
    }
    NX_CHECK_EQ(consumers.size(), 16U);

    com::EventConsumerSet<16> set{*node};
    NX_CHECK(set.status().has_value());
    NX_CHECK_EQ(set.capacity(), 16U);
    for (auto& consumer : consumers) {
        NX_CHECK(set.attach(consumer).has_value());
    }
    NX_CHECK_EQ(set.size(), 16U);

    const auto extra_service = unique_service("int-bound-extra");
    auto extra_channel = com::EventChannel<synchro::SyncPayload>::create(
        *node, *com::ServiceName::create(extra_service.c_str()), com::ChannelConfig{});
    NX_CHECK(extra_channel.has_value());
    if (!extra_channel.has_value()) {
        return;
    }
    CollectedEvents extra_state{};
    auto extra = extra_channel->consumer(CollectingEventCallback{&extra_state},
                                         CollectingLossCallback{&extra_state});
    NX_CHECK(extra.has_value());
    if (!extra.has_value()) {
        return;
    }
    const auto rejected = set.attach(*extra);
    NX_CHECK(!rejected.has_value());
    if (!rejected.has_value()) {
        NX_CHECK(rejected.error() == com::Error::OutOfSamples);
    }

    for (auto& consumer : consumers) {
        NX_CHECK(set.detach(consumer).has_value());
    }
    NX_CHECK_EQ(set.size(), 0U);
}

void test_multiple_channels_use_separate_runners() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    const auto service_a = unique_service("int-multi-a");
    const auto service_b = unique_service("int-multi-b");
    auto config_a = make_consumer_config(service_a);
    auto config_b = make_consumer_config(service_b);
    config_a.initial_sync_timeout_ns = 60'000'000'000ULL;
    config_b.initial_sync_timeout_ns = 60'000'000'000ULL;

    auto channel_a = com::EventChannel<synchro::SyncPayload>::create(
        *node, *com::ServiceName::create(service_a.c_str()), config_a.channel.channel);
    auto channel_b = com::EventChannel<synchro::SyncPayload>::create(
        *node, *com::ServiceName::create(service_b.c_str()), config_b.channel.channel);
    NX_CHECK(channel_a.has_value());
    NX_CHECK(channel_b.has_value());
    if (!channel_a.has_value() || !channel_b.has_value()) {
        return;
    }
    auto producer_a = channel_a->producer();
    auto producer_b = channel_b->producer();
    NX_CHECK(producer_a.has_value());
    NX_CHECK(producer_b.has_value());
    if (!producer_a.has_value() || !producer_b.has_value()) {
        return;
    }

    MeterFixture meter_a{};
    MeterFixture meter_b{};
    auto runner_a = synchro::SyncRunner<CaptureLogger>::create(*node, config_a, logger, *meter_a.meter);
    auto runner_b = synchro::SyncRunner<CaptureLogger>::create(*node, config_b, logger, *meter_b.meter);
    NX_CHECK(runner_a.has_value());
    NX_CHECK(runner_b.has_value());
    if (!runner_a.has_value() || !runner_b.has_value()) {
        return;
    }

    std::atomic<std::uint32_t> received_a{0};
    std::atomic<std::uint32_t> received_b{0};
    NX_CHECK((*runner_a)
                  ->consumer()
                  .on_any_phase([&received_a](const synchro::SyncContext&) {
                      received_a.fetch_add(1U, std::memory_order_relaxed);
                  })
                  .has_value());
    NX_CHECK((*runner_b)
                  ->consumer()
                  .on_any_phase([&received_b](const synchro::SyncContext&) {
                      received_b.fetch_add(1U, std::memory_order_relaxed);
                  })
                  .has_value());

    std::stop_source stop{};
    std::jthread thread_a{[&runner_a, token = stop.get_token()] { (*runner_a)->run(token); }};
    std::jthread thread_b{[&runner_b, token = stop.get_token()] { (*runner_b)->run(token); }};
    std::this_thread::sleep_for(std::chrono::milliseconds{20});

    const auto now = chrono::Timestamp::now();
    const std::uint64_t header = now.has_value() ? now->nanoseconds() : 0U;
    synchro::SyncPayload payload_a{};
    payload_a.phases_total = 10U;
    payload_a.cycle_period_ns = 10'000'000U;
    payload_a.scheduled_time_ns = header;
    auto payload_b = payload_a;

    NX_CHECK(producer_a
                 ->publish(payload_a, 0x53594E43U,
                           com::EventTime{header, com::TimestampDomain::MonotonicRaw})
                 .has_value());
    NX_CHECK(producer_b
                 ->publish(payload_b, 0x53594E43U,
                           com::EventTime{header, com::TimestampDomain::MonotonicRaw})
                 .has_value());

    NX_CHECK(wait_for(
        [&received_a, &received_b] {
            return received_a.load(std::memory_order_relaxed) >= 1U &&
                   received_b.load(std::memory_order_relaxed) >= 1U;
        },
        std::chrono::milliseconds{2'000}));
    stop.request_stop();
    thread_a.join();
    thread_b.join();

    NX_CHECK_EQ(received_a.load(std::memory_order_relaxed), 1U);
    NX_CHECK_EQ(received_b.load(std::memory_order_relaxed), 1U);
    NX_CHECK_EQ((*runner_a)->stats().received, 1U);
    NX_CHECK_EQ((*runner_b)->stats().received, 1U);
}

void test_logging_and_metrics_integration() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    const auto service = unique_service("int-observability");
    auto generator_config = make_generator_config(service, 20'000'000U, 4U);

    RecordingSink sink{};
    metrics::MetricProvider provider{metrics::MetricProviderConfig{}, sink};
    auto meter = provider.get_meter("synchro_integration", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter.has_value()) {
        return;
    }

    auto generator =
        synchro::SyncGenerator<CaptureLogger>::create(*node, generator_config, logger, **meter);
    NX_CHECK(generator.has_value());
    if (!generator.has_value()) {
        return;
    }

    std::atomic<bool> collecting{true};
    std::atomic<std::uint32_t> collect_failures{0};
    std::thread collector{[&provider, &collecting, &collect_failures] {
        while (collecting.load(std::memory_order_acquire)) {
            if (!provider.collect_now().has_value()) {
                collect_failures.fetch_add(1U, std::memory_order_relaxed);
            }
            std::this_thread::sleep_for(std::chrono::microseconds{500});
        }
    }};

    NX_CHECK((*generator)->start().has_value());
    NX_CHECK(wait_for([&generator] { return (*generator)->state() == synchro::SyncState::Running; },
                      std::chrono::milliseconds{2'000}));
    NX_CHECK(wait_for([&generator] { return (*generator)->stats().phases_published >= 4U; },
                      std::chrono::milliseconds{2'000}));
    (*generator)->stop();
    collecting.store(false, std::memory_order_release);
    collector.join();

    NX_CHECK_EQ(collect_failures.load(std::memory_order_relaxed), 0U);
    NX_CHECK(provider.collect_now().has_value());

    const metrics::MetricRecord* cycles = find_record(sink, "sync_cycles_total");
    const metrics::MetricRecord* phases = find_record(sink, "sync_phases_total");
    const metrics::MetricRecord* overruns = find_record(sink, "sync_overruns_total");
    const metrics::MetricRecord* jitter = find_record(sink, "sync_jitter_ns");
    const metrics::MetricRecord* state = find_record(sink, "sync_state");
    NX_CHECK(cycles != nullptr);
    NX_CHECK(phases != nullptr);
    NX_CHECK(overruns != nullptr);
    NX_CHECK(jitter != nullptr);
    NX_CHECK(state != nullptr);
    if (cycles != nullptr) {
        NX_CHECK(cycles->kind == metrics::MetricKind::Counter);
        NX_CHECK(cycles->concurrency == metrics::ConcurrencyPolicy::MultiThreaded);
    }
    if (phases != nullptr) {
        NX_CHECK(phases->scalar.counter >= 4U);
    }
    if (jitter != nullptr) {
        NX_CHECK(jitter->kind == metrics::MetricKind::ExplicitHistogram);
        NX_CHECK(jitter->concurrency == metrics::ConcurrencyPolicy::MultiThreaded);
    }

    NX_CHECK(captured_contains("sync generator starting"));
    NX_CHECK(captured_contains("sync generator running"));
    NX_CHECK(captured_contains("sync generator stopped"));
}

} // namespace

void run_integration_suite() {
    run_suite("integration grid", test_end_to_end_grid);
    run_suite("integration restart", test_restart_resync);
    run_suite("integration allocations", test_allocation_free_steady_state);
    run_suite("integration 16 consumers", test_sixteen_consumer_boundary);
    run_suite("integration multi-channel", test_multiple_channels_use_separate_runners);
    run_suite("integration observability", test_logging_and_metrics_integration);
}
