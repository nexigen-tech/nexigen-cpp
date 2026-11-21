// nexigen::com — typed event producer.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <nexigen/com/channel_config.hpp>
#include <nexigen/com/concepts/event_payload_concept.hpp>
#include <nexigen/com/detail/event_time_source.hpp>
#include <nexigen/com/empty_payload.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/com/event_producer_stats.hpp>
#include <nexigen/com/event_time.hpp>
#include <nexigen/com/hardware_metadata.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publish_outcome.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/core/concepts/meter_concept.hpp>
#include <nexigen/core/null_meter.hpp>
#include <nexigen/com/service_name.hpp>

namespace nexigen::com {

namespace detail {

/// @brief Map a notify status onto the publish outcome.
///
/// A sent sample with a failed wake is still a successful publication; callers
/// must not republish it.
/// @param status Result returned by the notifier.
/// @return `Notified` on success, `NotifyFailed` on failure.
[[nodiscard]] inline PublishOutcome publish_outcome_from_notify(const Status& status) noexcept {
    return status.has_value() ? PublishOutcome::Notified : PublishOutcome::NotifyFailed;
}

#if defined(NEXIGEN_EVENT_SYSTEM_TESTING)
/// @brief Deterministic producer faults, compiled only into the test target.
///
/// The real transport failures (`loan()`, `send()`, `notify()`) cannot be
/// triggered on demand, so the event-system tests toggle these flags instead.
/// They do not exist in production builds and never alter the hot path there.
struct EventProducerFaults {
    static inline bool fail_loan{false};   ///< Make `loan_uninit()` fail.
    static inline bool fail_send{false};   ///< Make `send()` fail after writing.
    static inline bool fail_notify{false}; ///< Make `notify()` fail.

    /// @brief Clear every injected fault.
    static void reset() noexcept {
        fail_loan = false;
        fail_send = false;
        fail_notify = false;
    }
};
#endif

} // namespace detail

/// @brief Typed producer of one event channel.
///
/// Publishes zero-copy samples with `EventHeader`, assigns the transport
/// sequence after a successful send, samples the host ingest time, and
/// preserves optional FPGA metadata. `on_source_reset()` starts a new epoch
/// and resets the sequence.
///
/// @tparam T Event payload; constrained by `concepts::EventPayload`.
/// @note Spec `com/event-system`, requirements "Event production and publish
///       outcome" and "Producer epoch and source restart".
template <concepts::EventPayload T, core::concepts::MeterLike M = core::NullMeter>
class EventProducer {
public:
    /// @brief Create the publisher and notifier for one channel.
    /// @param node Node that owns the transport membership.
    /// @param name Service name shared with the consumer.
    /// @param config Channel limits; must match the consumer's configuration.
    /// @return Producer, or a mapped `com::Error`/`InvalidArgument`.
    [[nodiscard]] static Result<EventProducer>
    create(Node& node, ServiceName name, const ChannelConfig& config, M* meter = nullptr) noexcept {
        const Status valid = config.validate();
        if (!valid.has_value()) {
            return std::unexpected(valid.error());
        }
        auto publisher =
            Publisher<T, EventHeader>::create(node, name, config.publish_subscribe_options());
        if (!publisher.has_value()) {
            return std::unexpected(publisher.error());
        }
        auto notifier = EventNotifier::create(node, name, config.event_options());
        if (!notifier.has_value()) {
            return std::unexpected(notifier.error());
        }
        EventProducer result{std::move(name), std::move(*publisher), std::move(*notifier), config};
        result.bind_counters(meter);
        return result;
    }

    EventProducer(const EventProducer&) = delete;
    EventProducer& operator=(const EventProducer&) = delete;
    /// @brief Move-construct a producer, transferring the transport handles.
    EventProducer(EventProducer&&) noexcept = default;
    /// @brief Move-assign a producer, transferring the transport handles.
    /// @return This producer.
    EventProducer& operator=(EventProducer&&) noexcept = default;
    ~EventProducer() = default;

