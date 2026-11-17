// nexigen::com — typed event consumer.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/com/channel_config.hpp>
#include <nexigen/com/concepts/event_payload_concept.hpp>
#include <nexigen/com/detail/event_sequence_tracker.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/event_consumer_stats.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/com/i_event_consumer.hpp>
#include <nexigen/com/loss_info.hpp>
#include <nexigen/com/sample.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>

namespace nexigen::com {

/// @brief Typed consumer of one event channel.
///
/// Tracks the transport sequence, invokes the event callback for delivered
/// samples, and invokes the loss callback for gaps, duplicates, unexpected
/// resets, and producer restarts. The payload reference is valid only during
/// the callback.
///
/// @tparam T Event payload; constrained by `concepts::EventPayload`.
/// @tparam OnEvent Callable invoked as `(const EventHeader&, const T&)`.
/// @tparam OnLoss Callable invoked as `(const LossInfo&)`.
/// @note Spec `com/event-system`, requirements "Transport loss detection and
///       callbacks" and "Consumer waiting and multiplexing".
template <concepts::EventPayload T, class OnEvent, class OnLoss>
class EventConsumer final : public IEventConsumer {
public:
    /// @brief Create the subscriber and listener for one channel.
    /// @param node Node that owns the transport membership.
    /// @param name Service name shared with the producer.
    /// @param config Channel limits; must match the producer's configuration.
    /// @param on_event Event callback.
    /// @param on_loss Loss callback.
    /// @return Consumer, or a mapped `com::Error`/`InvalidArgument`.
    [[nodiscard]] static Result<EventConsumer>
    create(Node& node, ServiceName name, const ChannelConfig& config, OnEvent on_event,
           OnLoss on_loss) noexcept {
        const Status valid = config.validate();
        if (!valid.has_value()) {
            return std::unexpected(valid.error());
        }
        auto subscriber = Subscriber<T, EventHeader>::create(
            node, name, config.publish_subscribe_options());
        if (!subscriber.has_value()) {
            return std::unexpected(subscriber.error());
        }
        auto listener = EventListener::create(node, name, config.event_options());
        if (!listener.has_value()) {
            return std::unexpected(listener.error());
        }
        return EventConsumer{std::move(name), std::move(*subscriber), std::move(*listener),
                             std::move(on_event), std::move(on_loss)};
    }

    EventConsumer(const EventConsumer&) = delete;
    EventConsumer& operator=(const EventConsumer&) = delete;
    /// @brief Move-construct a consumer, transferring the transport handles.
    EventConsumer(EventConsumer&&) noexcept = default;
    /// @brief Move-assign a consumer, transferring the transport handles.
    /// @return This consumer.
    EventConsumer& operator=(EventConsumer&&) noexcept = default;
    ~EventConsumer() override = default;

    /// @brief Drain every currently queued event.
    /// @return Empty status, or the first receive/callback failure.
    [[nodiscard]] Status drain() noexcept override {
        while (true) {
            auto received = subscriber_.receive();
            if (!received.has_value()) {
                return std::unexpected(received.error());
            }
            if (!received->has_value()) {
                return {};
            }
            auto sample = std::move(**received);
            handle(sample.user_header(), sample.payload());
        }
    }

    /// @brief Block until one notification batch arrives, then drain.
    ///
    /// There is no timeout and no programmatic wake; use `EventConsumerSet`
    /// for managed stop, timeout, or multiplexing.
    /// @return Empty status, or a wait/receive failure.
    [[nodiscard]] Status wait() noexcept {
        if (attached_to_set_) {
            return std::unexpected(Error::InvalidState);
        }
        const std::uint64_t before = tracker_.stats().received;
        const auto drained = drain();
        if (!drained.has_value()) {
            return drained;
        }
        if (tracker_.stats().received != before) {
            return {};
        }
        Status drain_status{};
        auto callback = [this, &drain_status](EventId /*id*/) {
            if (drain_status.has_value()) {
                drain_status = drain();
            }
        };
        auto waited = listener_.blocking_wait(callback);
        if (!waited.has_value()) {
            return waited;
        }
        return drain_status;
    }

    /// @brief The listener used for multiplexing.
    /// @return Borrowed listener.
    [[nodiscard]] const EventListener& listener() const noexcept override { return listener_; }

    /// @brief Whether this consumer is attached to a set.
    /// @return `true` while set-attached.
    [[nodiscard]] bool attached_to_set() const noexcept override { return attached_to_set_; }

    /// @brief Set or clear the set-attached state.
    /// @param attached New state.
    void set_attached_to_set(bool attached) noexcept override { attached_to_set_ = attached; }

    /// @brief Consumer statistics.
    /// @return Current counters.
    [[nodiscard]] const EventConsumerStats& stats() const noexcept { return tracker_.stats(); }

    /// @brief Number of delivered events.
    /// @return Cumulative delivered count.
    [[nodiscard]] std::uint64_t received() const noexcept override {
        return tracker_.stats().received;
    }

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable consumer state.
    [[nodiscard]] std::string to_string() const override {
        return std::format("EventConsumer(service={}, received={}, lost={}, duplicates={}, "
                           "resets={}, restarts={})",
                           service_name_.to_string(), tracker_.stats().received,
                           tracker_.stats().transport_lost, tracker_.stats().duplicates,
                           tracker_.stats().unexpected_resets, tracker_.stats().restarts);
    }

private:
    EventConsumer(ServiceName name, Subscriber<T, EventHeader> subscriber,
                  EventListener listener, OnEvent on_event, OnLoss on_loss) noexcept
        : service_name_(std::move(name)), subscriber_(std::move(subscriber)),
          listener_(std::move(listener)), on_event_(std::move(on_event)),
          on_loss_(std::move(on_loss)) {}

    void handle(const EventHeader& header, const T& payload) {
        const auto observation = tracker_.observe(header);
        if (observation.has_loss) {
            LossInfo info{};
            info.reason = observation.reason;
            info.expected_sequence = observation.expected_sequence;
            info.actual_sequence = header.sequence_id;
            info.lost_count = observation.lost_count;
            info.old_epoch = observation.old_epoch;
            info.new_epoch = observation.new_epoch;
            info.actual_timestamp_ns = header.timestamp_ns;
            info.actual_timestamp_domain = header.timestamp_domain;
            on_loss_(info);
        }
        if (observation.deliver) {
            on_event_(header, payload);
        }
    }

    ServiceName service_name_;
    Subscriber<T, EventHeader> subscriber_;
    EventListener listener_;
    OnEvent on_event_;
    OnLoss on_loss_;
    detail::EventSequenceTracker tracker_{};
    bool attached_to_set_{false};
};

} // namespace nexigen::com

/// @brief Formats an event consumer as its diagnostic string.
template <nexigen::com::concepts::EventPayload T, class OnEvent, class OnLoss>
struct std::formatter<nexigen::com::EventConsumer<T, OnEvent, OnLoss>>
    : std::formatter<std::string_view> {
    /// @brief Write the consumer diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param consumer Consumer to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::EventConsumer<T, OnEvent, OnLoss>& consumer, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(consumer.to_string(), ctx);
    }
};
