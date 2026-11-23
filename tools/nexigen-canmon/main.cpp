#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/app/app.hpp>
#include <nexigen/app/policies/console_policy.hpp>
#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/hw/can/can_monitor_filter.hpp>
#include <nexigen/hw/can/com/can_client.hpp>
#include <nexigen/hw/can/can_service_names.hpp>
#include <nexigen/hw/can/can_tool_options.hpp>
#include <nexigen/hw/can/detail/can_message_ring.hpp>

namespace app = nexigen::app;
namespace can = nexigen::hw::can;

namespace {

using Policy = app::policies::ConsolePolicy;
using Runtime = can::CanClient<Policy>;
using MessageRing = can::detail::SpscRing<can::CanMessage, 8192>;
using StatusRing = can::detail::SpscRing<can::CanChannelStatus, 1024>;

using MonitorFilter = can::CanMonitorFilter;

std::string channel_name(std::span<const can::CanChannelConfig> channels, std::uint16_t id) {
    for (const auto& channel : channels) {
        if (channel.channel_id == id) {
            return channel.name;
        }
    }
    return "?";
}

void print_message(const MonitorFilter& filter, const can::CanMessage& message,
                   const std::string& channel) {
    if (filter.json) {
        nlohmann::json json;
        can::to_json(json, message);
        std::printf("%s\n", json.dump().c_str());
        return;
    }
    std::printf("[%s] %s\n", channel.c_str(), message.to_string().c_str());
}

void print_status(const MonitorFilter& filter, const can::CanChannelStatus& status,
                  const std::string& channel) {
    if (filter.json) {
        nlohmann::json json;
        can::to_json(json, status);
        std::printf("%s\n", json.dump().c_str());
        return;
    }
    std::printf("[%s] %s\n", channel.c_str(), status.to_string().c_str());
}

bool setup_filter(const can::CanToolOptions& options, MonitorFilter& filter) {
    filter.channel = options.filter_channel;
    filter.id = options.filter_id;
    filter.has_id = options.has_filter_id;
    filter.errors_only = options.errors_only;
    filter.json = options.json;
    if (!options.filter_kind.empty()) {
        if (!can::from_string(options.filter_kind, filter.kind)) {
            std::fprintf(stderr, "canmon: invalid --filter-kind\n");
            return false;
        }
        filter.has_kind = true;
    }
    if (!options.filter_origin.empty()) {
        if (!can::from_string(options.filter_origin, filter.origin)) {
            std::fprintf(stderr, "canmon: invalid --filter-origin\n");
            return false;
        }
        filter.has_origin = true;
    }
    return true;
}

bool subscribe_channels(Runtime& runtime, std::span<const can::CanChannelConfig> channels,
                        MessageRing& messages, StatusRing& statuses,
                        std::atomic<std::uint64_t>& dropped) {
    for (const auto& channel : channels) {
        const bool rx_ok = runtime.subscribe_with_loss(
            channel.name,
            [&messages, &dropped](const can::CanMessage& message) {
                if (!messages.try_push(message)) {
                    dropped.fetch_add(1U, std::memory_order_relaxed);
                }
            },
            [](const nexigen::com::LossInfo&) {});
        const bool status_ok = runtime.subscribe_status(
            channel.name, [&statuses, &dropped](const can::CanChannelStatus& value) {
                if (!statuses.try_push(value)) {
                    dropped.fetch_add(1U, std::memory_order_relaxed);
                }
            });
        if (!rx_ok || !status_ok) {
            return false;
        }
    }
    return true;
}

void render_loop(MessageRing& messages, StatusRing& statuses, const MonitorFilter& filter,
                 std::span<const can::CanChannelConfig> channels,
                 std::atomic<std::uint64_t>& dropped, std::stop_token token) {
    can::CanMessage message{};
    can::CanChannelStatus status{};
    while (!token.stop_requested() || messages.size() > 0U || statuses.size() > 0U) {
        while (messages.try_pop(message)) {
            const std::string name = channel_name(channels, message.channel_id);
            if (matches(filter, message, name)) {
                print_message(filter, message, name);
            }
        }
        while (statuses.try_pop(status)) {
            print_status(filter, status, "status");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (dropped.load() > 0U) {
        std::fprintf(stderr, "canmon: display dropped=%llu\n",
                     static_cast<unsigned long long>(dropped.load()));
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
    MonitorFilter filter{};
    if (!setup_filter(*parsed, filter)) {
        return 1;
    }
    std::vector<const char*> app_argv{"nexigen-canmon", "--app-id", parsed->app_id.c_str()};
    if (!parsed->app_name.empty()) {
        app_argv.push_back("--app-name");
        app_argv.push_back(parsed->app_name.c_str());
    }
    auto created = Runtime::create(
        static_cast<int>(app_argv.size()), app_argv.data(), "nexigen-canmon", parsed->channels,
        parsed->embedded ? app::TimeBaseMode::Embedded : app::TimeBaseMode::External);
    if (!created) {
        return 1;
    }
    if (!*created) {
        return 0;
    }
    auto runtime = std::move(*created);
    MessageRing messages{};
    StatusRing statuses{};
    std::atomic<std::uint64_t> dropped{0};
    if (!subscribe_channels(*runtime, parsed->channels, messages, statuses, dropped)) {
        return 1;
    }
    std::jthread renderer{[&](std::stop_token token) {
        render_loop(messages, statuses, filter, parsed->channels, dropped, token);
    }};
    const auto reason = runtime->run();
    renderer.request_stop();
    renderer.join();
    std::uint64_t transport_losses = 0;
    for (const auto& channel : parsed->channels) {
        if (const auto stats = runtime->stats(channel.name); stats.has_value()) {
            transport_losses += stats->losses;
        }
    }
    if (transport_losses > 0U) {
        std::fprintf(stderr, "canmon: transport losses=%llu\n",
                     static_cast<unsigned long long>(transport_losses));
    }
    const auto status = runtime->shutdown();
    return reason && status ? app::exit_code(*reason) : 1;
}
