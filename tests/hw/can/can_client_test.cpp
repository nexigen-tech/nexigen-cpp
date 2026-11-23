// COM-gated CanClient: mixed multi-channel receive/publish and diagnostics.
#include <array>
#include <atomic>
#include <span>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>
#include <vector>

#include <nexigen/app/policies/console_policy.hpp>
#include <nexigen/com/event_channel.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_service_names.hpp>
#include <nexigen/hw/can/com/can_client.hpp>
#include <nexigen/hw/can/com/can_transport.hpp>

namespace app = nexigen::app;
namespace can = nexigen::hw::can;
namespace com = nexigen::com;

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* text) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", text);
    }
}

std::vector<const char*> args() {
    static std::vector<const char*> value{"can-client-test", "--app-id", "CLIENT"};
    return value;
}

can::CanChannelConfig channel(std::string name, std::uint16_t id) {
    can::CanChannelConfig config{};
    config.name = std::move(name);
    config.channel_id = id;
    config.driver_channel = config.name;
    config.bit_rate = 500'000;
    return config;
}

bool send_status(const can::CanChannelConfig& config) {
    auto node = com::Node::create();
    auto name = com::ServiceName::create(can::status_service(config.name)->c_str());
    if (!node || !name) {
        return false;
    }
    com::ChannelConfig channel_config{};
    auto channel = com::EventChannel<can::CanChannelStatus>::create(*node, *name, channel_config);
    if (!channel) {
        return false;
    }
    auto producer = channel->producer("RAW");
    if (!producer) {
        return false;
    }
    can::CanChannelStatus status{};
    status.state = can::CanChannelState::Active;
    const auto now = nexigen::chrono::Timestamp::now();
    status.timestamp_ns = now ? now->nanoseconds() : 0U;
    return producer
        ->publish(status, can::event_status,
                  com::EventTime{status.timestamp_ns, com::TimestampDomain::Hardware},
                  com::HardwareMetadata{})
        .has_value();
}

bool send_rx(const can::CanChannelConfig& config) {
    auto node = com::Node::create();
    auto name = com::ServiceName::create(can::rx_service(config.name)->c_str());
    if (!node || !name) {
        return false;
    }
    com::ChannelConfig channel_config{};
    auto channel = com::EventChannel<can::CanMessage>::create(*node, *name, channel_config);
    if (!channel) {
        return false;
    }
    auto producer = channel->producer("RAW");
    if (!producer) {
        return false;
    }
    can::CanMessage message{};
    message.kind = can::CanMessageKind::ClassicData;
    message.channel_id = config.channel_id;
    message.can_id = 0x100U + config.channel_id;
    message.dlc = 1;
    message.data[0] = 0x5A;
    message.timestamp_source = can::CanTimestampSource::Simulated;
    const auto now = nexigen::chrono::Timestamp::now();
    message.hardware_timestamp_ns = now ? now->nanoseconds() : 0U;
    return producer
        ->publish(message, can::event_rx, can::event_time(message), can::hardware_metadata(message))
        .has_value();
}

void feed_tx(std::span<can::CanPublisher*> publishers,
             const std::vector<can::CanChannelConfig>& configs) {
    std::this_thread::sleep_for(std::chrono::milliseconds{1000});
    for (std::size_t index = 0; index < configs.size(); ++index) {
        const auto& config = configs[index];
        can::CanMessage message{};
        message.kind = can::CanMessageKind::ClassicData;
        message.channel_id = config.channel_id;
        message.can_id = 0x200U + config.channel_id;
        message.dlc = 1;
        message.data[0] = 0x11;
        check(publishers[index]->send(message).has_value(), "send tx");
        check(send_rx(config), "raw rx publish");
        std::this_thread::sleep_for(std::chrono::milliseconds{150});
        check(send_rx(config), "raw rx publish 2");
        check(send_status(config), "raw status publish");
        std::this_thread::sleep_for(std::chrono::milliseconds{150});
    }
}

void check_runtime(const std::atomic<int>& rx0, const std::atomic<int>& rx1,
                   const std::atomic<std::thread::id>& callback_thread,
                   const std::thread::id& caller, const std::atomic<int>& tx_count) {
    check(rx0.load() >= 1 && rx1.load() >= 1, "two rx callbacks");
    check(callback_thread.load() == caller, "callbacks on run thread");
    check(tx_count.load() >= 1, "tx publication observed");
}

