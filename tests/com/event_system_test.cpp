// Tests nexigen::com event system — only built with NEXIGEN_ENABLE_COM=ON.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <format>
#include <functional>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/com/channel_config.hpp>
#include <nexigen/com/concepts/event_payload_concept.hpp>
#include <nexigen/com/detail/event_sequence_tracker.hpp>
#include <nexigen/com/empty_payload.hpp>
#include <nexigen/com/event_channel.hpp>
#include <nexigen/com/event_consumer.hpp>
#include <nexigen/com/event_consumer_set.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/com/event_options.hpp>
#include <nexigen/com/event_producer.hpp>
#include <nexigen/com/event_time.hpp>
#include <nexigen/com/hardware_metadata.hpp>
#include <nexigen/com/hardware_event.hpp>
#include <nexigen/com/loss_info.hpp>
#include <nexigen/com/loss_reason.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publish_outcome.hpp>
#include <nexigen/com/publish_subscribe_options.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/timestamp_domain.hpp>
#include <nexigen/com/wait_outcome.hpp>

namespace com = nexigen::com;

std::atomic<bool> g_count_allocations{false};
std::atomic<std::size_t> g_allocation_count{0};

void* operator new(std::size_t size) {
    if (g_count_allocations.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1U, std::memory_order_relaxed);
    }
    void* pointer = std::malloc(size == 0U ? 1U : size);
    if (pointer == nullptr) {
        std::abort();
    }
    return pointer;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}
void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

namespace {

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

[[nodiscard]] std::string unique_name(std::string_view suffix) {
    static std::atomic<std::uint64_t> counter{0};
    return std::format("Nexigen/EventTest/{}/{}", suffix,
                       counter.fetch_add(1U, std::memory_order_relaxed));
}

[[nodiscard]] com::Result<com::ServiceName> make_name(std::string_view suffix) {
    return com::ServiceName::create(unique_name(suffix).c_str());
}

[[nodiscard]] std::uint64_t thread_cpu_time_ns() noexcept {
    timespec ts{};
    if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return 0U;
    }
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

struct Pod {
    std::uint64_t value{0};
    std::uint32_t code{0};
};

struct Other {
    std::uint64_t value{0};
};

static_assert(com::concepts::EventPayload<Pod>);
static_assert(com::concepts::EventPayload<com::EmptyPayload>);
static_assert(!com::concepts::EventPayload<std::string>);
static_assert(!com::concepts::EventPayload<std::vector<int>>);
static_assert(!com::concepts::EventPayload<int&>);
static_assert(!com::concepts::EventPayload<void>);
static_assert(sizeof(com::EventHeader) == 72U);
static_assert(alignof(com::EventHeader) == 8U);

void test_value_serialization() {
    com::PublishSubscribeOptions ps{};
    ps.max_subscribers = 16;
    ps.max_publishers = 1;
    ps.max_nodes = 17;
    ps.subscriber_max_buffer_size = 512;
    ps.subscriber_buffer_size = 512;
    ps.history_size = 16;
    ps.enable_safe_overflow = true;
    const nlohmann::json ps_json = ps;
    com::PublishSubscribeOptions ps_back{};
    com::from_json(ps_json, ps_back);
    NX_CHECK(ps_back.max_subscribers == 16U);
    NX_CHECK(ps_back.max_publishers == 1U);
    NX_CHECK(ps_back.max_nodes == 17U);
    NX_CHECK(ps_back.subscriber_max_buffer_size == 512U);
    NX_CHECK(ps_back.subscriber_buffer_size == 512U);
    NX_CHECK(ps_back.history_size == 16U);
    NX_CHECK(ps_back.enable_safe_overflow);
    NX_CHECK(std::format("{}", ps) == ps.to_string());

    com::EventOptions eo{};
    eo.max_listeners = 16;
    eo.max_notifiers = 1;
    eo.max_nodes = 17;
    const nlohmann::json eo_json = eo;
    com::EventOptions eo_back{};
    com::from_json(eo_json, eo_back);
    NX_CHECK(eo_back.max_listeners == 16U);
    NX_CHECK(eo_back.max_notifiers == 1U);
    NX_CHECK(eo_back.max_nodes == 17U);
    NX_CHECK(std::format("{}", eo) == eo.to_string());

    com::PublishSubscribeOptions ps_untouched = ps;
    com::from_json(nlohmann::json{42}, ps_untouched);
    NX_CHECK(ps_untouched.to_string() == ps.to_string());
    com::from_json(nlohmann::json{{"max_subscribers", "many"}, {"max_publishers", 1.5}},
                   ps_untouched);
    NX_CHECK(ps_untouched.to_string() == ps.to_string());

    com::EventOptions eo_untouched = eo;
    com::from_json(nlohmann::json::array(), eo_untouched);
    NX_CHECK(eo_untouched.to_string() == eo.to_string());
    com::from_json(nlohmann::json{{"max_listeners", false}, {"max_notifiers", -1}}, eo_untouched);
    NX_CHECK(eo_untouched.to_string() == eo.to_string());

    com::ChannelConfig config{};
    NX_CHECK(config.validate().has_value());
    NX_CHECK(config.max_consumers == 16U);
    NX_CHECK(config.queue_size == 512U);
    NX_CHECK(config.history_size == 16U);
    const auto ps_options = config.publish_subscribe_options();
    NX_CHECK(ps_options.max_subscribers == 16U);
    NX_CHECK(ps_options.max_publishers == 1U);
    NX_CHECK(ps_options.max_nodes == 17U);
    NX_CHECK(ps_options.enable_safe_overflow);
    const auto event_options = config.event_options();
    NX_CHECK(event_options.max_listeners == 16U);
    NX_CHECK(event_options.max_notifiers == 1U);
    NX_CHECK(event_options.max_nodes == 17U);
    const nlohmann::json config_json = config;
    com::ChannelConfig config_back{};
    com::from_json(config_json, config_back);
    NX_CHECK(config_back.max_consumers == 16U);
    NX_CHECK(config_back.default_timestamp_domain == com::TimestampDomain::MonotonicRaw);
    NX_CHECK(std::format("{}", config) == config.to_string());

    com::EventTime time{123U, com::TimestampDomain::Hardware};
    const nlohmann::json time_json = time;
    com::EventTime time_back{};
    com::from_json(time_json, time_back);
    NX_CHECK(time_back.ns == 123U);
    NX_CHECK(time_back.domain == com::TimestampDomain::Hardware);
    NX_CHECK(std::format("{}", time) == time.to_string());

    com::HardwareMetadata hw{};
    hw.sequence = 7U;
    hw.native_ticks = 900U;
    hw.event_code = 42U;
    hw.has_sequence = true;
    hw.has_ticks = true;
    hw.has_event_code = true;
    const nlohmann::json hw_json = hw;
    com::HardwareMetadata hw_back{};
    com::from_json(hw_json, hw_back);
    NX_CHECK(hw_back.sequence == 7U);
    NX_CHECK(hw_back.native_ticks == 900U);
    NX_CHECK(hw_back.event_code == 42U);
    NX_CHECK(hw_back.has_sequence && hw_back.has_ticks && hw_back.has_event_code);
    NX_CHECK(std::format("{}", hw) == hw.to_string());

    com::LossInfo loss{};
    loss.reason = com::LossReason::TransportGap;
    loss.expected_sequence = 4U;
    loss.actual_sequence = 9U;
    loss.lost_count = 5U;
    loss.actual_timestamp_domain = com::TimestampDomain::Hardware;
    const nlohmann::json loss_json = loss;
    com::LossInfo loss_back{};
    com::from_json(loss_json, loss_back);
    NX_CHECK(loss_back.reason == com::LossReason::TransportGap);
    NX_CHECK(loss_back.lost_count == 5U);
    NX_CHECK(loss_back.actual_timestamp_domain == com::TimestampDomain::Hardware);
    NX_CHECK(std::format("{}", loss) == loss.to_string());

    com::EventConsumerStats cs{};
    cs.received = 3U;
    cs.transport_lost = 2U;
    cs.restarts = 1U;
    cs.has_received = true;
    const nlohmann::json cs_json = cs;
    com::EventConsumerStats cs_back{};
    com::from_json(cs_json, cs_back);
    NX_CHECK(cs_back.received == 3U);
    NX_CHECK(cs_back.transport_lost == 2U);
    NX_CHECK(cs_back.has_received);
    NX_CHECK(std::format("{}", cs) == cs.to_string());

    com::EventProducerStats ps_stats{};
    ps_stats.published = 4U;
    ps_stats.send_failures = 1U;
    ps_stats.notify_failures = 2U;
    ps_stats.last_notify_error = com::Error::ConnectionFailure;
    ps_stats.current_sequence = 4U;
    ps_stats.current_epoch = 99U;
    const nlohmann::json producer_json = ps_stats;
    com::EventProducerStats producer_back{};
    com::from_json(producer_json, producer_back);
    NX_CHECK(producer_back.published == 4U);
    NX_CHECK(producer_back.notify_failures == 2U);
    NX_CHECK(producer_back.last_notify_error == com::Error::ConnectionFailure);
    NX_CHECK(std::format("{}", ps_stats) == ps_stats.to_string());

    NX_CHECK(com::EmptyPayload{}.to_string() == "EmptyPayload()");
    NX_CHECK(std::format("{}", com::EmptyPayload{}) == "EmptyPayload()");

    com::EventHeader header{};
    header.sequence_id = 11U;
    header.timestamp_ns = 22U;
    header.producer_epoch = 33U;
    header.ingest_timestamp_ns = 44U;
    header.hardware_sequence = 55U;
    header.native_ticks = 66U;
    header.domain_id = 7U;
    header.domain_epoch = 9U;
    header.event_code = 77U;
    header.hardware_event_code = 88U;
    std::memcpy(header.app_id, "LIDR", 4U);
    header.flags = com::EventHeader::has_canonical_projection_bit |
                   com::EventHeader::has_payload_bit | com::EventHeader::has_hardware_sequence_bit |
                   com::EventHeader::has_hardware_timestamp_bit |
                   com::EventHeader::has_hardware_event_code_bit;
    const nlohmann::json header_json = header;
    com::EventHeader header_back{};
    com::from_json(header_json, header_back);
    NX_CHECK(header_back.sequence_id == 11U);
    NX_CHECK(header_back.domain_id == 7U);
    NX_CHECK(header_back.domain_epoch == 9U);
    NX_CHECK(header_back.native_ticks == 66U);
    NX_CHECK(header_back.has_payload());
    NX_CHECK(header_back.has_hardware_sequence());
    NX_CHECK(header_back.has_hardware_timestamp());
    NX_CHECK(header_back.has_hardware_event_code());
    NX_CHECK(header_back.app_id_text() == "LIDR");
    NX_CHECK(std::format("{}", header) == header.to_string());

    NX_CHECK(com::to_string(com::TimestampDomain::Hardware) == "Hardware");
    NX_CHECK(com::to_string(com::LossReason::UnexpectedSequenceReset) == "UnexpectedSequenceReset");
    NX_CHECK(com::to_string(com::PublishOutcome::NotifyFailed) == "NotifyFailed");
    NX_CHECK(com::to_string(com::WaitOutcome::WokenUp) == "WokenUp");
    com::PublishOutcome po{};
    NX_CHECK(com::from_string("Notified", po) && po == com::PublishOutcome::Notified);
    com::WaitOutcome wo{};
    NX_CHECK(com::from_string("Timeout", wo) && wo == com::WaitOutcome::Timeout);
    NX_CHECK(std::format("{}", com::LossReason::Duplicate) == "Duplicate");
}

template <class E, std::size_t N> void check_enum_roundtrip(const std::array<E, N>& values) {
    for (const E value : values) {
        const std::string name{com::to_string(value)};
        E parsed{};
        NX_CHECK(com::from_string(name, parsed));
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == name);

        const nlohmann::json encoded = value;
        NX_CHECK(encoded.is_string());
        if (encoded.is_string()) {
            NX_CHECK(encoded.get<std::string>() == name);
        }
        E decoded{};
        com::from_json(encoded, decoded);
        NX_CHECK(decoded == value);

        E untouched = value;
        com::from_json(nlohmann::json{42}, untouched);
        NX_CHECK(untouched == value);
        com::from_json(nlohmann::json{"Nope"}, untouched);
        NX_CHECK(untouched == value);
    }
}

