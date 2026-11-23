// Test-only Linux vcan adapter probe for the CanDriver contract.
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nexigen/hw/can/can_driver.hpp>

namespace can = nexigen::hw::can;

namespace {

class VcanDriver final : public can::CanDriver {
public:
    ~VcanDriver() override {
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    [[nodiscard]] nexigen::core::Result<can::CanDriverCapabilities>
    capabilities() noexcept override {
        can::CanDriverCapabilities caps{};
        caps.hardware_timestamps = true;
        caps.hardware_sequence = true;
        return caps;
    }

    [[nodiscard]] nexigen::core::Status open_channel(const can::CanChannelConfig& config,
                                                     can::ICanMessageSink& sink) noexcept override {
        fd_ = ::socket(AF_CAN, SOCK_RAW, CAN_RAW);
        if (fd_ < 0) {
            return std::unexpected(nexigen::core::ErrorCode::SystemError);
        }
        ifreq ifr{};
        std::snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", config.driver_channel.c_str());
        if (::ioctl(fd_, SIOCGIFINDEX, &ifr) < 0) {
            ::close(fd_);
            fd_ = -1;
            return std::unexpected(nexigen::core::ErrorCode::NotFound);
        }
        sockaddr_can address{};
        address.can_family = AF_CAN;
        address.can_ifindex = ifr.ifr_ifindex;
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof address) < 0) {
            ::close(fd_);
            fd_ = -1;
            return std::unexpected(nexigen::core::ErrorCode::SystemError);
        }
        const int loopback = 0;
        static_cast<void>(
            ::setsockopt(fd_, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &loopback, sizeof loopback));
        sink_ = &sink;
        channel_id_ = config.channel_id;
        stop_.store(false);
        worker_ = std::jthread([this](std::stop_token token) { read_loop(token); });
        return {};
    }

    [[nodiscard]] nexigen::core::Status submit(std::string_view channel,
                                               const can::CanMessage& message) noexcept override {
        static_cast<void>(channel);
        if (fd_ < 0) {
            return std::unexpected(nexigen::core::ErrorCode::NotInitialized);
        }
        can_frame frame{};
        frame.can_id = message.can_id;
        if (message.extended) {
            frame.can_id |= CAN_EFF_FLAG;
        }
        frame.can_dlc = message.dlc > 8U ? 8U : message.dlc;
        for (std::size_t i = 0; i < frame.can_dlc; ++i) {
            frame.data[i] = message.data[i];
        }
        return ::write(fd_, &frame, sizeof frame) == static_cast<ssize_t>(sizeof frame)
                   ? nexigen::core::Status{}
                   : nexigen::core::Status{std::unexpected(nexigen::core::ErrorCode::SystemError)};
    }

    [[nodiscard]] nexigen::core::Status close_channel(std::string_view channel) noexcept override {
        static_cast<void>(channel);
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        sink_ = nullptr;
        return {};
    }

private:
    void read_loop(std::stop_token token) noexcept {
        while (!token.stop_requested()) {
            pollfd descriptor{fd_, POLLIN, 0};
            if (::poll(&descriptor, 1, 100) <= 0) {
                continue;
            }
            can_frame frame{};
            if (::read(fd_, &frame, sizeof frame) != static_cast<ssize_t>(sizeof frame)) {
                continue;
            }
            can::CanMessage message{};
            message.kind = can::CanMessageKind::ClassicData;
            message.channel_id = channel_id_;
            message.extended = (frame.can_id & CAN_EFF_FLAG) != 0U;
            message.can_id = frame.can_id & (message.extended ? CAN_EFF_MASK : CAN_SFF_MASK);
            message.dlc = frame.can_dlc > 8U ? 8U : frame.can_dlc;
            for (std::size_t i = 0; i < message.dlc; ++i) {
                message.data[i] = frame.data[i];
            }
            timespec now{};
            static_cast<void>(::clock_gettime(CLOCK_MONOTONIC_RAW, &now));
            message.hardware_timestamp_ns =
                static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ULL +
                static_cast<std::uint64_t>(now.tv_nsec);
            message.hardware_sequence = sequence_.fetch_add(1U) + 1U;
            message.frame_flags.bits = can::CanFrameFlags::has_hardware_timestamp |
                                       can::CanFrameFlags::has_hardware_sequence;
            message.timestamp_source = can::CanTimestampSource::Hardware;
            message.origin = can::CanMessageOrigin::ExternalDevice;
            if (sink_ != nullptr) {
                sink_->on_message(message);
            }
        }
    }

    int fd_{-1};
    std::uint16_t channel_id_{0};
    can::ICanMessageSink* sink_{nullptr};
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<bool> stop_{false};
    std::jthread worker_{};
};

class CountingSink final : public can::ICanMessageSink {
public:
    void on_message(const can::CanMessage& message) noexcept override {
        if (message.channel_id == 1U) {
            ++count;
        }
    }
    int count{0};
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return 2;
    }
    can::CanChannelConfig config{};
    config.name = "can0";
    config.channel_id = 1;
    config.driver_channel = argv[1];
    config.bit_rate = 500'000;
    VcanDriver receiver;
    VcanDriver sender;
    CountingSink sink{};
    if (!receiver.open_channel(config, sink).has_value() ||
        !sender.open_channel(config, sink).has_value()) {
        std::fprintf(stderr, "vcan probe: cannot open %s\n", argv[1]);
        return 77;
    }
    can::CanMessage message{};
    message.kind = can::CanMessageKind::ClassicData;
    message.channel_id = 1;
    message.can_id = 0x123U;
    message.dlc = 2;
    message.data[0] = 0xAA;
    message.data[1] = 0x55;
    if (!sender.submit("can0", message).has_value()) {
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    static_cast<void>(receiver.close_channel("can0"));
    static_cast<void>(sender.close_channel("can0"));
    if (sink.count == 0) {
        std::fprintf(stderr, "vcan probe: no frame received\n");
        return 1;
    }
    std::printf("vcan probe: received %d frame(s)\n", sink.count);
    return 0;
}
