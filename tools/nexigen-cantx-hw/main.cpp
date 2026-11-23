// Hardware transmitter skeleton: iceoryx2 -> vendor driver.
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
#include <nexigen/hw/can/com/can_transmitter.hpp>
#include <nexigen/hw/can/com/vendor_can_driver.hpp>

#include "detail/can_tool_common.hpp"

namespace app = nexigen::app;
namespace can = nexigen::hw::can;

using Runtime = app::App<app::policies::ConsolePolicy>;

int main(int argc, char** argv) {
    auto parsed = can::parse_tool_options(argc, argv);
    if (!parsed || parsed->help) {
        std::fputs(std::string(can::can_tool_usage()).c_str(), stdout);
        return parsed ? 0 : 1;
    }
    std::vector<const char*> app_argv{"nexigen-cantx-hw", "--app-id", parsed->app_id.c_str()};
    auto created = Runtime::create(
        static_cast<int>(app_argv.size()), app_argv.data(), "nexigen-cantx-hw",
        parsed->embedded ? app::TimeBaseMode::Embedded : app::TimeBaseMode::External);
    if (!created) {
        return 1;
    }
    if (!*created) {
        return 0;
    }
    auto runtime = std::move(*created);
    auto status_producers = nexigen::tools::can::make_status_producers(*runtime, parsed->channels);
    if (!status_producers) {
        return 1;
    }
    can::VendorCanDriver driver{};
    can::CanTransmitter transmitter{driver, parsed->channels, *status_producers};
    if (!nexigen::tools::can::subscribe_tx(*runtime, parsed->channels, transmitter)) {
        return 1;
    }
    if (auto started = transmitter.start(); !started) {
        std::fprintf(stderr, "cantx-hw: %s (fill VendorCanDriver)\n",
                     std::string(nexigen::core::to_string(started.error())).c_str());
        return 1;
    }
    const auto reason = runtime->run();
    transmitter.stop();
    const auto status = runtime->shutdown();
    return reason && status ? app::exit_code(*reason) : 1;
}