void test_enum_roundtrip_table() {
    check_enum_roundtrip<com::TimestampDomain>(
        std::array{com::TimestampDomain::Unknown, com::TimestampDomain::MonotonicRaw,
                   com::TimestampDomain::Monotonic, com::TimestampDomain::Realtime,
                   com::TimestampDomain::Hardware});
    check_enum_roundtrip<com::LossReason>(
        std::array{com::LossReason::TransportGap, com::LossReason::ProducerRestart,
                   com::LossReason::Duplicate, com::LossReason::UnexpectedSequenceReset});
    check_enum_roundtrip<com::PublishOutcome>(
        std::array{com::PublishOutcome::Notified, com::PublishOutcome::NotifyFailed});
    check_enum_roundtrip<com::WaitOutcome>(
        std::array{com::WaitOutcome::Events, com::WaitOutcome::Timeout, com::WaitOutcome::WokenUp});
}

void test_sequence_tracker() {
    com::detail::EventSequenceTracker tracker{};
    com::EventHeader header{};
    header.producer_epoch = 1U;

    header.sequence_id = 5U;
    auto observation = tracker.observe(header);
    NX_CHECK(!observation.has_loss);
    NX_CHECK(observation.deliver);
    NX_CHECK(tracker.stats().first_sequence == 5U);
    NX_CHECK(tracker.stats().history_skipped == 5U);

    header.sequence_id = 6U;
    observation = tracker.observe(header);
    NX_CHECK(!observation.has_loss);
    NX_CHECK(observation.deliver);

    header.sequence_id = 9U;
    observation = tracker.observe(header);
    NX_CHECK(observation.has_loss);
    NX_CHECK(observation.reason == com::LossReason::TransportGap);
    NX_CHECK(observation.lost_count == 2U);
    NX_CHECK(tracker.stats().transport_lost == 2U);

    header.sequence_id = 7U;
    observation = tracker.observe(header);
    NX_CHECK(observation.has_loss);
    NX_CHECK(observation.reason == com::LossReason::Duplicate);
    NX_CHECK(!observation.deliver);
    NX_CHECK(tracker.stats().duplicates == 1U);

    header.sequence_id = 0U;
    header.producer_epoch = 2U;
    observation = tracker.observe(header);
    NX_CHECK(observation.has_loss);
    NX_CHECK(observation.reason == com::LossReason::ProducerRestart);
    NX_CHECK(observation.old_epoch == 1U);
    NX_CHECK(observation.new_epoch == 2U);
    NX_CHECK(observation.deliver);
    NX_CHECK(tracker.stats().restarts == 1U);

    com::detail::EventSequenceTracker reset_tracker{};
    com::EventHeader reset{};
    reset.producer_epoch = 7U;
    reset.sequence_id = 5U;
    static_cast<void>(reset_tracker.observe(reset));
    reset.sequence_id = 0U;
    observation = reset_tracker.observe(reset);
    NX_CHECK(observation.has_loss);
    NX_CHECK(observation.reason == com::LossReason::UnexpectedSequenceReset);
    NX_CHECK(observation.deliver);
    NX_CHECK(reset_tracker.stats().unexpected_resets == 1U);
}

