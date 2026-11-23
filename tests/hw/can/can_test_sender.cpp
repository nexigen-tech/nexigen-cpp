// Test-only control app: publishes CAN transmission requests for the e2e test.
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <nexigen/app/app.hpp>
#include <nexigen/app/policies/console_policy.hpp>
#include <nexigen/hw/can/can_service_names.hpp>
#include <nexigen/hw/can/can_tool_options.hpp>
#include <nexigen/hw/can/com/can_transport.hpp>

namespace app = nexigen::app;
namespace can = nexigen::hw::can;

int main(int argc, char** argv) {
    auto parsed = can::parse_tool_options(argc, argv);
    if (!parsed || parsed->help) {
        return 1;
    }
    std::vector<const char*> app_argv{"can-test-sender", "--app-id", parsed->app_id.c_str()};
    auto created = app::App<app::policies::ConsolePolicy>::create(
        static_cast<int>(app_argv.size()), app_argv.data(), "can-test-sender",
        parsed->embedded ? app::TimeBaseMode::Embedded : app::TimeBaseMode::External);
    if (!created || !*created) {
        return 1;
    }
    auto runtime = std::move(*created);
    for (const auto& channel : parsed->channels) {
        auto service = can::tx_service(channel.name);
        if (!service) {
            return 1;
        }
        auto* producer = runtime->publish<can::CanMessage>(*service);
        if (producer == nullptr) {
            return 1;
        }
        for (std::uint64_t i = 0; i < 10U; ++i) {
            can::CanMessage message{};
            message.kind = can::CanMessageKind::ClassicData;
            message.channel_id = channel.channel_id;
            message.can_id = 0x123U;
            message.dlc = 2;
            message.data[0] = 0xAA;
            message.data[1] = static_cast<std::uint8_t>(i);
            message.timestamp_source = can::CanTimestampSource::Simulated;
            auto now = nexigen::chrono::Timestamp::now();
            message.hardware_timestamp_ns = now ? now->nanoseconds() : 0U;
            message.frame_flags.bits = can::CanFrameFlags::has_hardware_timestamp;
            static_cast<void>(producer->publish(message, can::event_tx, can::event_time(message),
                                                can::hardware_metadata(message)));
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{300});
    return runtime->shutdown().has_value() ? 0 : 1;
}
