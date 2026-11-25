// COM-gated bridge tests: driver-open rollback and transmitter normalization.
#include "fake_can_driver.hpp"
#include <chrono>
#include <array>
#include <cstdio>
#include <thread>
#include <vector>

#include <nexigen/hw/can/can_channel_config.hpp>
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
} // namespace

void test_batch_transmit() {
    can::test::FakeCanDriver driver{};
    const std::vector<can::CanChannelConfig> configs{channel("can0", 1)};
    can::CanTransmitter transmitter{driver, configs};
    check(transmitter.start().has_value(), "batch transmitter starts");
    can::CanMessage message{};
    message.kind = can::CanMessageKind::ClassicData;
    message.channel_id = 1U;
    message.can_id = 0x321U;
    message.dlc = 1U;
    for (std::size_t index = 0; index < can::CanMessageBatch::max_messages; ++index) {
        message.hardware_timestamp_ns = 1000U + index;
        check(transmitter.enqueue("can0", message).has_value(), "batch enqueue");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{40});
    transmitter.stop();
    check(driver.submitted.size() == can::CanMessageBatch::max_messages,
          "batch fallback preserves count");
    if (!driver.submitted.empty()) {
        check(driver.submitted.front().hardware_timestamp_ns == 1000U, "batch first timestamp");
        check(driver.submitted.back().hardware_timestamp_ns == 1099U, "batch last timestamp");
    }
}

int main() {
    test_batch_transmit();

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

    driver.fail_open_at = -1;
    driver.open_attempts = 0;
    const std::vector<can::CanChannelConfig> one{channel("can0", 1)};
    can::CanTransmitter single{driver, one};
    check(single.start().has_value(), "single channel starts");
    can::CanMessage message{};
    message.kind = can::CanMessageKind::ClassicData;
    message.channel_id = 1;
    message.can_id = 0x123;
    message.dlc = 1;
    check(single.enqueue("can0", message).has_value(), "enqueue");
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    single.stop();
    check(!driver.submitted.empty(), "submitted");

    driver.submitted.clear();
    driver.fail_submits = 3;
    can::CanTransmitter retry{driver, one};
    check(retry.start().has_value(), "retry transmitter starts");
    check(retry.enqueue("can0", message).has_value(), "retry enqueue");
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    retry.stop();
    check(!driver.submitted.empty(), "bounded retry delivered");
    if (!driver.submitted.empty()) {
        check(driver.submitted[0].hardware_timestamp_ns != 0U, "zero timestamp filled");
        check(driver.submitted[0].origin == can::CanMessageOrigin::LocalTransmit, "origin set");
    }
    check(driver.opened.empty(), "stop closes channel");

    std::printf("hw can bridge: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