void test_channel_roundtrip() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("roundtrip");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }

    com::ChannelConfig config{};
    config.max_consumers = 4U;
    config.queue_size = 8U;
    config.history_size = 4U;
    auto channel_result = com::EventChannel<Pod>::create(*node, *name, config);
    NX_CHECK(channel_result.has_value());
    if (!channel_result.has_value()) {
        return;
    }
    auto& channel = *channel_result;

    std::vector<com::EventHeader> received;
    std::vector<com::LossInfo> losses;
    auto consumer = channel.consumer(
        [&received](const com::EventHeader& header, const Pod& payload) {
            (void)payload;
            received.push_back(header);
        },
        [&losses](const com::LossInfo& info) { losses.push_back(info); });
    NX_CHECK(consumer.has_value());
    if (!consumer.has_value()) {
        return;
    }
    auto producer = channel.producer(std::string_view{"APP1"});
    NX_CHECK(producer.has_value());
    if (!producer.has_value()) {
        return;
    }

    Pod first{10U, 1U};
    auto outcome = producer->publish_now(first, 0x1234U);
    NX_CHECK(outcome.has_value());
    NX_CHECK(*outcome == com::PublishOutcome::Notified);
    Pod second{20U, 2U};
    outcome = producer->publish_now(second, 0x1235U);
    NX_CHECK(outcome.has_value());
    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(received.size() == 2U);
    if (received.size() == 2U) {
        NX_CHECK(received[0].sequence_id == 0U);
        NX_CHECK(received[0].event_code == 0x1234U);
        NX_CHECK(received[0].has_payload());
        NX_CHECK(received[0].app_id_text() == "APP1");
        NX_CHECK(received[0].domain_id == 0U);
        NX_CHECK(received[0].has_canonical_projection());
        NX_CHECK(received[0].ingest_timestamp_ns > 0U);
        NX_CHECK(received[0].timestamp_ns == received[0].ingest_timestamp_ns);
        NX_CHECK(!received[0].has_hardware_sequence());
        NX_CHECK(!received[0].has_hardware_timestamp());
        NX_CHECK(!received[0].has_hardware_event_code());
        NX_CHECK(received[1].sequence_id == 1U);
    }
    NX_CHECK(losses.empty());
    NX_CHECK(consumer->stats().received == 2U);

    producer->on_source_reset();
    Pod third{30U, 3U};
    outcome = producer->publish_now(third, 0x1236U);
    NX_CHECK(outcome.has_value());
    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(received.size() == 3U);
    if (received.size() == 3U) {
        NX_CHECK(received[2].sequence_id == 0U);
    }
    NX_CHECK(losses.size() == 1U);
    if (losses.size() == 1U) {
        NX_CHECK(losses[0].reason == com::LossReason::ProducerRestart);
        NX_CHECK(losses[0].new_epoch != losses[0].old_epoch);
    }
    NX_CHECK(std::format("{}", channel) == channel.to_string());
    NX_CHECK(std::format("{}", *producer) == producer->to_string());
    NX_CHECK(std::format("{}", *consumer) == consumer->to_string());
}

void test_empty_payload_and_hardware() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("hardware");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    com::ChannelConfig config{};
    config.max_consumers = 2U;
    config.queue_size = 4U;
    config.history_size = 2U;
    auto channel_result = com::EventChannel<com::EmptyPayload>::create(*node, *name, config);
    NX_CHECK(channel_result.has_value());
    if (!channel_result.has_value()) {
        return;
    }
    auto& channel = *channel_result;

    std::vector<com::EventHeader> received;
    auto consumer =
        channel.consumer([&received](const com::EventHeader& header,
                                     const com::EmptyPayload&) { received.push_back(header); },
                         [](const com::LossInfo&) {});
    NX_CHECK(consumer.has_value());
    if (!consumer.has_value()) {
        return;
    }
    auto producer = channel.producer();
    NX_CHECK(producer.has_value());
    if (!producer.has_value()) {
        return;
    }

    com::HardwareMetadata hardware{};
    hardware.sequence = 77U;
    hardware.native_ticks = 123456U;
    hardware.canonical_ns = 123456U;
    hardware.has_canonical = true;
    hardware.domain_id = 7U;
    hardware.domain_epoch = 5U;
    hardware.event_code = 0xABCU;
    hardware.has_sequence = true;
    hardware.has_ticks = true;
    hardware.has_event_code = true;
    const auto outcome =
        producer->publish(com::EmptyPayload{}, 0x55U,
                          com::EventTime{123456U, com::TimestampDomain::Hardware}, hardware);
    NX_CHECK(outcome.has_value());
    NX_CHECK(*outcome == com::PublishOutcome::Notified);
    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(received.size() == 1U);
    if (received.size() == 1U) {
        const auto& header = received[0];
        NX_CHECK(!header.has_payload());
        NX_CHECK(header.has_hardware_sequence());
        NX_CHECK(header.has_hardware_timestamp());
        NX_CHECK(header.has_hardware_event_code());
        NX_CHECK(header.hardware_sequence == 77U);
        NX_CHECK(header.native_ticks == 123456U);
        NX_CHECK(header.hardware_event_code == 0xABCU);
        NX_CHECK(header.domain_id == hardware.domain_id);
        NX_CHECK(header.has_canonical_projection());
        NX_CHECK(header.timestamp_ns == 123456U);
        NX_CHECK(header.ingest_timestamp_ns > 0U);
    }
}

void test_gap_from_slow_consumer() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("gap");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    com::ChannelConfig config{};
    config.max_consumers = 2U;
    config.queue_size = 2U;
    config.history_size = 0U;
    auto channel_result = com::EventChannel<Pod>::create(*node, *name, config);
    NX_CHECK(channel_result.has_value());
    if (!channel_result.has_value()) {
        return;
    }
    auto& channel = *channel_result;

    std::vector<com::LossInfo> losses;
    std::size_t received = 0;
    auto consumer =
        channel.consumer([&received](const com::EventHeader&, const Pod&) { ++received; },
                         [&losses](const com::LossInfo& info) { losses.push_back(info); });
    NX_CHECK(consumer.has_value());
    if (!consumer.has_value()) {
        return;
    }
    auto producer = channel.producer();
    NX_CHECK(producer.has_value());
    if (!producer.has_value()) {
        return;
    }

    NX_CHECK(producer->publish_now(Pod{0U, 0U}, 1U).has_value());
    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(received == 1U);

    for (std::uint64_t i = 1U; i <= 4U; ++i) {
        NX_CHECK(producer->publish_now(Pod{i, 0U}, 1U).has_value());
    }
    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(losses.size() == 1U);
    if (losses.size() == 1U) {
        NX_CHECK(losses[0].reason == com::LossReason::TransportGap);
        NX_CHECK(losses[0].lost_count == 2U);
    }
    NX_CHECK(consumer->stats().transport_lost == 2U);
    NX_CHECK(received == 3U);
}

struct ChannelPair {
    com::EventChannel<Pod> data;
    com::EventChannel<Other> other;
};

[[nodiscard]] std::optional<ChannelPair> make_channel_pair(com::Node& node) {
    auto name_a = make_name("set-a");
    auto name_b = make_name("set-b");
    if (!name_a.has_value() || !name_b.has_value()) {
        return std::nullopt;
    }
    com::ChannelConfig config{};
    config.max_consumers = 2U;
    config.queue_size = 4U;
    config.history_size = 1U;
    auto data = com::EventChannel<Pod>::create(node, *name_a, config);
    auto other = com::EventChannel<Other>::create(node, *name_b, config);
    if (!data.has_value() || !other.has_value()) {
        return std::nullopt;
    }
    return ChannelPair{std::move(*data), std::move(*other)};
}

