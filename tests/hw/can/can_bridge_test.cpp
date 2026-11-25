// COM-gated bridge tests: driver-open rollback, packet admission and transmit accounting.
#include "fake_can_driver.hpp"
#include <chrono>
#include <array>
#include <cstdio>
#include <format>
#include <string>
#include <thread>
#include <vector>

#include <nexigen/hw/can/can_channel_config.hpp>
#include <nexigen/hw/can/can_message_batch.hpp>
#include <nexigen/hw/can/com/can_receiver.hpp>
#include <nexigen/hw/can/com/can_transmitter.hpp>

namespace can = nexigen::hw::can;

namespace {
int failures = 0;
int checks = 0;

void check(bool condition, const char* text) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", text);
    }
}

can::CanChannelConfig channel(std::string name, std::uint16_t id) {
    can::CanChannelConfig config{};
    config.name = std::move(name);
    config.channel_id = id;
    config.driver_channel = config.name;
    config.bit_rate = 500'000;
    return config;
}

can::CanMessage make_message(std::uint16_t channel_id, std::uint32_t can_id,
                             std::uint64_t timestamp_ns) {
    can::CanMessage message{};
    message.kind = can::CanMessageKind::ClassicData;
    message.channel_id = channel_id;
    message.can_id = can_id;
    message.dlc = 1U;
    message.data[0] = static_cast<std::uint8_t>(can_id & 0xFFU);
    message.timestamp_source = can::CanTimestampSource::Simulated;
    message.hardware_timestamp_ns = timestamp_ns;
    message.frame_flags.bits = can::CanFrameFlags::has_hardware_timestamp;
    return message;
}

can::CanMessageBatch make_packet(std::uint16_t channel_id, std::size_t count,
                                 std::uint64_t base_timestamp = 1000U) {
    can::CanMessageBatch packet{};
    packet.channel_id = channel_id;
    packet.count = static_cast<std::uint16_t>(count);
    for (std::size_t index = 0; index < count; ++index) {
        packet.messages[index] = make_message(
            channel_id, static_cast<std::uint32_t>(0x100U + index), base_timestamp + index);
    }
    return packet;
}

template <class Predicate> bool wait_for(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{800};
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    return predicate();
}

void test_packet_admission() {
    can::test::FakeCanDriver driver{};
    can::CanChannelConfig config = channel("can0", 1);
    config.fd_enabled = true;
    const std::vector<can::CanChannelConfig> configs{config};
    can::CanTransmitter transmitter{driver, configs};
    check(transmitter.start().has_value(), "packet transmitter starts");
    can::CanMessageBatch packet = make_packet(1U, can::CanMessageBatch::max_messages);
    for (std::size_t index = 0; index < packet.count; ++index) {
        can::CanMessage& message = packet.messages[index];
        message.hardware_sequence = 5000U + index;
        message.frame_flags.bits |= can::CanFrameFlags::has_hardware_sequence;
        switch (index % 3U) {
            case 0:
                message.kind = can::CanMessageKind::ClassicData;
                message.dlc = 8U;
                break;
            case 1:
                message.kind = can::CanMessageKind::FdData;
                message.dlc = 8U;
                break;
            default:
                message.kind = can::CanMessageKind::Remote;
                message.dlc = 0U;
                break;
        }
        message.data[0] = static_cast<std::uint8_t>(index);
    }
    check(transmitter.enqueue_batch("can0", packet).has_value(), "packet admitted");
    check(wait_for([&] { return transmitter.submitted() == can::CanMessageBatch::max_messages; }),
          "packet submitted");
    transmitter.stop();

    check(driver.batches.size() == 1U, "one driver batch");
    bool metadata_preserved = !driver.batches.empty();
    if (!driver.batches.empty()) {
        const can::CanMessageBatch& batch = driver.batches.front();
        metadata_preserved = batch.count == can::CanMessageBatch::max_messages;
        for (std::size_t index = 0; index < batch.count; ++index) {
            const can::CanMessage& message = batch.messages[index];
            const can::CanMessageKind expected_kind =
                index % 3U == 0U   ? can::CanMessageKind::ClassicData
                : index % 3U == 1U ? can::CanMessageKind::FdData
                                   : can::CanMessageKind::Remote;
            metadata_preserved = metadata_preserved && message.hardware_sequence == 5000U + index &&
                                 message.kind == expected_kind &&
                                 message.can_id == 0x100U + index &&
                                 message.hardware_timestamp_ns == 1000U + index &&
                                 message.data[0] == static_cast<std::uint8_t>(index);
        }
        check(batch.messages.front().hardware_timestamp_ns == 1000U, "batch first timestamp");
        check(batch.messages.front().can_id == 0x100U, "batch first identifier");
        check(batch.messages.back().hardware_timestamp_ns == 1099U, "batch last timestamp");
        check(batch.messages.back().can_id == 0x163U, "batch last identifier");
    }
    check(metadata_preserved, "batch order and metadata preserved");
    check(transmitter.submitted() == can::CanMessageBatch::max_messages, "submitted counter");
    check(transmitter.dropped() == 0U, "no drops");
    check(transmitter.rejected() == 0U, "no rejections");
    check(transmitter.failed() == 0U, "no failures");
    const std::string text = transmitter.to_string();
    check(text.find("channels=1") != std::string::npos, "diagnostic reports channels");
    check(text.find("submitted=100") != std::string::npos, "diagnostic reports submitted");
    check(std::format("{}", transmitter) == text, "formatter matches diagnostic");
}