void check_stats(can::CanClient<app::policies::ConsolePolicy>& client,
                 can::CanPublisher* publisher0, can::CanPublisher* publisher1) {
    const auto stats0 = client.stats("can0");
    const auto stats1 = client.stats("can1");
    check(stats0.has_value() && stats0->sent >= 1U && stats0->received >= 1U, "can0 stats");
    check(stats1.has_value() && stats1->sent >= 1U && stats1->received >= 1U, "can1 stats");
    if (publisher0 != nullptr) {
        can::CanMessage invalid{};
        invalid.kind = can::CanMessageKind::Error;
        check(!publisher0->send(invalid).has_value(), "invalid send rejected");
        const auto failed = client.stats("can0");
        check(failed.has_value() && failed->failed >= 1U, "failed counter");
    }
    auto burst = client.publish("can0");
    if (burst.has_value()) {
        can::CanMessage message{};
        message.kind = can::CanMessageKind::ClassicData;
        message.channel_id = 1;
        message.can_id = 0x321;
        message.dlc = 1;
        int backpressure = 0;
        for (int i = 0; i < 5000; ++i) {
            if (!(*burst)->send(message).has_value()) {
                ++backpressure;
            }
        }
        check(backpressure > 0, "publisher backpressure");
    }
    check(!client.subscribe("can0", [](const can::CanMessage&) {}), "late subscribe rejected");
    check(!client.publish("can0").has_value(), "late publish rejected");
    std::atomic<int> concurrent{0};
    std::jthread first{[&] {
        can::CanMessage message{};
        message.kind = can::CanMessageKind::ClassicData;
        message.channel_id = 1;
        message.can_id = 0x1;
        message.dlc = 1;
        for (int i = 0; i < 50; ++i) {
            if (publisher0->send(message).has_value()) {
                concurrent.fetch_add(1);
            }
        }
    }};
    std::jthread second{[&] {
        can::CanMessage message{};
        message.kind = can::CanMessageKind::ClassicData;
        message.channel_id = 2;
        message.can_id = 0x2;
        message.dlc = 1;
        for (int i = 0; i < 50; ++i) {
            if (publisher1->send(message).has_value()) {
                concurrent.fetch_add(1);
            }
        }
    }};
    first.join();
    second.join();
    check(concurrent.load() > 0, "concurrent publishers");
    check(client.shutdown().has_value(), "shutdown");
    check(client.shutdown().has_value(), "idempotent shutdown");
}

bool setup_client(can::CanClient<app::policies::ConsolePolicy>& client, std::atomic<int>& rx0,
                  std::atomic<int>& rx1, std::atomic<std::thread::id>& callback_thread,
                  can::CanPublisher*& publisher0, can::CanPublisher*& publisher1) {
    check(client.subscribe("can0",
                           [&](const can::CanMessage&) {
                               rx0.fetch_add(1);
                               callback_thread.store(std::this_thread::get_id());
                           }),
          "subscribe can0");
    check(client.subscribe("can1",
                           [&](const com::EventHeader&, const can::CanMessage&) {
                               rx1.fetch_add(1);
                               callback_thread.store(std::this_thread::get_id());
                           }),
          "subscribe can1");
    auto first = client.publish("can0");
    auto second = client.publish("can1");
    check(first.has_value() && second.has_value(), "publish channels");
    if (!first.has_value() || !second.has_value()) {
        return false;
    }
    publisher0 = *first;
    publisher1 = *second;
    return true;
}

void run_client_test() {
    const std::vector<can::CanChannelConfig> configs{channel("can0", 1), channel("can1", 2)};
    auto created = can::CanClient<app::policies::ConsolePolicy>::create(
        static_cast<int>(args().size()), args().data(), "can-client-test", configs,
        app::TimeBaseMode::Embedded);
    check(created.has_value() && *created != nullptr, "client create");
    if (!created.has_value() || !*created) {
        return;
    }
    auto& client = **created;
    std::atomic<int> rx0{0};
    std::atomic<int> rx1{0};
    std::atomic<std::thread::id> callback_thread{};
    const auto caller = std::this_thread::get_id();
    can::CanPublisher* publisher0 = nullptr;
    can::CanPublisher* publisher1 = nullptr;
    if (!setup_client(client, rx0, rx1, callback_thread, publisher0, publisher1)) {
        return;
    }
    std::array<can::CanPublisher*, 2> publishers{publisher0, publisher1};

    std::atomic<int> tx_count{0};
    std::jthread raw_receiver{[&](std::stop_token token) {
        auto node = com::Node::create();
        auto name0 = com::ServiceName::create(can::tx_service("can0")->c_str());
        auto name1 = com::ServiceName::create(can::tx_service("can1")->c_str());
        if (!node || !name0 || !name1) {
            return;
        }
        com::ChannelConfig config{};
        auto sub0 = com::Subscriber<can::CanMessage, com::EventHeader>::create(
            *node, *name0, config.publish_subscribe_options());
        auto sub1 = com::Subscriber<can::CanMessage, com::EventHeader>::create(
            *node, *name1, config.publish_subscribe_options());
        if (!sub0 || !sub1) {
            return;
        }
        while (!token.stop_requested() && tx_count.load() < 2) {
            for (auto* subscriber : {&*sub0, &*sub1}) {
                auto received = subscriber->receive();
                if (received && received->has_value()) {
                    tx_count.fetch_add(1);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }};
    std::jthread feeder{[&] { feed_tx(publishers, configs); }};
    std::jthread stopper{[] {
        std::this_thread::sleep_for(std::chrono::milliseconds{2000});
        static_cast<void>(::kill(::getpid(), SIGTERM));
    }};
    check(client.run().has_value(), "client run");
    feeder.join();
    raw_receiver.request_stop();
    raw_receiver.join();
    stopper.join();
    check_runtime(rx0, rx1, callback_thread, caller, tx_count);
    check_stats(client, publisher0, publisher1);
}

void test_rejections() {
    const std::vector<can::CanChannelConfig> configs{channel("can0", 1)};
    const char* no_id[] = {"can-client-test"};
    auto missing = can::CanClient<app::policies::ConsolePolicy>::create(
        1, no_id, "can-client-test", configs, app::TimeBaseMode::Embedded);
    check(!missing.has_value(), "missing app-id rejected");

    const std::vector<can::CanChannelConfig> duplicate{channel("dup", 1), channel("dup", 2)};
    auto bad = can::CanClient<app::policies::ConsolePolicy>::create(
        static_cast<int>(args().size()), args().data(), "can-client-test", duplicate,
        app::TimeBaseMode::Embedded);
    check(!bad.has_value(), "duplicate channels rejected");
}

} // namespace

int main() {
    run_client_test();
    test_rejections();
    std::printf("hw can client: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