void test_consumer_set() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto channels = make_channel_pair(*node);
    NX_CHECK(channels.has_value());
    if (!channels.has_value()) {
        return;
    }
    auto& channel_a = channels->data;
    auto& channel_b = channels->other;

    std::size_t received_a = 0;
    std::size_t received_b = 0;
    auto consumer_a =
        channel_a.consumer([&received_a](const com::EventHeader&, const Pod&) { ++received_a; },
                           [](const com::LossInfo&) {});
    auto consumer_b =
        channel_b.consumer([&received_b](const com::EventHeader&, const Other&) { ++received_b; },
                           [](const com::LossInfo&) {});
    NX_CHECK(consumer_a.has_value() && consumer_b.has_value());
    if (!consumer_a.has_value() || !consumer_b.has_value()) {
        return;
    }

    com::EventConsumerSet<2> set{*node};
    NX_CHECK(static_cast<bool>(set));
    NX_CHECK(set.status().has_value());
    NX_CHECK(set.attach(*consumer_a).has_value());
    NX_CHECK(set.attach(*consumer_b).has_value());
    NX_CHECK(set.size() == 2U);
    NX_CHECK(!consumer_a->wait().has_value());
    const auto exclusive = consumer_a->wait();
    NX_CHECK(!exclusive.has_value() && exclusive.error() == com::Error::InvalidState);

    NX_CHECK(set.detach(*consumer_b).has_value());
    NX_CHECK(set.size() == 1U);
    NX_CHECK(std::format("{}", set) == set.to_string());
    NX_CHECK(set.detach(*consumer_a).has_value());
    NX_CHECK(set.size() == 0U);
}

void test_consumer_set_delivery() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto channels = make_channel_pair(*node);
    NX_CHECK(channels.has_value());
    if (!channels.has_value()) {
        return;
    }
    auto& channel_a = channels->data;
    auto& channel_b = channels->other;

    std::size_t received_a = 0;
    std::size_t received_b = 0;
    auto consumer_a =
        channel_a.consumer([&received_a](const com::EventHeader&, const Pod&) { ++received_a; },
                           [](const com::LossInfo&) {});
    auto consumer_b =
        channel_b.consumer([&received_b](const com::EventHeader&, const Other&) { ++received_b; },
                           [](const com::LossInfo&) {});
    NX_CHECK(consumer_a.has_value() && consumer_b.has_value());
    if (!consumer_a.has_value() || !consumer_b.has_value()) {
        return;
    }

    com::EventConsumerSet<2> set{*node};
    NX_CHECK(static_cast<bool>(set));
    NX_CHECK(set.attach(*consumer_a).has_value());
    NX_CHECK(set.attach(*consumer_b).has_value());

    auto producer_a = channel_a.producer();
    auto producer_b = channel_b.producer();
    NX_CHECK(producer_a.has_value() && producer_b.has_value());
    if (!producer_a.has_value() || !producer_b.has_value()) {
        return;
    }
    NX_CHECK(producer_a->publish_now(Pod{1U, 0U}, 1U).has_value());
    NX_CHECK(producer_b->publish_now(Other{2U}, 2U).has_value());
    const auto outcome = set.wait_for(std::chrono::milliseconds{200});
    NX_CHECK(outcome.has_value());
    if (outcome.has_value()) {
        NX_CHECK(*outcome == com::WaitOutcome::Events || *outcome == com::WaitOutcome::WokenUp);
    }
    NX_CHECK(received_a + received_b >= 1U);

    NX_CHECK(producer_a->publish_now(Pod{3U, 0U}, 3U).has_value());
    const auto blocking = set.wait();
    NX_CHECK(blocking.has_value());
    if (blocking.has_value()) {
        NX_CHECK(*blocking == com::WaitOutcome::Events);
    }
}

void test_consumer_set_capacity() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name_a = make_name("capacity-a");
    auto name_b = make_name("capacity-b");
    NX_CHECK(name_a.has_value() && name_b.has_value());
    if (!name_a.has_value() || !name_b.has_value()) {
        return;
    }
    com::ChannelConfig config{};
    config.max_consumers = 2U;
    auto channel_a_result = com::EventChannel<Pod>::create(*node, *name_a, config);
    auto channel_b_result = com::EventChannel<Other>::create(*node, *name_b, config);
    NX_CHECK(channel_a_result.has_value() && channel_b_result.has_value());
    if (!channel_a_result.has_value() || !channel_b_result.has_value()) {
        return;
    }
    auto& channel_a = *channel_a_result;
    auto& channel_b = *channel_b_result;
    auto consumer_a =
        channel_a.consumer([](const com::EventHeader&, const Pod&) {}, [](const com::LossInfo&) {});
    auto consumer_b = channel_b.consumer([](const com::EventHeader&, const Other&) {},
                                         [](const com::LossInfo&) {});
    NX_CHECK(consumer_a.has_value() && consumer_b.has_value());
    if (!consumer_a.has_value() || !consumer_b.has_value()) {
        return;
    }
    com::EventConsumerSet<1> small{*node};
    NX_CHECK(static_cast<bool>(small));
    NX_CHECK(small.status().has_value());
    NX_CHECK(small.attach(*consumer_a).has_value());
    NX_CHECK(!small.attach(*consumer_b).has_value());
}

void test_event_options_and_limits() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("event-limit");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    const com::EventOptions options{1U, 1U, 1U};
    auto notifier = com::EventNotifier::create(*node, *name, options);
    NX_CHECK(notifier.has_value());
    auto listener_one = com::EventListener::create(*node, *name, options);
    NX_CHECK(listener_one.has_value());
    auto listener_two = com::EventListener::create(*node, *name, options);
    NX_CHECK(!listener_two.has_value());
    NX_CHECK(!listener_two.has_value() && listener_two.error() == com::Error::CreationFailed);
}

void test_default_channel_and_std_function() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("defaults");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    auto channel_result = com::EventChannel<Pod>::create(*node, *name);
    NX_CHECK(channel_result.has_value());
    if (!channel_result.has_value()) {
        return;
    }
    auto& channel = *channel_result;
    NX_CHECK(channel.config().max_consumers == 16U);
    NX_CHECK(channel.config().queue_size == 512U);
    NX_CHECK(channel.config().history_size == 16U);

    std::function<void(const com::EventHeader&, const Pod&)> on_event = [](const com::EventHeader&,
                                                                           const Pod&) {};
    std::function<void(const com::LossInfo&)> on_loss = [](const com::LossInfo&) {};
    auto consumer = channel.consumer(std::move(on_event), std::move(on_loss));
    NX_CHECK(consumer.has_value());
    auto producer = channel.producer();
    NX_CHECK(producer.has_value());
    if (!consumer.has_value() || !producer.has_value()) {
        return;
    }
    NX_CHECK(producer->publish_now(Pod{1U, 2U}, 3U).has_value());
    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(consumer->stats().received == 1U);

    auto second = channel.producer();
    NX_CHECK(!second.has_value());
}

void test_empty_publish_overload_and_timeout() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("empty-overload");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    auto channel_result = com::EventChannel<com::EmptyPayload>::create(*node, *name);
    NX_CHECK(channel_result.has_value());
    if (!channel_result.has_value()) {
        return;
    }
    auto& channel = *channel_result;
    std::size_t received = 0;
    auto consumer = channel.consumer(
        [&received](const com::EventHeader&, const com::EmptyPayload&) { ++received; },
        [](const com::LossInfo&) {});
    NX_CHECK(consumer.has_value());
    auto producer = channel.producer();
    NX_CHECK(producer.has_value());
    if (!consumer.has_value() || !producer.has_value()) {
        return;
    }
    NX_CHECK(producer->publish_now(0x99U).has_value());
    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(received == 1U);

    com::EventConsumerSet<1> set{*node};
    NX_CHECK(static_cast<bool>(set));
    const auto timeout = set.wait_for(std::chrono::milliseconds{2});
    NX_CHECK(timeout.has_value());
    if (timeout.has_value()) {
        NX_CHECK(*timeout == com::WaitOutcome::Timeout);
    }
}