    /// @brief Publish a software-timed event.
    /// @param payload Event payload.
    /// @param event_code Canonical platform event code.
    /// @return `Notified`/`NotifyFailed`, or a send error.
    [[nodiscard]] Result<PublishOutcome> publish_now(const T& payload,
                                                     std::uint32_t event_code) noexcept {
        const auto now = detail::event_now_ns();
        if (!now.has_value()) {
            return std::unexpected(now.error());
        }
        return publish_impl(payload, event_code, EventTime{*now, config_.default_timestamp_domain},
                            HardwareMetadata{}, *now);
    }

    /// @brief Publish an event with explicit time and optional FPGA metadata.
    /// @param payload Event payload.
    /// @param event_code Canonical platform event code.
    /// @param time Event time and domain.
    /// @param hardware Optional hardware metadata.
    /// @return `Notified`/`NotifyFailed`, or a send error.
    [[nodiscard]] Result<PublishOutcome> publish(const T& payload, std::uint32_t event_code,
                                                 EventTime time,
                                                 HardwareMetadata hardware = {}) noexcept {
        const auto ingest = detail::event_now_ns();
        if (!ingest.has_value()) {
            return std::unexpected(ingest.error());
        }
        return publish_impl(payload, event_code, time, hardware, *ingest);
    }

    /// @brief Publish a payload-free event.
    /// @param event_code Canonical platform event code.
    /// @return `Notified`/`NotifyFailed`, or a send error.
    [[nodiscard]] Result<PublishOutcome> publish_now(std::uint32_t event_code) noexcept
        requires std::is_same_v<T, EmptyPayload>
    {
        return publish_now(EmptyPayload{}, event_code);
    }

    /// @brief Retry the wake for the most recently published sample.
    /// @return Empty status, or the notify error.
    [[nodiscard]] Status notify() noexcept {
#if defined(NEXIGEN_EVENT_SYSTEM_TESTING)
        const auto status = detail::EventProducerFaults::fail_notify
                                ? Status{std::unexpected(Error::ConnectionFailure)}
                                : notifier_.notify();
#else
        const auto status = notifier_.notify();
#endif
        if (!status.has_value()) {
            ++stats_.notify_failures;
            stats_.last_notify_error = status.error();
            return status;
        }
        return {};
    }

    /// @brief Start a new producer lifecycle after an FPGA/source reset.
    ///
    /// Must be called on the publishing thread after the gateway has quiesced
    /// its SPSC ring. Replaces the epoch and resets the transport sequence.
    void on_source_reset() noexcept {
        epoch_ = detail::make_event_epoch();
        sequence_ = 0;
        stats_.current_epoch = epoch_;
        stats_.current_sequence = 0;
    }

    /// @brief Producer statistics.
    /// @return Current counters.
    [[nodiscard]] const EventProducerStats& stats() const noexcept { return stats_; }

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable producer state.
    [[nodiscard]] std::string to_string() const {
        return std::format("EventProducer(service={}, published={}, sequence={}, epoch={})",
                           service_name_.to_string(), stats_.published, sequence_, epoch_);
    }

private:
    EventProducer(ServiceName name, Publisher<T, EventHeader> publisher, EventNotifier notifier,
                  const ChannelConfig& config) noexcept
        : service_name_(std::move(name)), publisher_(std::move(publisher)),
          notifier_(std::move(notifier)), config_(config), epoch_(detail::make_event_epoch()) {
        stats_.current_epoch = epoch_;
    }

