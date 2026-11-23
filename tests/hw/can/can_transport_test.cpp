// COM-gated transport mapping tests for nexigen::hw::can.
#include <cstdio>
#include <cstdint>

#include <nexigen/com/event_header.hpp>
#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/hw/can/com/can_transport.hpp>

namespace can = nexigen::hw::can;
namespace com = nexigen::com;

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
} // namespace

int main() {
    can::CanMessage message{};
    message.kind = can::CanMessageKind::ClassicData;
    message.hardware_timestamp_ns = 1234;
    message.hardware_sequence = 17;
    message.frame_flags.bits =
        can::CanFrameFlags::has_hardware_timestamp | can::CanFrameFlags::has_hardware_sequence;

    const com::EventTime time = can::event_time(message);
    check(time.ns == 1234U, "event time ns");
    check(time.domain == com::TimestampDomain::Hardware, "event time domain");

    const com::HardwareMetadata metadata = can::hardware_metadata(message);
    check(metadata.has_sequence && metadata.sequence == 17U, "hardware sequence");
    check(metadata.has_ticks && metadata.native_ticks == 1234U, "native ticks");
    check(metadata.has_canonical && metadata.canonical_ns == 1234U, "canonical ns");

    check(can::event_code_for(can::CanMessageKind::ClassicData) == can::event_rx, "rx code");
    check(can::event_code_for(can::CanMessageKind::Error) == can::event_error, "error code");
    check(can::event_code_for(can::CanMessageKind::BusIdle) == can::event_bus_idle, "idle code");

    std::printf("hw can transport: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