void test_hardware_ring() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("hardware-ring");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    auto channel_result = com::EventChannel<Pod>::create(*node, *name);
    NX_CHECK(channel_result.has_value());
    if (!channel_result.has_value()) {
        return;
    }
    auto& channel = *channel_result;
    std::vector<com::EventHeader> received;
    auto consumer = channel.consumer(
        [&received](const com::EventHeader& header, const Pod&) { received.push_back(header); },
        [](const com::LossInfo&) {});
    NX_CHECK(consumer.has_value());
    auto producer = channel.producer();
    NX_CHECK(producer.has_value());
    if (!consumer.has_value() || !producer.has_value()) {
        return;
    }

    nexigen::core::SpscRingBuffer<com::HardwareEvent<Pod>, 8U> ring{};
    com::HardwareEvent<Pod> event{};
    event.hardware_sequence = 12U;
    event.native_ticks = 3456U;
    event.domain_id = 7U;
    event.domain_epoch = 9U;
    event.hardware_event_code = 0x77U;
    event.has_sequence = true;
    event.has_ticks = true;
    event.has_event_code = true;
    event.payload = Pod{42U, 7U};
    NX_CHECK(ring.try_push(event).has_value());
    com::HardwareEvent<Pod> popped{};
    NX_CHECK(ring.try_pop(popped).has_value());

    com::HardwareMetadata metadata{};
    metadata.sequence = popped.hardware_sequence;
    metadata.native_ticks = popped.native_ticks;
    metadata.domain_id = popped.domain_id;
    metadata.domain_epoch = popped.domain_epoch;
    metadata.event_code = popped.hardware_event_code;
    metadata.has_sequence = popped.has_sequence;
    metadata.has_ticks = popped.has_ticks;
    metadata.has_event_code = popped.has_event_code;
    const auto outcome = producer->publish(
        popped.payload, 0x77U, com::EventTime{popped.native_ticks, com::TimestampDomain::Hardware},
        metadata);
    NX_CHECK(outcome.has_value());
    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(received.size() == 1U);
    if (received.size() == 1U) {
        NX_CHECK(received[0].hardware_sequence == 12U);
        NX_CHECK(received[0].native_ticks == 3456U);
        NX_CHECK(received[0].hardware_event_code == 0x77U);
        NX_CHECK(received[0].domain_id != 0U);
    }

    // The ring is quiesced (popped above); a new source lifecycle changes the
    // producer epoch and the domain epoch while ticks stay raw.
    const std::uint64_t first_epoch = received.empty() ? 0U : received[0].producer_epoch;
    producer->on_source_reset();
    event.native_ticks = 3500U;
    event.domain_epoch = 10U;
    NX_CHECK(ring.try_push(event).has_value());
    NX_CHECK(ring.try_pop(popped).has_value());
    metadata.native_ticks = popped.native_ticks;
    metadata.domain_epoch = popped.domain_epoch;
    NX_CHECK(producer
                 ->publish(popped.payload, 0x78U,
                           com::EventTime{popped.native_ticks, com::TimestampDomain::Hardware},
                           metadata)
                 .has_value());
    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(received.size() == 2U);
    if (received.size() == 2U) {
        NX_CHECK(received[1].native_ticks == 3500U); // unchanged by the gateway
        NX_CHECK(received[1].domain_epoch == 10U);
        NX_CHECK(received[1].producer_epoch != first_epoch);
    }
}

void test_partial_hardware_flags() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("partial-hardware");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    com::ChannelConfig config{};
    config.max_consumers = 2U;
    config.queue_size = 4U;
    config.history_size = 0U;
    auto channel_result = com::EventChannel<Pod>::create(*node, *name, config);
    NX_CHECK(channel_result.has_value());
    if (!channel_result.has_value()) {
        return;
    }
    auto& channel = *channel_result;
    std::vector<com::EventHeader> received;
    auto consumer = channel.consumer(
        [&received](const com::EventHeader& header, const Pod&) { received.push_back(header); },
        [](const com::LossInfo&) {});
    auto producer = channel.producer();
    NX_CHECK(consumer.has_value() && producer.has_value());
    if (!consumer.has_value() || !producer.has_value()) {
        return;
    }

    com::HardwareMetadata sequence_only{};
    sequence_only.sequence = 5U;
    sequence_only.has_sequence = true;
    NX_CHECK(producer
                 ->publish(Pod{1U, 0U}, 1U, com::EventTime{100U, com::TimestampDomain::Hardware},
                           sequence_only)
                 .has_value());

    com::HardwareMetadata timestamp_only{};
    timestamp_only.native_ticks = 200U;
    timestamp_only.domain_id = 7U;
    timestamp_only.has_ticks = true;
    NX_CHECK(producer
                 ->publish(Pod{2U, 0U}, 2U, com::EventTime{200U, com::TimestampDomain::Hardware},
                           timestamp_only)
                 .has_value());

    com::HardwareMetadata code_only{};
    code_only.event_code = 7U;
    code_only.has_event_code = true;
    NX_CHECK(producer
                 ->publish(Pod{3U, 0U}, 3U, com::EventTime{300U, com::TimestampDomain::Hardware},
                           code_only)
                 .has_value());

    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(received.size() == 3U);
    if (received.size() == 3U) {
        NX_CHECK(received[0].has_hardware_sequence());
        NX_CHECK(!received[0].has_hardware_timestamp());
        NX_CHECK(!received[0].has_hardware_event_code());
        NX_CHECK(!received[1].has_hardware_sequence());
        NX_CHECK(received[1].has_hardware_timestamp());
        NX_CHECK(!received[1].has_hardware_event_code());
        NX_CHECK(!received[2].has_hardware_sequence());
        NX_CHECK(!received[2].has_hardware_timestamp());
        NX_CHECK(received[2].has_hardware_event_code());
    }
}

