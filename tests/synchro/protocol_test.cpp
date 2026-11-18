// Protocol vocabulary, validation, and serialization tests
// (change add-synchro-event-sync; spec synchro/protocol).
#include "synchro_test_support.hpp"

#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/com/channel_config.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/synchro/late_event_policy.hpp>
#include <nexigen/synchro/past_target_policy.hpp>
#include <nexigen/synchro/start_mode.hpp>
#include <nexigen/synchro/start_policy.hpp>
#include <nexigen/synchro/sync_channel_config.hpp>
#include <nexigen/synchro/sync_config.hpp>
#include <nexigen/synchro/sync_consumer_config.hpp>
#include <nexigen/synchro/sync_consumer_state.hpp>
#include <nexigen/synchro/sync_consumer_stats.hpp>
#include <nexigen/synchro/sync_context.hpp>
#include <nexigen/synchro/sync_error.hpp>
#include <nexigen/synchro/sync_error_kind.hpp>
#include <nexigen/synchro/sync_generator_stats.hpp>
#include <nexigen/synchro/sync_payload.hpp>
#include <nexigen/synchro/sync_state.hpp>

namespace {

using namespace nexigen;

template <class Enum> void check_enum_roundtrip(Enum value) {
    const std::string_view name = synchro::to_string(value);
    Enum parsed{};
    NX_CHECK(synchro::from_string(name, parsed));
    NX_CHECK(parsed == value);

    nlohmann::json encoded = value;
    NX_CHECK(encoded.is_string());
    NX_CHECK(encoded.get<std::string>() == name);
    Enum decoded{};
    nlohmann::json::parse(encoded.dump()).get_to(decoded);
    NX_CHECK(decoded == value);

    Enum untouched = value;
    nlohmann::json hostile = 42;
    from_json(hostile, untouched);
    NX_CHECK(untouched == value);
}

synchro::SyncChannelConfig valid_channel() {
    synchro::SyncChannelConfig config{};
    config.service_name = "Nexigen/SynchroTest/Protocol";
    config.cycle_period_ns = 10'000'000;
    config.phases_per_cycle = 10;
    config.channel = com::ChannelConfig{};
    return config;
}

void test_payload_layout() {
    static_assert(sizeof(synchro::SyncPayload) == 32U);
    static_assert(alignof(synchro::SyncPayload) == 8U);
    static_assert(std::is_trivially_copyable_v<synchro::SyncPayload>);

    synchro::SyncPayload payload{};
    payload.cycle_number = 7U;
    payload.phase_index = 3U;
    payload.phases_total = 10U;
    payload.scheduled_time_ns = 123'456'789U;
    payload.cycle_period_ns = 100'000'000U;

    const nlohmann::json encoded = payload;
    NX_CHECK_EQ(encoded.at("cycle_number").get<std::uint64_t>(), 7U);
    NX_CHECK_EQ(encoded.at("phase_index").get<std::uint32_t>(), 3U);
    NX_CHECK_EQ(encoded.at("phases_total").get<std::uint32_t>(), 10U);
    NX_CHECK_EQ(encoded.at("scheduled_time_ns").get<std::uint64_t>(), 123'456'789U);
    NX_CHECK_EQ(encoded.at("cycle_period_ns").get<std::uint64_t>(), 100'000'000U);

    synchro::SyncPayload decoded{};
    nlohmann::json::parse(encoded.dump()).get_to(decoded);
    NX_CHECK_EQ(decoded.cycle_number, 7U);
    NX_CHECK_EQ(decoded.phase_index, 3U);
    NX_CHECK_EQ(decoded.phases_total, 10U);
    NX_CHECK_EQ(decoded.scheduled_time_ns, 123'456'789U);
    NX_CHECK_EQ(decoded.cycle_period_ns, 100'000'000U);

    NX_CHECK(std::format("{}", payload) == payload.to_string());

    synchro::SyncPayload untouched = decoded;
    nlohmann::json hostile{{"cycle_number", -5}, {"phase_index", "x"}};
    from_json(hostile, untouched);
    NX_CHECK_EQ(untouched.cycle_number, 7U);
    NX_CHECK_EQ(untouched.phase_index, 3U);
}

void test_channel_validation() {
    auto channel = valid_channel();
    NX_CHECK(channel.validate().has_value());
    NX_CHECK_EQ(channel.phase_period_ns(), 1'000'000U);

    auto empty_name = channel;
    empty_name.service_name.clear();
    NX_CHECK(!empty_name.validate().has_value());

    auto zero_period = channel;
    zero_period.cycle_period_ns = 0U;
    NX_CHECK(!zero_period.validate().has_value());

    auto zero_phases = channel;
    zero_phases.phases_per_cycle = 0U;
    NX_CHECK(!zero_phases.validate().has_value());

    auto too_fast = channel;
    too_fast.cycle_period_ns = 9'999'999U; // 999,999 ns per phase
    NX_CHECK(!too_fast.validate().has_value());

    auto bad_limits = channel;
    bad_limits.channel.max_consumers = 17U;
    NX_CHECK(!bad_limits.validate().has_value());
}

void test_config_validation() {
    synchro::SyncConfig config{};
    config.channel = valid_channel();
    config.spin_threshold_ns = 30'000U;
    NX_CHECK(config.validate().has_value());

    auto spin_too_large = config;
    spin_too_large.spin_threshold_ns = spin_too_large.channel.phase_period_ns();
    NX_CHECK(!spin_too_large.validate().has_value());

    auto bad_start = config;
    bad_start.start.past_target = static_cast<synchro::PastTargetPolicy>(99);
    NX_CHECK(!bad_start.validate().has_value());

    synchro::SyncConsumerConfig consumer{};
    consumer.channel = valid_channel();
    NX_CHECK(consumer.validate().has_value());

    auto bad_policy = consumer;
    bad_policy.late_policy = static_cast<synchro::LateEventPolicy>(99);
    NX_CHECK(!bad_policy.validate().has_value());

    NX_CHECK_EQ(consumer.resolve_max_latency(0U), 0U);
    NX_CHECK_EQ(consumer.resolve_max_latency(5U), 5U);
    consumer.max_allowed_latency_ns = 7U;
    NX_CHECK_EQ(consumer.resolve_max_latency(5U), 7U);
}

void test_enum_roundtrips() {
    check_enum_roundtrip(synchro::StartMode::NextCycle);
    check_enum_roundtrip(synchro::StartMode::AtTarget);
    check_enum_roundtrip(synchro::PastTargetPolicy::Reject);
    check_enum_roundtrip(synchro::PastTargetPolicy::RealignToNextCycle);
    check_enum_roundtrip(synchro::LateEventPolicy::DropAndWarn);
    check_enum_roundtrip(synchro::LateEventPolicy::DeliverWithFlag);
    check_enum_roundtrip(synchro::SyncState::Stopped);
    check_enum_roundtrip(synchro::SyncState::WaitingForStart);
    check_enum_roundtrip(synchro::SyncState::Running);
    check_enum_roundtrip(synchro::SyncState::Faulted);
    check_enum_roundtrip(synchro::SyncConsumerState::Unsynced);
    check_enum_roundtrip(synchro::SyncConsumerState::Locked);
    check_enum_roundtrip(synchro::SyncErrorKind::InvalidConfig);
    check_enum_roundtrip(synchro::SyncErrorKind::Transport);
    check_enum_roundtrip(synchro::SyncErrorKind::Metrics);
    check_enum_roundtrip(synchro::SyncErrorKind::Clock);
    check_enum_roundtrip(synchro::SyncErrorKind::EventFd);
    check_enum_roundtrip(synchro::SyncErrorKind::Thread);
    check_enum_roundtrip(synchro::SyncErrorKind::Lifecycle);
}

void test_error_model() {
    const auto transport = synchro::make_transport_error(com::Error::ConnectionFailure);
    NX_CHECK(transport.kind == synchro::SyncErrorKind::Transport);
    NX_CHECK(transport.transport == com::Error::ConnectionFailure);

    const auto metrics = synchro::make_metrics_error(core::ErrorCode::BufferFull);
    NX_CHECK(metrics.kind == synchro::SyncErrorKind::Metrics);
    NX_CHECK(metrics.core == core::ErrorCode::BufferFull);

    const auto os = synchro::make_os_error(synchro::SyncErrorKind::EventFd, 24);
    NX_CHECK(os.kind == synchro::SyncErrorKind::EventFd);
    NX_CHECK_EQ(os.os_errno, 24);
    NX_CHECK(os.core == core::ErrorCode::SystemError);

    const nlohmann::json encoded = transport;
    synchro::SyncError decoded{};
    nlohmann::json::parse(encoded.dump()).get_to(decoded);
    NX_CHECK(decoded.kind == transport.kind);
    NX_CHECK(decoded.transport == transport.transport);
    NX_CHECK(decoded.core == transport.core);
    NX_CHECK_EQ(decoded.os_errno, transport.os_errno);
    NX_CHECK(std::format("{}", transport) == transport.to_string());

    synchro::SyncError untouched = transport;
    nlohmann::json hostile{{"kind", 5}, {"transport", "NoSuchError"}, {"core", false}};
    from_json(hostile, untouched);
    NX_CHECK(untouched.kind == transport.kind);
    NX_CHECK(untouched.transport == transport.transport);
    NX_CHECK(untouched.core == transport.core);
}

void test_schedule_policy_serialization() {
    synchro::StartPolicy policy{};
    policy.mode = synchro::StartMode::AtTarget;
    policy.target_time_ns = 42U;
    policy.align_target_to_cycle = false;
    policy.past_target = synchro::PastTargetPolicy::RealignToNextCycle;

    const nlohmann::json encoded = policy;
    synchro::StartPolicy decoded{};
    nlohmann::json::parse(encoded.dump()).get_to(decoded);
    NX_CHECK(decoded.mode == policy.mode);
    NX_CHECK_EQ(decoded.target_time_ns, policy.target_time_ns);
    NX_CHECK(!decoded.align_target_to_cycle);
    NX_CHECK(decoded.past_target == policy.past_target);
    NX_CHECK(std::format("{}", policy) == policy.to_string());

    synchro::StartPolicy untouched = policy;
    nlohmann::json hostile{{"mode", 1}, {"past_target", "NoSuchPolicy"}};
    from_json(hostile, untouched);
    NX_CHECK(untouched.mode == policy.mode);
    NX_CHECK(untouched.past_target == policy.past_target);
}

void test_config_serialization() {
    auto channel = valid_channel();
    const nlohmann::json channel_json = channel;
    synchro::SyncChannelConfig decoded_channel{};
    nlohmann::json::parse(channel_json.dump()).get_to(decoded_channel);
    NX_CHECK(decoded_channel.service_name == channel.service_name);
    NX_CHECK_EQ(decoded_channel.cycle_period_ns, channel.cycle_period_ns);
    NX_CHECK_EQ(decoded_channel.phases_per_cycle, channel.phases_per_cycle);
    NX_CHECK(std::format("{}", channel) == channel.to_string());

    synchro::SyncConfig config{};
    config.channel = channel;
    config.spin_threshold_ns = 25U;
    config.event_code = 7U;
    const nlohmann::json config_json = config;
    synchro::SyncConfig decoded_config{};
    nlohmann::json::parse(config_json.dump()).get_to(decoded_config);
    NX_CHECK_EQ(decoded_config.spin_threshold_ns, 25U);
    NX_CHECK_EQ(decoded_config.event_code, 7U);
    NX_CHECK(decoded_config.start.mode == config.start.mode);
    NX_CHECK(std::format("{}", config) == config.to_string());

    synchro::SyncConsumerConfig consumer{};
    consumer.channel = channel;
    consumer.max_allowed_latency_ns = 11U;
    consumer.sync_loss_timeout_ns = 12U;
    consumer.initial_sync_timeout_ns = 13U;
    consumer.late_policy = synchro::LateEventPolicy::DeliverWithFlag;
    const nlohmann::json consumer_json = consumer;
    synchro::SyncConsumerConfig decoded_consumer{};
    nlohmann::json::parse(consumer_json.dump()).get_to(decoded_consumer);
    NX_CHECK_EQ(decoded_consumer.max_allowed_latency_ns, 11U);
    NX_CHECK_EQ(decoded_consumer.sync_loss_timeout_ns, 12U);
    NX_CHECK_EQ(decoded_consumer.initial_sync_timeout_ns, 13U);
    NX_CHECK(decoded_consumer.late_policy == consumer.late_policy);
    NX_CHECK(std::format("{}", consumer) == consumer.to_string());
}

void test_stats_and_context_serialization() {
    synchro::SyncGeneratorStats generator{};
    generator.cycles_completed = 3U;
    generator.phases_published = 30U;
    generator.overrun_slots = 2U;
    generator.overrun_episodes = 1U;
    generator.send_failures = 4U;
    generator.notify_failures = 5U;
    generator.start_time_ns = 6U;
    generator.last_phase_time_ns = 7U;
    generator.last_jitter_ns = -8;
    generator.max_jitter_ns = 9U;

    const nlohmann::json generator_json = generator;
    synchro::SyncGeneratorStats decoded_generator{};
    nlohmann::json::parse(generator_json.dump()).get_to(decoded_generator);
    NX_CHECK_EQ(decoded_generator.cycles_completed, 3U);
    NX_CHECK_EQ(decoded_generator.phases_published, 30U);
    NX_CHECK_EQ(decoded_generator.last_jitter_ns, -8);
    NX_CHECK(std::format("{}", generator) == generator.to_string());

    synchro::SyncConsumerStats consumer{};
    consumer.received = 1U;
    consumer.dropped_late = 2U;
    consumer.transport_lost = 3U;
    consumer.generator_skips = 4U;
    consumer.last_ipc_latency_ns = -5;
    consumer.last_e2e_latency_ns = 6;
    consumer.absolute_phase = 7U;

    const nlohmann::json consumer_json = consumer;
    synchro::SyncConsumerStats decoded_consumer{};
    nlohmann::json::parse(consumer_json.dump()).get_to(decoded_consumer);
    NX_CHECK_EQ(decoded_consumer.received, 1U);
    NX_CHECK_EQ(decoded_consumer.transport_lost, 3U);
    NX_CHECK_EQ(decoded_consumer.last_ipc_latency_ns, -5);
    NX_CHECK_EQ(decoded_consumer.absolute_phase, 7U);
    NX_CHECK(std::format("{}", consumer) == consumer.to_string());

    static_assert(sizeof(synchro::SyncContext) == 88U);
    synchro::SyncContext context{};
    context.cycle_number = 2U;
    context.phase_index = 3U;
    context.phases_total = 10U;
    context.scheduled_time_ns = 4U;
    context.cycle_period_ns = 5U;
    context.publish_time_ns = 6U;
    context.receive_time_ns = 7U;
    context.sequence_id = 8U;
    context.producer_epoch = 9U;
    context.ipc_latency_ns = -10;
    context.e2e_latency_ns = 11;
    context.is_late = true;

    const nlohmann::json context_json = context;
    synchro::SyncContext decoded_context{};
    nlohmann::json::parse(context_json.dump()).get_to(decoded_context);
    NX_CHECK_EQ(decoded_context.cycle_number, 2U);
    NX_CHECK_EQ(decoded_context.phase_index, 3U);
    NX_CHECK_EQ(decoded_context.sequence_id, 8U);
    NX_CHECK_EQ(decoded_context.producer_epoch, 9U);
    NX_CHECK_EQ(decoded_context.ipc_latency_ns, -10);
    NX_CHECK_EQ(decoded_context.e2e_latency_ns, 11);
    NX_CHECK(decoded_context.is_late);
    NX_CHECK(std::format("{}", context) == context.to_string());

    synchro::SyncContext untouched = context;
    nlohmann::json hostile{{"cycle_number", "x"}, {"is_late", 1}};
    from_json(hostile, untouched);
    NX_CHECK_EQ(untouched.cycle_number, 2U);
    NX_CHECK(untouched.is_late);
}

} // namespace

void run_protocol_suite() {
    run_suite("payload layout", test_payload_layout);
    run_suite("channel validation", test_channel_validation);
    run_suite("config validation", test_config_validation);
    run_suite("enum roundtrips", test_enum_roundtrips);
    run_suite("error model", test_error_model);
    run_suite("start policy serialization", test_schedule_policy_serialization);
    run_suite("config serialization", test_config_serialization);
    run_suite("stats/context serialization", test_stats_and_context_serialization);
}