void test_packet_rejection() {
    can::test::FakeCanDriver driver{};
    const std::vector<can::CanChannelConfig> configs{channel("can0", 1)};
    can::CanTransmitter transmitter{driver, configs};

    can::CanMessageBatch empty{};
    empty.channel_id = 1U;
    const auto empty_result = transmitter.enqueue_batch("can0", empty);
    check(!empty_result.has_value() &&
              empty_result.error() == nexigen::core::ErrorCode::InvalidArgument,
          "empty packet rejected with InvalidArgument");
    check(transmitter.rejected() == 0U, "empty packet changes no counters");

    can::CanMessageBatch oversized = make_packet(1U, 1U);
    oversized.count = 101U;
    const auto oversized_result = transmitter.enqueue_batch("can0", oversized);
    check(!oversized_result.has_value() &&
              oversized_result.error() == nexigen::core::ErrorCode::InvalidArgument,
          "oversized packet rejected with InvalidArgument");
    check(transmitter.rejected() == 101U, "oversized packet counts rejected messages");

    can::CanMessageBatch wrong_channel = make_packet(2U, 2U);
    check(!transmitter.enqueue_batch("can0", wrong_channel).has_value(),
          "packet channel mismatch rejected");

    can::CanMessageBatch wrong_member = make_packet(1U, 2U);
    wrong_member.messages[1].channel_id = 2U;
    check(!transmitter.enqueue_batch("can0", wrong_member).has_value(),
          "contained channel mismatch rejected");

    can::CanMessageBatch error_packet = make_packet(1U, 2U);
    error_packet.messages[0].kind = can::CanMessageKind::Error;
    error_packet.messages[0].dlc = 0U;
    check(!transmitter.enqueue_batch("can0", error_packet).has_value(), "error packet rejected");

    can::CanMessageBatch fd_packet = make_packet(1U, 2U);
    fd_packet.messages[0].kind = can::CanMessageKind::FdData;
    check(!transmitter.enqueue_batch("can0", fd_packet).has_value(),
          "fd packet rejected on classic channel");

    check(transmitter.rejected() == 109U, "rejections counted per message");
    check(transmitter.dropped() == 0U, "rejection does not count as a drop");
    check(transmitter.submitted() == 0U, "rejected packet was not submitted");

    const can::CanMessageBatch valid = make_packet(1U, 2U);
    check(transmitter.enqueue_batch("can0", valid).has_value(),
          "valid packet still admitted after rejections");
}