void test_source_reset_after_quiescence() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("reset-order");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    com::ChannelConfig config{};
    config.max_consumers = 2U;
    config.queue_size = 4U;
    config.history_size = 0U;
    auto channel_result = com::EventChannel<Pod>::create(*node, *name, config);
    NX_CHECK(channel_result.has_value());
    if (!channel_result.has_value()) {
        return;
    }
    auto& channel = *channel_result;
    std::vector<com::EventHeader> headers;
    std::vector<std::uint64_t> payloads;
    std::vector<com::LossInfo> losses;
    auto consumer = channel.consumer(
        [&headers, &payloads](const com::EventHeader& header, const Pod& payload) {
            headers.push_back(header);
            payloads.push_back(payload.value);
        },
        [&losses](const com::LossInfo& info) { losses.push_back(info); });
    auto producer = channel.producer();
    NX_CHECK(consumer.has_value() && producer.has_value());
    if (!consumer.has_value() || !producer.has_value()) {
        return;
    }

    nexigen::core::SpscRingBuffer<com::HardwareEvent<Pod>, 8U> ring{};
    com::HardwareEvent<Pod> old_event{};
    old_event.payload = Pod{1U, 0U};
    old_event.native_ticks = 100U;
    old_event.hardware_event_code = 1U;
    old_event.has_ticks = true;
    old_event.has_event_code = true;
    NX_CHECK(ring.try_push(old_event).has_value());
    com::HardwareEvent<Pod> popped{};
    NX_CHECK(ring.try_pop(popped).has_value());
    com::HardwareMetadata old_metadata{};
    old_metadata.native_ticks = popped.native_ticks;
    old_metadata.domain_id = popped.domain_id;
    old_metadata.domain_epoch = popped.domain_epoch;
    old_metadata.event_code = popped.hardware_event_code;
    old_metadata.has_ticks = popped.has_ticks;
    old_metadata.has_event_code = popped.has_event_code;
    NX_CHECK(producer
                 ->publish(popped.payload, 0x10U,
                           com::EventTime{popped.native_ticks, com::TimestampDomain::Hardware},
                           old_metadata)
                 .has_value());
    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(headers.size() == 1U);
    const std::uint64_t old_epoch = headers[0].producer_epoch;

    com::HardwareEvent<Pod> stale{};
    stale.payload = Pod{2U, 0U};
    stale.native_ticks = 200U;
    stale.has_ticks = true;
    NX_CHECK(ring.try_push(stale).has_value());
    com::HardwareEvent<Pod> discarded{};
    while (ring.try_pop(discarded).has_value()) {}

    producer->on_source_reset();

    com::HardwareEvent<Pod> fresh{};
    fresh.payload = Pod{3U, 0U};
    fresh.native_ticks = 300U;
    fresh.hardware_event_code = 3U;
    fresh.has_ticks = true;
    fresh.has_event_code = true;
    NX_CHECK(ring.try_push(fresh).has_value());
    NX_CHECK(ring.try_pop(popped).has_value());
    com::HardwareMetadata fresh_metadata{};
    fresh_metadata.native_ticks = popped.native_ticks;
    fresh_metadata.domain_id = popped.domain_id;
    fresh_metadata.domain_epoch = popped.domain_epoch;
    fresh_metadata.event_code = popped.hardware_event_code;
    fresh_metadata.has_ticks = popped.has_ticks;
    fresh_metadata.has_event_code = popped.has_event_code;
    NX_CHECK(producer
                 ->publish(popped.payload, 0x11U,
                           com::EventTime{popped.native_ticks, com::TimestampDomain::Hardware},
                           fresh_metadata)
                 .has_value());
    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(headers.size() == 2U && payloads.size() == 2U);
    if (headers.size() == 2U && payloads.size() == 2U) {
        NX_CHECK(headers[1].producer_epoch != old_epoch);
        NX_CHECK(headers[1].sequence_id == 0U);
        NX_CHECK(payloads[0] == 1U);
        NX_CHECK(payloads[1] == 3U);
    }
    NX_CHECK(losses.size() == 1U);
    if (losses.size() == 1U) {
        NX_CHECK(losses[0].reason == com::LossReason::ProducerRestart);
    }
}

void test_channel_port_isolation() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name_a = make_name("ports-producer");
    auto name_b = make_name("ports-consumer");
    NX_CHECK(name_a.has_value() && name_b.has_value());
    if (!name_a.has_value() || !name_b.has_value()) {
        return;
    }
    com::ChannelConfig config{};
    config.max_consumers = 1U;
    config.queue_size = 2U;
    config.history_size = 0U;

    auto producer_channel = com::EventChannel<Pod>::create(*node, *name_a, config);
    NX_CHECK(producer_channel.has_value());
    if (!producer_channel.has_value()) {
        return;
    }
    auto producer = producer_channel->producer();
    NX_CHECK(producer.has_value());
    auto external_subscriber = com::Subscriber<Pod, com::EventHeader>::create(
        *node, *name_a, config.publish_subscribe_options());
    auto external_listener = com::EventListener::create(*node, *name_a, config.event_options());
    NX_CHECK(external_subscriber.has_value());
    NX_CHECK(external_listener.has_value());

    auto consumer_channel = com::EventChannel<Pod>::create(*node, *name_b, config);
    NX_CHECK(consumer_channel.has_value());
    if (!consumer_channel.has_value()) {
        return;
    }
    auto consumer = consumer_channel->consumer([](const com::EventHeader&, const Pod&) {},
                                               [](const com::LossInfo&) {});
    NX_CHECK(consumer.has_value());
    auto external_publisher = com::Publisher<Pod, com::EventHeader>::create(
        *node, *name_b, config.publish_subscribe_options());
    auto external_notifier = com::EventNotifier::create(*node, *name_b, config.event_options());
    NX_CHECK(external_publisher.has_value());
    NX_CHECK(external_notifier.has_value());
}

void test_event_node_limit() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("node-limit");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    const com::EventOptions options{1U, 1U, 1U};
    auto notifier = com::EventNotifier::create(*node, *name, options);
    NX_CHECK(notifier.has_value());
    if (!notifier.has_value()) {
        return;
    }

    const pid_t child = fork();
    NX_CHECK(child >= 0);
    if (child == 0) {
        auto child_node = com::Node::create();
        if (!child_node.has_value()) {
            _exit(2);
        }
        auto listener = com::EventListener::create(*child_node, *name, options);
        _exit(listener.has_value() ? 1 : 0);
    }

    int status = 0;
    NX_CHECK(waitpid(child, &status, 0) == child);
    NX_CHECK(WIFEXITED(status));
    NX_CHECK(WEXITSTATUS(status) == 0);
}

void test_idle_wait_no_spin() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    com::EventConsumerSet<1> set{*node};
    NX_CHECK(static_cast<bool>(set));
    if (!set) {
        return;
    }
    const auto warmup = set.wait_for(std::chrono::milliseconds{10});
    NX_CHECK(warmup.has_value());
    if (warmup.has_value()) {
        NX_CHECK(*warmup == com::WaitOutcome::Timeout);
    }

    const std::uint64_t cpu_before = thread_cpu_time_ns();
    const auto outcome = set.wait_for(std::chrono::milliseconds{100});
    const std::uint64_t cpu_after = thread_cpu_time_ns();
    NX_CHECK(outcome.has_value());
    if (outcome.has_value()) {
        NX_CHECK(*outcome == com::WaitOutcome::Timeout);
    }
    NX_CHECK(cpu_after >= cpu_before);
    if (cpu_after >= cpu_before) {
        NX_CHECK(cpu_after - cpu_before < 50'000'000ULL);
    }
}

void test_consumer_set_sixteen_full_capacity() {
    using Consumer =
        com::EventConsumer<Pod, std::function<void(const com::EventHeader&, const Pod&)>,
                           std::function<void(const com::LossInfo&)>>;
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name_a = make_name("set-sixteen");
    auto name_b = make_name("set-seventeen");
    NX_CHECK(name_a.has_value() && name_b.has_value());
    if (!name_a.has_value() || !name_b.has_value()) {
        return;
    }
    com::ChannelConfig config{};
    config.max_consumers = 16U;
    config.queue_size = 2U;
    config.history_size = 0U;
    auto channel_a = com::EventChannel<Pod>::create(*node, *name_a, config);
    NX_CHECK(channel_a.has_value());
    if (!channel_a.has_value()) {
        return;
    }
    std::vector<Consumer> consumers;
    consumers.reserve(17U);
    for (std::size_t i = 0; i < 16U; ++i) {
        auto consumer =
            channel_a->consumer(std::function<void(const com::EventHeader&, const Pod&)>{},
                                std::function<void(const com::LossInfo&)>{});
        NX_CHECK(consumer.has_value());
        if (!consumer.has_value()) {
            return;
        }
        consumers.push_back(std::move(*consumer));
    }

    config.max_consumers = 1U;
    auto channel_b = com::EventChannel<Pod>::create(*node, *name_b, config);
    NX_CHECK(channel_b.has_value());
    if (!channel_b.has_value()) {
        return;
    }
    auto extra = channel_b->consumer(std::function<void(const com::EventHeader&, const Pod&)>{},
                                     std::function<void(const com::LossInfo&)>{});
    NX_CHECK(extra.has_value());
    if (!extra.has_value()) {
        return;
    }
    consumers.push_back(std::move(*extra));

    com::EventConsumerSet<16> set{*node};
    NX_CHECK(static_cast<bool>(set));
    if (!set) {
        return;
    }
    for (std::size_t i = 0; i < 16U; ++i) {
        NX_CHECK(set.attach(consumers[i]).has_value());
    }
    NX_CHECK(set.size() == 16U);
    const auto overflow = set.attach(consumers[16]);
    NX_CHECK(!overflow.has_value());
    if (!overflow.has_value()) {
        NX_CHECK(overflow.error() == com::Error::OutOfSamples);
    }
    NX_CHECK(set.size() == 16U);
}

