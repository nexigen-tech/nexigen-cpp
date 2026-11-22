// Test-only sender for examples/app/ex_app_quickstart.cpp.
#include <chrono>
#include <cstdint>
#include <thread>

#include <nexigen/com/event_channel.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>

#include "../../examples/app/quickstart_signal.hpp"

namespace com = nexigen::com;
using Signal = nexigen::examples::quickstart::Signal;

int main() {
    auto node = com::Node::create();
    auto name = com::ServiceName::create("Nexigen/Example/Quickstart/In");
    if (!node || !name) {
        return 1;
    }
    com::ChannelConfig config{};
    auto channel = com::EventChannel<Signal>::create(*node, *name, config);
    if (!channel) {
        return 1;
    }
    auto producer = channel->producer("QSND");
    if (!producer) {
        return 1;
    }
    for (std::uint64_t sequence = 0; sequence < 3U; ++sequence) {
        static_cast<void>(producer->publish_now(
            Signal{sequence, 10.0 + static_cast<double>(sequence)}, 0x1001U));
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    return 0;
}
