// Unit tests for the COM-free nexigen::hw::can vocabulary and message model.
#include <array>
#include <cstdio>
#include <cstdint>
#include <string>
#include <type_traits>

#include <nexigen/hw/can/can_channel_config.hpp>
#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_driver_capabilities.hpp>
#include <nexigen/hw/can/can_error_flags.hpp>
#include <nexigen/hw/can/can_frame_flags.hpp>
#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/hw/can/can_monitor_filter.hpp>
#include <nexigen/hw/can/can_channel_set.hpp>
#include <nexigen/hw/can/can_channel_tracker.hpp>
#include <nexigen/hw/can/can_service_names.hpp>
#include <nexigen/hw/can/detail/can_message_ring.hpp>

#include "fake_can_driver.hpp"

namespace can = nexigen::hw::can;
namespace core = nexigen::core;

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

void test_vocabulary() {
    can::CanMessageKind kind = can::CanMessageKind::ClassicData;
    check(can::to_string(kind) == "ClassicData", "kind name");
    check(can::from_string("FdData", kind) && kind == can::CanMessageKind::FdData, "kind parse");
    nlohmann::json json = kind;
    can::CanMessageKind parsed = can::CanMessageKind::ClassicData;
    can::from_json(json, parsed);
    check(parsed == kind, "kind json");
    check(std::format("{}", kind) == "FdData", "kind format");

    can::CanChannelState state = can::CanChannelState::BusOff;
    check(can::to_string(state) == "BusOff", "state name");
    check(std::format("{}", can::CanTimestampSource::Simulated) == "Simulated", "source format");
    check(can::to_string(can::CanMessageOrigin::LocalTransmit) == "LocalTransmit", "origin name");
}

void test_flags() {
    can::CanErrorFlags errors{};
    errors.bits = can::CanErrorFlags::bit_error | can::CanErrorFlags::crc_error;
    check(errors.any(), "errors any");
    check(errors.to_string() == "bit_error,crc_error", "errors to_string");

    can::CanFrameFlags flags{};
    flags.bits = can::CanFrameFlags::bit_rate_switch | can::CanFrameFlags::has_hardware_timestamp;
    check(flags.to_string() == "brs,hardware_timestamp", "frame flags to_string");
}

void test_message() {
    static_assert(sizeof(can::CanMessage) == 112U, "CanMessage must stay 112 bytes");
    static_assert(std::is_trivially_copyable_v<can::CanMessage>);

    can::CanMessage message{};
    message.kind = can::CanMessageKind::ClassicData;
    message.channel_id = 1;
    message.can_id = 0x123;
    message.dlc = 2;
    message.hardware_timestamp_ns = 42;
    message.hardware_sequence = 7;
    message.origin = can::CanMessageOrigin::ExternalDevice;
    message.data[0] = 0xAA;
    message.data[1] = 0x55;
    check(message.validate().has_value(), "classic message valid");

    nlohmann::json json;
    can::to_json(json, message);
    can::CanMessage restored{};
    can::from_json(json, restored);
    check(restored.kind == message.kind, "message kind json");
    check(restored.hardware_timestamp_ns == 42U, "message timestamp json");
    check(restored.hardware_sequence == 7U, "message sequence json");
    check(restored.data[0] == 0xAAU && restored.data[1] == 0x55U, "message data json");

    can::CanMessage fd{};
    fd.kind = can::CanMessageKind::FdData;
    fd.dlc = 64;
    check(fd.validate().has_value(), "fd message valid");
    fd.dlc = 65;
    check(!fd.validate().has_value(), "fd oversize rejected");

    can::CanMessage remote{};
    remote.kind = can::CanMessageKind::Remote;
    remote.dlc = 0;
    check(remote.validate().has_value(), "remote valid");
    remote.dlc = 1;
    check(!remote.validate().has_value(), "remote payload rejected");

    can::CanMessage idle{};
    idle.kind = can::CanMessageKind::BusIdle;
    idle.idle_duration_ns = 1'000'000'000ULL;
    check(idle.validate().has_value(), "heartbeat valid");
    idle.can_id = 1;
    check(!idle.validate().has_value(), "heartbeat id rejected");
}