    [[nodiscard]] Result<PublishOutcome> publish_impl(const T& payload, std::uint32_t event_code,
                                                      EventTime time,
                                                      const HardwareMetadata& hardware,
                                                      std::uint64_t ingest_ns) noexcept {
        EventHeader header{};
        header.sequence_id = sequence_;
        const bool software_time = time.domain != TimestampDomain::Hardware;
        header.timestamp_ns =
            hardware.has_canonical ? hardware.canonical_ns : (software_time ? time.ns : 0U);
        header.producer_epoch = epoch_;
        header.ingest_timestamp_ns = ingest_ns;
        header.event_code = event_code;
        header.domain_epoch = hardware.domain_epoch;
        header.native_ticks = hardware.native_ticks;
        header.hardware_sequence = static_cast<std::uint32_t>(hardware.sequence);
        header.hardware_event_code = hardware.event_code;
        header.domain_id = software_time ? 0U : hardware.domain_id;
        const bool has_payload = !std::is_same_v<T, EmptyPayload>;
        const bool has_projection = software_time || hardware.has_canonical;
        header.flags = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(has_payload) * EventHeader::has_payload_bit |
            static_cast<std::uint16_t>(has_projection) * EventHeader::has_canonical_projection_bit |
            static_cast<std::uint16_t>(hardware.has_sequence) *
                EventHeader::has_hardware_sequence_bit |
            static_cast<std::uint16_t>(hardware.has_ticks) *
                EventHeader::has_hardware_timestamp_bit |
            static_cast<std::uint16_t>(hardware.has_event_code) *
                EventHeader::has_hardware_event_code_bit);

#if defined(NEXIGEN_EVENT_SYSTEM_TESTING)
        if (detail::EventProducerFaults::fail_loan) {
            record_publish_failure();
            return std::unexpected(Error::OutOfSamples);
        }
#endif
        auto loan = publisher_.loan_uninit();
        if (!loan.has_value()) {
            record_publish_failure();
            return std::unexpected(loan.error());
        }
        auto sample = loan->write_payload(payload);
        sample.user_header_mut() = header;
#if defined(NEXIGEN_EVENT_SYSTEM_TESTING)
        if (detail::EventProducerFaults::fail_send) {
            record_publish_failure();
            return std::unexpected(Error::UnableToDeliver);
        }
#endif
        const auto sent = publisher_.send(std::move(sample));
        if (!sent.has_value()) {
            record_publish_failure();
            return std::unexpected(sent.error());
        }

        ++sequence_;
        ++stats_.published;
        if (published_ != nullptr) {
            published_->add(1U);
        }
        stats_.current_sequence = sequence_;
#if defined(NEXIGEN_EVENT_SYSTEM_TESTING)
        const auto notified = detail::EventProducerFaults::fail_notify
                                  ? Status{std::unexpected(Error::ConnectionFailure)}
                                  : notifier_.notify();
#else
        const auto notified = notifier_.notify();
#endif
        if (!notified.has_value()) {
            ++stats_.notify_failures;
            stats_.last_notify_error = notified.error();
        }
        return detail::publish_outcome_from_notify(notified);
    }

    /// @brief Count one failed publication.
    void record_publish_failure() noexcept {
        ++stats_.send_failures;
        if (publish_failed_ != nullptr) {
            publish_failed_->add(1U);
        }
    }

    /// @brief Create this producer's counters from the supplied meter.
    void bind_counters(M* meter) noexcept {
        meter_ = meter;
        if (meter_ == nullptr) {
            return;
        }
        if (auto created = meter_->template create_counter<>("published", "1");
            created.has_value()) {
            published_ = *created;
        }
        if (auto created = meter_->template create_counter<>("publish_failed", "1");
            created.has_value()) {
            publish_failed_ = *created;
        }
    }

    using MeterCounter =
        std::remove_cvref_t<decltype(**std::declval<M&>().template create_counter<>("", ""))>;
    M* meter_{nullptr};
    MeterCounter* published_{nullptr};
    MeterCounter* publish_failed_{nullptr};
    ServiceName service_name_;
    Publisher<T, EventHeader> publisher_;
    EventNotifier notifier_;
    ChannelConfig config_{};
    std::uint64_t epoch_{0};
    std::uint64_t sequence_{0};
    EventProducerStats stats_{};
};

} // namespace nexigen::com

/// @brief Formats an event producer as its diagnostic string.
template <nexigen::com::concepts::EventPayload T>
struct std::formatter<nexigen::com::EventProducer<T>> : std::formatter<std::string_view> {
    /// @brief Write the producer diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param producer Producer to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::EventProducer<T>& producer, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(producer.to_string(), ctx);
    }
};
