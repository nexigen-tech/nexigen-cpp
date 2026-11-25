#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <nexigen/app/app.hpp>
#include <nexigen/app/policies/console_policy.hpp>
#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_service_names.hpp>
#include <nexigen/hw/can/can_tool_options.hpp>
#include <nexigen/hw/can/com/can_receiver.hpp>
#include <nexigen/hw/can/com/virtual_can_driver.hpp>

namespace app = nexigen::app;
namespace can = nexigen::hw::can;

namespace {

using Policy = app::policies::ConsolePolicy;
using Runtime = app::App<Policy>;

nexigen::core::Result<std::vector<nexigen::com::EventProducer<can::CanMessageBatch>*>>
make_producers(Runtime& runtime, std::span<const can::CanChannelConfig> channels) {
    std::vector<nexigen::com::EventProducer<can::CanMessageBatch>*> producers;
    producers.reserve(channels.size());
    for (const auto& channel : channels) {
        auto service = can::rx_service(channel.name);
        if (!service) {
            return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
        }
        auto* producer =
            runtime.publish<can::CanMessageBatch>(*service, can::batch_channel_config());
        if (producer == nullptr) {
            return std::unexpected(nexigen::core::ErrorCode::SystemError);
        }
        producers.push_back(producer);
    }
    return producers;
}

nexigen::core::Result<std::vector<nexigen::com::EventProducer<can::CanChannelStatus>*>>
make_status_producers(Runtime& runtime, std::span<const can::CanChannelConfig> channels) {
    std::vector<nexigen::com::EventProducer<can::CanChannelStatus>*> producers;
    producers.reserve(channels.size());
    for (const auto& channel : channels) {
        auto service = can::status_service(channel.name);
        if (!service) {
            return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
        }
        nexigen::com::ChannelConfig status_config{};
        status_config.max_publishers = 2U;
        auto* producer = runtime.publish<can::CanChannelStatus>(*service, status_config);
        if (producer == nullptr) {
            return std::unexpected(nexigen::core::ErrorCode::SystemError);
        }
        producers.push_back(producer);
    }
    return producers;
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
    if (parsed->backend != "virtual") {
        std::fprintf(stderr, "canrx: hardware backend is not linked in this build\n");
        return 1;
    }
    std::vector<const char*> app_argv{"nexigen-canrx", "--app-id", parsed->app_id.c_str()};
    if (!parsed->app_name.empty()) {
        app_argv.push_back("--app-name");
        app_argv.push_back(parsed->app_name.c_str());
    }
    auto created = Runtime::create(
        static_cast<int>(app_argv.size()), app_argv.data(), "nexigen-canrx",
        parsed->embedded ? app::TimeBaseMode::Embedded : app::TimeBaseMode::External);
    if (!created) {
        return 1;
    }
    if (!*created) {
        return 0;
    }
    auto runtime = std::move(*created);
    auto producers = make_producers(*runtime, parsed->channels);
    if (!producers) {
        for (const auto& diagnostic : runtime->diagnostics()) {
            std::fprintf(stderr, "%s\n", diagnostic.to_string().c_str());
        }
        return 1;
    }
    auto status_producers = make_status_producers(*runtime, parsed->channels);
    if (!status_producers) {
        return 1;
    }
    auto driver = std::make_unique<can::VirtualCanDriver>(parsed->app_id);
    if (!driver->usable()) {
        std::fprintf(stderr, "canrx: virtual driver could not start\n");
        return 1;
    }
    can::CanReceiver receiver{*driver, parsed->channels, *producers, *status_producers};
    if (auto started = receiver.start(); !started) {
        std::fprintf(stderr, "canrx: %s\n",
                     std::string(nexigen::core::to_string(started.error())).c_str());
        return 1;
    }
    const auto reason = runtime->run();
    receiver.stop();
    const auto status = runtime->shutdown();
    return reason && status ? app::exit_code(*reason) : 1;
}