void test_config_and_services() {
    can::CanChannelConfig config{};
    config.name = "can0";
    config.channel_id = 1;
    config.driver_channel = "can0";
    config.bit_rate = 500'000;
    check(config.validate().has_value(), "config valid");

    config.name = "CAN0";
    check(!config.validate().has_value(), "uppercase name rejected");
    config.name = "can0";
    config.rx_ring = 0;
    check(!config.validate().has_value(), "zero ring rejected");
    config.rx_ring = 4096;

    auto service = can::rx_service("can0");
    check(service.has_value() && *service == "nexigen/hw/can/can0/rx", "rx service");
    auto status = can::status_service("bad/name");
    check(!status.has_value(), "bad channel service rejected");

    can::CanChannelConfig second = config;
    second.name = "can1";
    second.channel_id = 2;
    const can::CanChannelConfig list[] = {config, second};
    check(can::validate_channels(list).has_value(), "unique channel list");
    const can::CanChannelConfig duplicate[] = {config, config};
    check(!can::validate_channels(duplicate).has_value(), "duplicate channel rejected");

    std::array<can::CanChannelConfig, 17> many{};
    for (std::size_t i = 0; i < many.size(); ++i) {
        many[i] = config;
        many[i].name = "ch" + std::to_string(i);
        many[i].channel_id = static_cast<std::uint16_t>(i + 1U);
    }
    check(!can::validate_channels(many).has_value(), "seventeen channels rejected");
}

void test_status() {
    can::CanChannelStatus status{};
    status.state = can::CanChannelState::BusOff;
    status.bus_off_count = 2;
    status.rx_count = 10;
    status.last_error_flags.bits = can::CanErrorFlags::bus_off;
    nlohmann::json json;
    can::to_json(json, status);
    can::CanChannelStatus restored{};
    can::from_json(json, restored);
    check(restored.state == can::CanChannelState::BusOff, "status state json");
    check(restored.bus_off_count == 2U, "status counter json");

    can::CanChannelTracker tracker{};
    can::CanMessage first{};
    first.kind = can::CanMessageKind::ClassicData;
    first.frame_flags.bits = can::CanFrameFlags::has_hardware_sequence;
    first.hardware_sequence = 1;
    tracker.observe(first);
    first.hardware_sequence = 3;
    tracker.observe(first);
    check(tracker.status().rx_count == 2U, "tracker rx count");
    check(tracker.status().sequence_gap_count == 1U, "tracker gap");
    can::CanMessage error{};
    error.kind = can::CanMessageKind::Error;
    error.error_flags.bits = can::CanErrorFlags::bus_off;
    tracker.observe(error);
    check(tracker.status().state == can::CanChannelState::BusOff, "tracker bus-off");
    check(tracker.status().bus_off_count == 1U, "tracker bus-off count");
    error.error_flags.bits = can::CanErrorFlags::error_warning;
    tracker.observe(error);
    check(tracker.status().state == can::CanChannelState::Warning, "tracker warning");
    error.error_flags.bits = can::CanErrorFlags::error_passive;
    tracker.observe(error);
    check(tracker.status().state == can::CanChannelState::ErrorPassive, "tracker passive");
    error.error_flags.bits = 0;
    tracker.observe(error);
    check(tracker.status().state == can::CanChannelState::Active, "tracker recovery");
}

class RecordingSink final : public can::ICanMessageSink {
public:
    can::CanMessage last{};
    int count{0};
    void on_message(const can::CanMessage& message) noexcept override {
        last = message;
        ++count;
    }
};

