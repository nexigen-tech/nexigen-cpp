#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <nexigen/app/app.hpp>
#include <nexigen/app/policies/console_policy.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/hw/can/can_service_names.hpp>
#include <nexigen/hw/can/can_tool_options.hpp>
#include <nexigen/hw/can/com/can_transport.hpp>

namespace app = nexigen::app;
namespace can = nexigen::hw::can;

namespace {

using Policy = app::policies::ConsolePolicy;
using Runtime = app::App<Policy>;

struct VirtualChannel {
    std::string name;
    std::uint16_t channel_id{0};
    std::uint64_t heartbeat_interval_ns{1'000'000'000ULL};
    nexigen::com::EventProducer<can::CanMessage>* rx{nullptr};
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<std::uint64_t> last_activity_ns{0};
    std::uint64_t last_heartbeat_ns{0};
    std::optional<can::CanMessage> reorder_pending{};
};

using ChannelList = std::vector<std::unique_ptr<VirtualChannel>>;

std::uint64_t now_ns() {
    const auto now = nexigen::chrono::Timestamp::now();
    return now ? now->nanoseconds() : 0U;
}

void publish(VirtualChannel& channel, can::CanMessage message) {
    message.channel_id = channel.channel_id;
    message.timestamp_source = can::CanTimestampSource::Simulated;
    message.hardware_timestamp_ns = now_ns();
    message.hardware_sequence = channel.sequence.fetch_add(1U) + 1U;
    message.frame_flags.bits |=
        can::CanFrameFlags::has_hardware_timestamp | can::CanFrameFlags::has_hardware_sequence;
    if (message.kind != can::CanMessageKind::BusIdle) {
        channel.last_activity_ns.store(message.hardware_timestamp_ns);
    }
    static_cast<void>(channel.rx->publish(message, can::event_code_for(message.kind),
                                          can::event_time(message),
                                          can::hardware_metadata(message)));
}

void publish_faults(VirtualChannel& channel, const can::CanToolOptions& options) {
    const auto add = [&](std::uint32_t flags) {
        can::CanMessage error{};
        error.kind = can::CanMessageKind::Error;
        error.error_flags.bits = flags;
        error.origin = can::CanMessageOrigin::LocalTransmit;
        publish(channel, error);
    };
    if (options.inject_errors) {
        add(can::CanErrorFlags::bit_error);
    }
    if (options.inject_overload) {
        can::CanMessage overload{};
        overload.kind = can::CanMessageKind::Overload;
        overload.origin = can::CanMessageOrigin::LocalTransmit;
        publish(channel, overload);
    }
    if (options.inject_bus_off) {
        add(can::CanErrorFlags::bus_off);
    }
    if (options.inject_warning) {
        add(can::CanErrorFlags::error_warning);
    }
    if (options.inject_passive) {
        add(can::CanErrorFlags::error_passive);
    }
}

void loopback(VirtualChannel& channel, const can::CanToolOptions& options, std::mt19937_64& rng,
              const can::CanMessage& message) {
    if (options.latency_ns != 0U || options.jitter_ns != 0U) {
        const std::uint64_t jitter = options.jitter_ns == 0U ? 0U : rng() % options.jitter_ns;
        std::this_thread::sleep_for(std::chrono::nanoseconds{options.latency_ns + jitter});
    }
    if (options.inject_drop && (rng() % 10U) == 0U) {
        return;
    }
    publish_faults(channel, options);
    can::CanMessage echo = message;
    echo.origin = can::CanMessageOrigin::LocalTransmit;
    if (options.inject_reorder) {
        if (!channel.reorder_pending.has_value()) {
            channel.reorder_pending = echo;
            return;
        }
        publish(channel, echo);
        publish(channel, *channel.reorder_pending);
        channel.reorder_pending.reset();
        return;
    }
    if (options.inject_burst) {
        publish(channel, echo);
        publish(channel, echo);
        publish(channel, echo);
        return;
    }
    publish(channel, echo);
    if (options.inject_duplicate && (rng() % 10U) == 0U) {
        publish(channel, echo);
    }
}

bool setup_channels(Runtime& runtime, const can::CanToolOptions& options, ChannelList& channels,
                    std::mt19937_64& rng) {
    for (const auto& config : options.channels) {
        auto vbus_rx = can::vbus_rx_service(config.name);
        auto vbus_tx = can::vbus_tx_service(config.name);
        if (!vbus_rx || !vbus_tx) {
            return false;
        }
        auto channel = std::make_unique<VirtualChannel>();
        channel->name = config.name;
        channel->channel_id = config.channel_id;
        channel->heartbeat_interval_ns = config.heartbeat_interval_ns;
        channel->last_activity_ns.store(now_ns());
        channel->rx = runtime.publish<can::CanMessage>(*vbus_rx);
        if (channel->rx == nullptr) {
            return false;
        }
        auto* raw = channel.get();
        const bool attached = runtime.subscribe<can::CanMessage>(
            *vbus_tx, [raw, &options, &rng](const can::CanMessage& message) {
                loopback(*raw, options, rng, message);
            });
        if (!attached) {
            return false;
        }
        channels.push_back(std::move(channel));
    }
    return true;
}

void heartbeat_loop(ChannelList& channels, const can::CanToolOptions& options,
                    std::stop_token token) {
    std::uint64_t last_external_ns = 0;
    while (!token.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        const std::uint64_t now = now_ns();
        for (auto& channel : channels) {
            const std::uint64_t last = channel->last_activity_ns.load();
            if (channel->heartbeat_interval_ns != 0U &&
                now - last >= channel->heartbeat_interval_ns &&
                now - channel->last_heartbeat_ns >= channel->heartbeat_interval_ns) {
                can::CanMessage idle{};
                idle.kind = can::CanMessageKind::BusIdle;
                idle.origin = can::CanMessageOrigin::ExternalDevice;
                idle.idle_duration_ns = now - last;
                publish(*channel, idle);
                channel->last_heartbeat_ns = now;
            }
            if (options.external && now - last_external_ns >= 500'000'000ULL) {
                can::CanMessage external{};
                external.kind = can::CanMessageKind::ClassicData;
                external.can_id = 0x200U;
                external.dlc = 2;
                external.data[0] = 0xE0;
                external.data[1] = 0x01;
                external.origin = can::CanMessageOrigin::ExternalDevice;
                publish(*channel, external);
            }
        }
        if (options.external) {
            last_external_ns = now;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    auto parsed = can::parse_tool_options(argc, argv);
    if (!parsed) {
        std::fprintf(stderr, "%s", std::string(can::can_tool_usage()).c_str());
        return 1;
    }
    if (parsed->help) {
        std::fputs(std::string(can::can_tool_usage()).c_str(), stdout);
        return 0;
    }
    std::vector<const char*> app_argv{"nexigen-canvbus", "--app-id", parsed->app_id.c_str()};
    if (!parsed->app_name.empty()) {
        app_argv.push_back("--app-name");
        app_argv.push_back(parsed->app_name.c_str());
    }
    auto created = Runtime::create(
        static_cast<int>(app_argv.size()), app_argv.data(), "nexigen-canvbus",
        parsed->embedded ? app::TimeBaseMode::Embedded : app::TimeBaseMode::External);
    if (!created) {
        return 1;
    }
    if (!*created) {
        return 0;
    }
    auto runtime = std::move(*created);
    std::mt19937_64 rng{parsed->seed};
    ChannelList channels;
    if (!setup_channels(*runtime, *parsed, channels, rng)) {
        return 1;
    }
    std::jthread heartbeat{
        [&channels, &parsed](std::stop_token token) { heartbeat_loop(channels, *parsed, token); }};
    std::jthread duration{[&parsed](std::stop_token token) {
        if (parsed->duration_ms == 0U) {
            return;
        }
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds{parsed->duration_ms};
        while (!token.stop_requested() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        if (!token.stop_requested()) {
            static_cast<void>(::kill(::getpid(), SIGTERM));
        }
    }};
    const auto reason = runtime->run();
    heartbeat.request_stop();
    duration.request_stop();
    heartbeat.join();
    duration.join();
    const auto status = runtime->shutdown();
    return reason && status ? app::exit_code(*reason) : 1;
}
