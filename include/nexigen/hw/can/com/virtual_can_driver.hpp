#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

#include <nexigen/com/event_channel.hpp>
#include <nexigen/com/event_consumer.hpp>
#include <nexigen/com/event_consumer_set.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_channel_config.hpp>
#include <nexigen/hw/can/can_driver.hpp>
#include <nexigen/hw/can/can_service_names.hpp>
#include <nexigen/hw/can/com/can_transport.hpp>

namespace nexigen::hw::can {

/// @brief `CanDriver` backed by `nexigen-canvbus` over iceoryx2.
///
/// The driver subscribes to each channel's `vbus/rx` service and publishes
/// transmission requests on `vbus/tx`. It is the virtual-hardware backend used
/// by `nexigen-canrx` and `nexigen-cantx`.
class VirtualCanDriver final : public CanDriver {
public:
    /// @brief Create the transport node and wait set.
    /// @param app_id Application identity stamped on vbus publications.
    explicit VirtualCanDriver(std::string_view app_id) noexcept;

    /// @brief Stop the wait thread and release services.
    ~VirtualCanDriver() override;

    VirtualCanDriver(const VirtualCanDriver&) = delete;
    VirtualCanDriver& operator=(const VirtualCanDriver&) = delete;
    VirtualCanDriver(VirtualCanDriver&&) = delete;
    VirtualCanDriver& operator=(VirtualCanDriver&&) = delete;

    /// @brief Report virtual capabilities.
    /// @return Capabilities with all optional kinds enabled.
    [[nodiscard]] core::Result<CanDriverCapabilities> capabilities() noexcept override;

    /// @brief Open a virtual channel endpoint.
    /// @param config Channel configuration.
    /// @param sink Receiver of `vbus/rx` messages.
    /// @return OK, or a service/attach failure.
    [[nodiscard]] core::Status open_channel(const CanChannelConfig& config,
                                            ICanMessageSink& sink) noexcept override;

    /// @brief Publish a transmission request on `vbus/tx`.
    /// @param channel Channel name.
    /// @param message Message to publish.
    /// @return Publish outcome or a mapping error.
    [[nodiscard]] core::Status submit(std::string_view channel,
                                      const CanMessage& message) noexcept override;

    /// @brief Close one virtual channel endpoint.
    /// @param channel Channel name.
    /// @return OK or `NotFound`.
    [[nodiscard]] core::Status close_channel(std::string_view channel) noexcept override;

    /// @brief Current usable state.
    /// @return `true` when the node and wait set were created.
    [[nodiscard]] bool usable() const noexcept { return worker_.joinable(); }

private:
    class EndpointBase {
    public:
        virtual ~EndpointBase() = default;
        [[nodiscard]] virtual com::IEventConsumer& consumer() noexcept = 0;
        [[nodiscard]] virtual com::EventProducer<CanMessage>* ensure_producer() noexcept = 0;
    };

    template <class OnEvent, class OnLoss> class Endpoint final : public EndpointBase {
    public:
        Endpoint(com::Node& node, com::ServiceName tx_name, com::ChannelConfig config,
                 std::string app_id, com::EventChannel<CanMessage> rx_channel,
                 com::EventConsumer<CanMessage, OnEvent, OnLoss> consumer)
            : node_(&node), tx_name_(std::move(tx_name)), config_(config),
              app_id_(std::move(app_id)), rx_channel_(std::move(rx_channel)),
              consumer_(std::move(consumer)) {}

        [[nodiscard]] com::IEventConsumer& consumer() noexcept override { return consumer_; }

        [[nodiscard]] com::EventProducer<CanMessage>* ensure_producer() noexcept override {
            if (producer_.has_value()) {
                return &*producer_;
            }
            auto created =
                com::EventProducer<CanMessage>::create(*node_, tx_name_, config_, app_id_);
            if (!created.has_value()) {
                return nullptr;
            }
            producer_.emplace(std::move(*created));
            return &*producer_;
        }

    private:
        com::Node* node_{nullptr};
        com::ServiceName tx_name_;
        com::ChannelConfig config_{};
        std::string app_id_{};
        com::EventChannel<CanMessage> rx_channel_;
        com::EventConsumer<CanMessage, OnEvent, OnLoss> consumer_;
        std::optional<com::EventProducer<CanMessage>> producer_{};
    };

    void wait_loop(std::stop_token token) noexcept;
    [[nodiscard]] EndpointBase* find(std::string_view channel) noexcept;

    std::string app_id_{};
    std::unique_ptr<com::Node> node_{};
    std::optional<com::EventConsumerSet<16>> set_{};
    std::vector<std::pair<std::string, std::unique_ptr<EndpointBase>>> channels_{};
    std::jthread worker_{};
};

} // namespace nexigen::hw::can