void test_full_queue_accounting() {
    can::test::FakeCanDriver driver{};
    const std::vector<can::CanChannelConfig> configs{channel("can0", 1)};
    can::CanTransmitter transmitter{driver, configs};
    const can::CanMessageBatch packet = make_packet(1U, can::CanMessageBatch::max_messages);
    for (int attempt = 0; attempt < 15; ++attempt) {
        check(transmitter.enqueue_batch("can0", packet).has_value(), "ring slot admitted");
    }
    check(!transmitter.enqueue_batch("can0", packet).has_value(), "full ring rejects packet");
    check(transmitter.dropped() == can::CanMessageBatch::max_messages,
          "full ring drops the whole packet count");
    check(transmitter.rejected() == 0U, "full ring is not a validation rejection");
}

void test_backpressure_accounting() {
    can::test::FakeCanDriver driver{};
    driver.submit_status = std::unexpected(nexigen::core::ErrorCode::SystemError);
    const std::vector<can::CanChannelConfig> configs{channel("can0", 1)};
    can::CanTransmitter transmitter{driver, configs};
    check(transmitter.start().has_value(), "backpressure transmitter starts");
    const can::CanMessageBatch packet = make_packet(1U, 50U);
    check(transmitter.enqueue_batch("can0", packet).has_value(), "backpressure packet admitted");
    check(wait_for([&] { return transmitter.failed() == 50U; }), "packet failure counted");
    transmitter.stop();
    check(transmitter.submitted() == 0U, "failed packet is not submitted");
    check(transmitter.failed() == 50U, "failure counted per message");
    check(transmitter.dropped() == 0U, "driver failure is not a queue drop");
}

void test_single_message_compat() {
    can::test::FakeCanDriver driver{};
    const std::vector<can::CanChannelConfig> configs{channel("can0", 1)};
    can::CanTransmitter transmitter{driver, configs};
    check(transmitter.start().has_value(), "single transmitter starts");
    can::CanMessage message = make_message(1U, 0x123U, 0U);
    message.origin = can::CanMessageOrigin::Unknown;
    message.timestamp_source = can::CanTimestampSource::Hardware;
    check(transmitter.enqueue("can0", message).has_value(), "single message enqueued");
    check(wait_for([&] { return transmitter.submitted() == 1U; }), "single message submitted");
    transmitter.stop();
    check(!driver.submitted.empty(), "single message reached the driver");
    if (!driver.submitted.empty()) {
        check(driver.submitted[0].hardware_timestamp_ns != 0U, "zero timestamp filled");
        check(driver.submitted[0].origin == can::CanMessageOrigin::LocalTransmit, "origin set");
    }
}
} // namespace

int main() {
    test_packet_admission();
    test_packet_rejection();
    test_full_queue_accounting();
    test_backpressure_accounting();
    test_single_message_compat();

    can::test::FakeCanDriver driver{};
    driver.fail_open_at = 1;
    const std::vector<can::CanChannelConfig> configs{channel("can0", 1), channel("can1", 2)};
    can::CanTransmitter transmitter{driver, configs};
    const auto started = transmitter.start();
    check(!started.has_value(), "second open fails");
    check(driver.opened.empty(), "first channel rolled back");

    can::test::FakeCanDriver receiver_driver{};
    receiver_driver.fail_open_at = 1;
    std::array<nexigen::com::EventProducer<can::CanMessageBatch>*, 2> producers{nullptr, nullptr};
    can::CanReceiver receiver{receiver_driver, configs, producers};
    check(!receiver.start().has_value(), "receiver second open fails");
    check(receiver_driver.opened.empty(), "receiver first channel rolled back");

    const std::vector<can::CanChannelConfig> one{channel("can0", 1)};
    driver.fail_open_at = -1;
    driver.open_attempts = 0;
    can::CanTransmitter retry{driver, one};
    check(retry.start().has_value(), "retry transmitter starts");
    can::CanMessage message = make_message(1U, 0x123U, 0U);
    check(retry.enqueue("can0", message).has_value(), "retry enqueue");
    check(wait_for([&] { return retry.submitted() == 1U; }), "bounded retry delivered");
    retry.stop();
    check(!driver.submitted.empty(), "submitted");
    check(driver.opened.empty(), "stop closes channel");

    std::printf("hw can bridge: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