void test_monitor_filter() {
    can::CanMessage message{};
    message.channel_id = 1;
    message.can_id = 0x123;
    message.kind = can::CanMessageKind::ClassicData;
    message.origin = can::CanMessageOrigin::ExternalDevice;
    can::CanMonitorFilter filter{};
    check(can::matches(filter, message, "can0"), "filter default");
    filter.channel = "can1";
    check(!can::matches(filter, message, "can0"), "filter channel");
    filter.channel.clear();
    filter.has_id = true;
    filter.id = 0x123;
    check(can::matches(filter, message, "can0"), "filter id match");
    filter.id = 0x124;
    check(!can::matches(filter, message, "can0"), "filter id miss");
    filter.has_id = false;
    filter.has_kind = true;
    filter.kind = can::CanMessageKind::Error;
    check(!can::matches(filter, message, "can0"), "filter kind");
    filter.has_kind = false;
    filter.has_origin = true;
    filter.origin = can::CanMessageOrigin::ExternalDevice;
    check(can::matches(filter, message, "can0"), "filter origin match");
    filter.origin = can::CanMessageOrigin::LocalTransmit;
    check(!can::matches(filter, message, "can0"), "filter origin miss");
    filter.has_origin = false;
    filter.errors_only = true;
    check(!can::matches(filter, message, "can0"), "filter errors only");
    message.kind = can::CanMessageKind::Error;
    check(can::matches(filter, message, "can0"), "filter errors only match");
}

void test_ring() {
    nexigen::hw::can::detail::CanMessageRing<4> ring{};
    can::CanMessage message{};
    message.can_id = 1;
    check(ring.try_push(message), "ring push");
    message.can_id = 2;
    check(ring.try_push(message), "ring push 2");
    can::CanMessage out{};
    check(ring.try_pop(out) && out.can_id == 1, "ring pop fifo");
    check(ring.try_pop(out) && out.can_id == 2, "ring pop 2");
    check(!ring.try_pop(out), "ring empty");
    for (int i = 0; i < 3; ++i) {
        check(ring.try_push(message), "ring fill");
    }
    check(!ring.try_push(message), "ring full");
}

void test_driver_contract() {
    can::test::FakeCanDriver driver{};
    driver.caps.supports_fd = false;
    driver.caps.supports_remote = false;
    driver.caps.hardware_timestamps = true;
    driver.caps.hardware_sequence = true;
    driver.caps.error_frames = true;
    driver.caps.heartbeats = true;

    auto capabilities = driver.capabilities();
    check(capabilities.has_value() && capabilities->hardware_timestamps, "driver capabilities");

    can::CanChannelConfig config{};
    config.name = "can0";
    config.channel_id = 1;
    config.driver_channel = "can0";
    config.bit_rate = 500'000;
    RecordingSink sink{};
    check(driver.open_channel(config, sink).has_value(), "driver open");
    check(driver.submit("can1", can::CanMessage{}).error() == core::ErrorCode::NotFound,
          "unknown channel");

    can::CanMessage fd{};
    fd.kind = can::CanMessageKind::FdData;
    fd.dlc = 64;
    check(driver.submit("can0", fd).error() == core::ErrorCode::UnsupportedFormat, "fd gated");

    can::CanMessage remote{};
    remote.kind = can::CanMessageKind::Remote;
    check(driver.submit("can0", remote).error() == core::ErrorCode::UnsupportedFormat,
          "remote gated");

    can::CanMessage data{};
    data.kind = can::CanMessageKind::ClassicData;
    data.hardware_timestamp_ns = 123;
    data.hardware_sequence = 9;
    data.error_flags.bits = can::CanErrorFlags::crc_error;
    data.origin = can::CanMessageOrigin::ExternalDevice;
    check(driver.submit("can0", data).has_value(), "classic submit");
    check(driver.submitted.size() == 1U, "submit recorded");

    driver.emit(data);
    check(sink.count == 1 && sink.last.hardware_timestamp_ns == 123U,
          "driver sink preserves timestamp");
    check(sink.last.hardware_sequence == 9U, "driver sink preserves sequence");
    check(sink.last.error_flags.bits == can::CanErrorFlags::crc_error,
          "driver sink preserves errors");
    data.kind = can::CanMessageKind::BusIdle;
    data.dlc = 0;
    data.hardware_timestamp_ns = 77;
    data.idle_duration_ns = 5000;
    driver.emit(data);
    check(sink.last.kind == can::CanMessageKind::BusIdle && sink.last.idle_duration_ns == 5000U,
          "driver sink preserves heartbeat");

    check(driver.close_channel("can0").has_value(), "driver close");
    check(!driver.close_channel("can0").has_value(), "driver close twice");
}

} // namespace

int main() {
    test_vocabulary();
    test_flags();
    test_message();
    test_config_and_services();
    test_status();
    test_monitor_filter();
    test_ring();
    test_driver_contract();
    std::printf("hw can core: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