void test_config_validation_and_options() {
    com::ChannelConfig invalid{};
    invalid.max_consumers = 0U;
    NX_CHECK(!invalid.validate().has_value());
    invalid = com::ChannelConfig{};
    invalid.max_consumers = 17U;
    NX_CHECK(!invalid.validate().has_value());
    invalid = com::ChannelConfig{};
    invalid.queue_size = 1U;
    invalid.history_size = 2U;
    NX_CHECK(!invalid.validate().has_value());
    invalid = com::ChannelConfig{};
    invalid.default_timestamp_domain = com::TimestampDomain::Unknown;
    NX_CHECK(!invalid.validate().has_value());

    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto invalid_name = make_name("invalid-channel");
    NX_CHECK(invalid_name.has_value());
    if (invalid_name.has_value()) {
        com::ChannelConfig invalid_channel{};
        invalid_channel.max_consumers = 0U;
        const auto rejected = com::EventChannel<Pod>::create(*node, *invalid_name, invalid_channel);
        NX_CHECK(!rejected.has_value());
        if (!rejected.has_value()) {
            NX_CHECK(rejected.error() == com::Error::InvalidArgument);
        }
    }

    auto name = make_name("options");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    com::PublishSubscribeOptions small{};
    small.max_subscribers = 2U;
    small.max_publishers = 1U;
    small.max_nodes = 3U;
    small.subscriber_max_buffer_size = 4U;
    small.subscriber_buffer_size = 4U;
    small.history_size = 2U;
    small.enable_safe_overflow = true;
    auto publisher = com::Publisher<Pod>::create(*node, *name, small);
    NX_CHECK(publisher.has_value());
    auto compatible = com::Subscriber<Pod>::create(*node, *name, small);
    NX_CHECK(compatible.has_value());
    com::PublishSubscribeOptions large = small;
    large.max_subscribers = 4U;
    large.max_nodes = 5U;
    auto incompatible = com::Subscriber<Pod>::create(*node, *name, large);
    NX_CHECK(!incompatible.has_value());
}

void test_history_configuration() {
    com::ChannelConfig config{};
    config.queue_size = 8U;
    config.history_size = 4U;
    const auto options = config.publish_subscribe_options();
    NX_CHECK(options.history_size == 4U);
    NX_CHECK(options.subscriber_history_request == 4U);
    NX_CHECK(options.subscriber_max_buffer_size == 8U);
    NX_CHECK(options.enable_safe_overflow);
}

void test_hardware_sequence_is_trace_only() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("trace-only");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    com::ChannelConfig config{};
    config.max_consumers = 2U;
    config.queue_size = 4U;
    config.history_size = 0U;
    auto channel_result = com::EventChannel<Pod>::create(*node, *name, config);
    NX_CHECK(channel_result.has_value());
    if (!channel_result.has_value()) {
        return;
    }
    auto& channel = *channel_result;
    std::vector<std::uint64_t> sequences;
    std::size_t losses = 0;
    auto consumer = channel.consumer(
        [&sequences](const com::EventHeader& header, const Pod&) {
            sequences.push_back(header.hardware_sequence);
        },
        [&losses](const com::LossInfo&) { ++losses; });
    NX_CHECK(consumer.has_value());
    auto producer = channel.producer();
    NX_CHECK(producer.has_value());
    if (!consumer.has_value() || !producer.has_value()) {
        return;
    }
    for (const std::uint64_t hardware : {10U, 2U, 7U}) {
        com::HardwareMetadata hw{};
        hw.sequence = hardware;
        hw.has_sequence = true;
        NX_CHECK(producer
                     ->publish(Pod{hardware, 0U}, 1U,
                               com::EventTime{0U, com::TimestampDomain::Hardware}, hw)
                     .has_value());
    }
    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(losses == 0U);
    NX_CHECK(sequences.size() == 3U);
    NX_CHECK(sequences[0] == 10U && sequences[1] == 2U && sequences[2] == 7U);
}

void test_notify_outcome_mapping() {
    const com::Status success{};
    NX_CHECK(com::detail::publish_outcome_from_notify(success) == com::PublishOutcome::Notified);
    const com::Status failed = std::unexpected(com::Error::ConnectionFailure);
    NX_CHECK(com::detail::publish_outcome_from_notify(failed) == com::PublishOutcome::NotifyFailed);
}

void test_allocation_free_hot_paths() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("alloc");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    com::ChannelConfig config{};
    config.max_consumers = 2U;
    config.queue_size = 8U;
    config.history_size = 0U;
    auto channel_result = com::EventChannel<Pod>::create(*node, *name, config);
    NX_CHECK(channel_result.has_value());
    if (!channel_result.has_value()) {
        return;
    }
    auto& channel = *channel_result;
    auto producer = channel.producer();
    auto consumer =
        channel.consumer([](const com::EventHeader&, const Pod&) {}, [](const com::LossInfo&) {});
    NX_CHECK(producer.has_value() && consumer.has_value());
    if (!producer.has_value() || !consumer.has_value()) {
        return;
    }
    NX_CHECK(producer->publish_now(Pod{0U, 0U}, 1U).has_value());
    NX_CHECK(consumer->wait().has_value());

    nexigen::core::SpscRingBuffer<com::HardwareEvent<Pod>, 8U> ring{};
    com::HardwareEvent<Pod> event{};
    event.payload = Pod{1U, 1U};
    event.native_ticks = 10U;
    event.has_ticks = true;
    com::HardwareEvent<Pod> popped{};

    g_allocation_count.store(0U, std::memory_order_relaxed);
    g_count_allocations.store(true, std::memory_order_relaxed);
    NX_CHECK(ring.try_push(event).has_value());
    NX_CHECK(ring.try_pop(popped).has_value());
    NX_CHECK(producer->publish_now(popped.payload, 1U).has_value());
    NX_CHECK(consumer->drain().has_value());
    g_count_allocations.store(false, std::memory_order_relaxed);
    NX_CHECK(g_allocation_count.load(std::memory_order_relaxed) == 0U);
}

void test_wake_from_thread() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    com::EventConsumerSet<1> set{*node};
    NX_CHECK(static_cast<bool>(set));
    if (!set) {
        return;
    }
    std::thread waker([&set] {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        set.wake_up();
    });
    const auto outcome = set.wait_for(std::chrono::seconds{1});
    waker.join();
    NX_CHECK(outcome.has_value());
    if (outcome.has_value()) {
        NX_CHECK(*outcome == com::WaitOutcome::WokenUp);
    }
}

