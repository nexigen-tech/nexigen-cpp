// nexigen::com — application-facing event channel factory.
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/com/channel_config.hpp>
#include <nexigen/com/concepts/event_payload_concept.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event_consumer.hpp>
#include <nexigen/com/event_producer.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>

namespace nexigen::com {

/// @brief Factory for the producer or consumer endpoint of one event channel.
///
/// The channel stores the process node, service name, and channel settings; it
/// creates no service in the constructor. `producer()` creates only the
/// publisher and notifier, and `consumer()` creates only the subscriber and
/// listener.
///
/// @tparam T Event payload; constrained by `concepts::EventPayload`.
/// @note Spec `com/event-system`, requirement "Event channel factory".
template <concepts::EventPayload T> class EventChannel {
public:
    /// @brief Create a channel with the default 16/512/16 settings.
    /// @param node Node that must outlive the channel and its endpoints.
    /// @param name Service name shared by producer and consumer.
    [[nodiscard]] static Result<EventChannel> create(Node& node, ServiceName name) noexcept {
        return create(node, std::move(name), ChannelConfig{});
    }

    /// @brief Create a channel after validating its configuration.
    /// @param node Node that must outlive the channel and its endpoints.
    /// @param name Service name shared by producer and consumer.
    /// @param config Channel settings.
    /// @return Channel, or `InvalidArgument` for an invalid configuration.
    [[nodiscard]] static Result<EventChannel> create(Node& node, ServiceName name,
                                                     ChannelConfig config) noexcept {
        const Status valid = config.validate();
        if (!valid.has_value()) {
            return std::unexpected(valid.error());
        }
        return EventChannel{node, std::move(name), config};
    }

private:
    /// @brief Create a channel with the default 16/512/16 settings.
    /// @param node Node that must outlive the channel and its endpoints.
    /// @param name Service name shared by producer and consumer.
    EventChannel(Node& node, ServiceName name) noexcept
        : node_(&node), name_(std::move(name)), config_(ChannelConfig{}) {}

    /// @brief Create a channel with explicit settings.
    /// @param node Node that must outlive the channel and its endpoints.
    /// @param name Service name shared by producer and consumer.
    /// @param config Channel settings.
    EventChannel(Node& node, ServiceName name, ChannelConfig config) noexcept
        : node_(&node), name_(std::move(name)), config_(config) {}

public:
    /// @brief Create the producer endpoint.
    /// @return Producer, or a mapped `com::Error`/`InvalidArgument`.
    [[nodiscard]] Result<EventProducer<T>> producer() noexcept {
        return EventProducer<T>::create(*node_, name_, config_);
    }

    /// @brief Create the consumer endpoint.
    /// @tparam OnEvent Callable invoked as `(const EventHeader&, const T&)`.
    /// @tparam OnLoss Callable invoked as `(const LossInfo&)`.
    /// @param on_event Event callback.
    /// @param on_loss Loss callback.
    /// @return Consumer, or a mapped `com::Error`/`InvalidArgument`.
    template <class OnEvent, class OnLoss>
    [[nodiscard]] Result<EventConsumer<T, OnEvent, OnLoss>>
    consumer(OnEvent on_event, OnLoss on_loss) noexcept {
        return EventConsumer<T, OnEvent, OnLoss>::create(*node_, name_, config_,
                                                         std::move(on_event),
                                                         std::move(on_loss));
    }

    /// @brief Service name.
    /// @return Borrowed name.
    [[nodiscard]] const ServiceName& name() const noexcept { return name_; }

    /// @brief Channel settings.
    /// @return Borrowed settings.
    [[nodiscard]] const ChannelConfig& config() const noexcept { return config_; }

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable channel summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("EventChannel(service={}, config={})", name_.to_string(),
                           config_.to_string());
    }

private:
    Node* node_{nullptr};
    ServiceName name_;
    ChannelConfig config_{};
};

} // namespace nexigen::com

/// @brief Formats an event channel as its diagnostic string.
template <nexigen::com::concepts::EventPayload T>
struct std::formatter<nexigen::com::EventChannel<T>> : std::formatter<std::string_view> {
    /// @brief Write the channel diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param channel Channel to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::EventChannel<T>& channel, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(channel.to_string(), ctx);
    }
};
