// Test-only control app: publishes CAN transmission requests for the e2e test.
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
    std::uint64_t count = 10U;
    if (const char* value = std::getenv("NEXIGEN_CAN_TEST_COUNT"); value != nullptr) {
        count = std::strtoull(value, nullptr, 0);
    }
    std::uint64_t period_us = 100'000U;
    if (const char* value = std::getenv("NEXIGEN_CAN_TEST_PERIOD_US"); value != nullptr) {
        period_us = std::strtoull(value, nullptr, 0);
    }
    for (const auto& channel : parsed->channels) {
        auto service = can::tx_service(channel.name);
        if (!service) {
            return 1;
        }
        auto* producer = runtime->publish<can::CanMessage>(*service);
        if (producer == nullptr) {
            return 1;
        }
        auto next = std::chrono::steady_clock::now();
        for (std::uint64_t i = 0; count == 0U || i < count; ++i) {
            can::CanMessage message{};
            message.kind = can::CanMessageKind::ClassicData;
            message.channel_id = channel.channel_id;
            message.can_id = 0x123U;
            message.dlc = 8;
            for (std::size_t byte = 0; byte < message.data.size(); ++byte) {
                message.data[byte] =
                    static_cast<std::uint8_t>((byte == 0U ? 0xAAU : byte) + (i & 0xFFU));
            }
            message.timestamp_source = can::CanTimestampSource::Simulated;
            auto now = nexigen::chrono::Timestamp::now();
            message.hardware_timestamp_ns = now ? now->nanoseconds() : 0U;
            message.frame_flags.bits = can::CanFrameFlags::has_hardware_timestamp;
            static_cast<void>(producer->publish(message, can::event_tx, can::event_time(message),
                                                can::hardware_metadata(message)));
            if (period_us != 0U) {
                next += std::chrono::microseconds{period_us};
                std::this_thread::sleep_until(next);
            }
        }
    }
    if (count != 0U) {
        std::this_thread::sleep_for(std::chrono::milliseconds{300});
    }
    return runtime->shutdown().has_value() ? 0 : 1;
}