void test_endpoint_limits() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto data_name = make_name("limit-data");
    NX_CHECK(data_name.has_value());
    if (!data_name.has_value()) {
        return;
    }
    const com::PublishSubscribeOptions options{16U, 1U, 17U, 4U, 4U, 0U, 0U, true};
    auto publisher = com::Publisher<Pod>::create(*node, *data_name, options);
    NX_CHECK(publisher.has_value());
    std::vector<com::Subscriber<Pod>> subscribers;
    subscribers.reserve(16U);
    for (std::size_t i = 0; i < 16U; ++i) {
        auto subscriber = com::Subscriber<Pod>::create(*node, *data_name, options);
        NX_CHECK(subscriber.has_value());
        if (subscriber.has_value()) {
            subscribers.push_back(std::move(*subscriber));
        }
    }
    NX_CHECK(subscribers.size() == 16U);
    auto extra_subscriber = com::Subscriber<Pod>::create(*node, *data_name, options);
    NX_CHECK(!extra_subscriber.has_value());

    auto event_name = make_name("limit-event");
    NX_CHECK(event_name.has_value());
    if (!event_name.has_value()) {
        return;
    }
    const com::EventOptions event_options{16U, 1U, 17U};
    auto notifier = com::EventNotifier::create(*node, *event_name, event_options);
    NX_CHECK(notifier.has_value());
    std::vector<com::EventListener> listeners;
    listeners.reserve(16U);
    for (std::size_t i = 0; i < 16U; ++i) {
        auto listener = com::EventListener::create(*node, *event_name, event_options);
        NX_CHECK(listener.has_value());
        if (listener.has_value()) {
            listeners.push_back(std::move(*listener));
        }
    }
    NX_CHECK(listeners.size() == 16U);
    auto extra_listener = com::EventListener::create(*node, *event_name, event_options);
    NX_CHECK(!extra_listener.has_value());
    auto extra_notifier = com::EventNotifier::create(*node, *event_name, event_options);
    NX_CHECK(!extra_notifier.has_value());
}

/// @brief Clears injected producer faults on construction and destruction.
struct FaultGuard {
    FaultGuard() noexcept { com::detail::EventProducerFaults::reset(); }
    ~FaultGuard() { com::detail::EventProducerFaults::reset(); }
    FaultGuard(const FaultGuard&) = delete;
    FaultGuard& operator=(const FaultGuard&) = delete;
};

void test_failed_send_preserves_sequence() {
    FaultGuard guard;
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("failed-send");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    com::ChannelConfig config{};
    config.max_consumers = 2U;
    config.queue_size = 8U;
    config.history_size = 0U;
    auto channel_result = com::EventChannel<Pod>::create(*node, *name, config);
    NX_CHECK(channel_result.has_value());
    if (!channel_result.has_value()) {
        return;
    }
    auto& channel = *channel_result;
    std::vector<com::EventHeader> received;
    auto consumer = channel.consumer(
        [&received](const com::EventHeader& header, const Pod&) { received.push_back(header); },
        [](const com::LossInfo&) {});
    auto producer = channel.producer();
    NX_CHECK(consumer.has_value() && producer.has_value());
    if (!consumer.has_value() || !producer.has_value()) {
        return;
    }

    auto outcome = producer->publish_now(Pod{1U, 1U}, 1U);
    NX_CHECK(outcome.has_value());
    if (outcome.has_value()) {
        NX_CHECK(*outcome == com::PublishOutcome::Notified);
    }
    NX_CHECK(producer->stats().published == 1U);
    NX_CHECK(producer->stats().current_sequence == 1U);

    com::detail::EventProducerFaults::fail_send = true;
    const auto failed = producer->publish_now(Pod{2U, 2U}, 2U);
    NX_CHECK(!failed.has_value());
    if (!failed.has_value()) {
        NX_CHECK(failed.error() == com::Error::UnableToDeliver);
    }
    NX_CHECK(producer->stats().published == 1U);
    NX_CHECK(producer->stats().current_sequence == 1U);
    NX_CHECK(producer->stats().send_failures == 1U);
    com::detail::EventProducerFaults::fail_send = false;

    com::detail::EventProducerFaults::fail_loan = true;
    const auto no_sample = producer->publish_now(Pod{3U, 3U}, 3U);
    NX_CHECK(!no_sample.has_value());
    if (!no_sample.has_value()) {
        NX_CHECK(no_sample.error() == com::Error::OutOfSamples);
    }
    NX_CHECK(producer->stats().published == 1U);
    NX_CHECK(producer->stats().current_sequence == 1U);
    NX_CHECK(producer->stats().send_failures == 2U);
    com::detail::EventProducerFaults::fail_loan = false;

    const auto recovered = producer->publish_now(Pod{4U, 4U}, 4U);
    NX_CHECK(recovered.has_value());
    if (recovered.has_value()) {
        NX_CHECK(*recovered == com::PublishOutcome::Notified);
    }
    NX_CHECK(producer->stats().published == 2U);
    NX_CHECK(producer->stats().current_sequence == 2U);

    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(received.size() == 2U);
    if (received.size() == 2U) {
        NX_CHECK(received[0].sequence_id == 0U);
        NX_CHECK(received[1].sequence_id == 1U);
    }
}

void test_notify_failure_and_retry() {
    FaultGuard guard;
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    auto name = make_name("failed-notify");
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    com::ChannelConfig config{};
    config.max_consumers = 2U;
    config.queue_size = 8U;
    config.history_size = 0U;
    auto channel_result = com::EventChannel<Pod>::create(*node, *name, config);
    NX_CHECK(channel_result.has_value());
    if (!channel_result.has_value()) {
        return;
    }
    auto& channel = *channel_result;
    std::vector<com::EventHeader> received;
    auto consumer = channel.consumer(
        [&received](const com::EventHeader& header, const Pod&) { received.push_back(header); },
        [](const com::LossInfo&) {});
    auto producer = channel.producer();
    NX_CHECK(consumer.has_value() && producer.has_value());
    if (!consumer.has_value() || !producer.has_value()) {
        return;
    }

    com::detail::EventProducerFaults::fail_notify = true;
    const auto outcome = producer->publish_now(Pod{7U, 7U}, 7U);
    NX_CHECK(outcome.has_value());
    if (outcome.has_value()) {
        NX_CHECK(*outcome == com::PublishOutcome::NotifyFailed);
    }
    NX_CHECK(producer->stats().published == 1U);
    NX_CHECK(producer->stats().current_sequence == 1U);
    NX_CHECK(producer->stats().notify_failures == 1U);
    NX_CHECK(producer->stats().last_notify_error == com::Error::ConnectionFailure);

    com::detail::EventProducerFaults::fail_notify = false;
    NX_CHECK(producer->notify().has_value());
    NX_CHECK(producer->stats().published == 1U);
    NX_CHECK(producer->stats().current_sequence == 1U);

    NX_CHECK(consumer->wait().has_value());
    NX_CHECK(received.size() == 1U);
    if (received.size() == 1U) {
        NX_CHECK(received[0].sequence_id == 0U);
        NX_CHECK(received[0].event_code == 7U);
    }

    const auto second = producer->publish_now(Pod{8U, 8U}, 8U);
    NX_CHECK(second.has_value());
    if (second.has_value()) {
        NX_CHECK(*second == com::PublishOutcome::Notified);
    }
    NX_CHECK(producer->stats().published == 2U);
    NX_CHECK(producer->stats().current_sequence == 2U);
}

} // namespace

int main() {
    test_value_serialization();
    test_enum_roundtrip_table();
    test_sequence_tracker();
    test_channel_roundtrip();
    test_empty_payload_and_hardware();
    test_partial_hardware_flags();
    test_gap_from_slow_consumer();
    test_consumer_set();
    test_consumer_set_delivery();
    test_consumer_set_capacity();
    test_consumer_set_sixteen_full_capacity();
    test_event_options_and_limits();
    test_default_channel_and_std_function();
    test_empty_publish_overload_and_timeout();
    test_hardware_ring();
    test_source_reset_after_quiescence();
    test_channel_port_isolation();
    test_event_node_limit();
    test_config_validation_and_options();
    test_history_configuration();
    test_hardware_sequence_is_trace_only();
    test_notify_outcome_mapping();
    test_allocation_free_hot_paths();
    test_wake_from_thread();
    test_idle_wait_no_spin();
    test_endpoint_limits();
    test_failed_send_preserves_sequence();
    test_notify_failure_and_retry();
    std::printf("event-system tests: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
