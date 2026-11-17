// nexigen::com — E2E-protected subscriber (change add-e2e-protection).
#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include <nexigen/com/e2e/e2e_config.hpp>
#include <nexigen/com/e2e/e2e_receiver.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/sample.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::com {

/// @brief One received E2E message: the payload plus its check result.
///
/// The sample owns the shared-memory borrow, so the payload reference stays
/// valid until the sample is destroyed. `status()` and `state()` report the
/// receiver's verdict without a separate query.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @note Spec `com/e2e`, requirements "Zero-copy E2E ports" and "Receiver
///       supervision".
template <concepts::Message T> class E2eSample {
public:
    /// @brief Adopt a received slot and its check result.
    /// @param sample Owned shared-memory borrow.
    /// @param status Classification of this message.
    /// @param state Counters after this message.
    E2eSample(Sample<E2eMessage<T>> sample, E2eStatus status, E2eState state) noexcept
        : sample_(std::move(sample)), status_(status), state_(state) {}

    /// @brief Copying is disabled: the sample owns the transport borrow.
    E2eSample(const E2eSample&) = delete;
    /// @brief Copy assignment is disabled: the sample owns the transport borrow.
    /// @return This sample.
    E2eSample& operator=(const E2eSample&) = delete;
    /// @brief Move-construct a sample, transferring the borrow.
    E2eSample(E2eSample&&) noexcept = default;
    /// @brief Move-assign a sample, transferring the borrow.
    /// @return This sample.
    E2eSample& operator=(E2eSample&&) noexcept = default;

    /// @brief The producer's payload bytes.
    /// @return Borrowed payload; valid while this sample is alive.
    /// @note Spec `com/e2e`, requirement "Zero-copy E2E ports".
    [[nodiscard]] const T& payload() const noexcept { return sample_.payload().payload; }

    /// @brief Classification of this message.
    /// @return The E2E status computed by the receiver.
    /// @note Spec `com/e2e`, requirement "Receiver supervision".
    [[nodiscard]] E2eStatus status() const noexcept { return status_; }

    /// @brief Counters after this message.
    /// @return Borrowed snapshot of the endpoint counters.
    /// @note Spec `com/e2e`, requirement "Receiver supervision".
    [[nodiscard]] const E2eState& state() const noexcept { return state_; }

    /// @brief The wire header that carried the payload.
    /// @return Borrowed E2E header.
    /// @note Spec `com/e2e`, requirement "E2E envelope and checksum".
    [[nodiscard]] const E2eHeader& header() const noexcept { return sample_.payload().header; }

private:
    Sample<E2eMessage<T>> sample_; ///< Owned slot borrow.
    E2eStatus status_;             ///< Check result.
    E2eState state_;               ///< Counter snapshot.
};

/// @brief Subscriber of E2E envelopes with counter supervision.
///
/// Wraps `com::Subscriber` and classifies every received envelope before the
/// application sees it. The payload and check result travel together in
/// `E2eSample`, so a corrupted message is never silently dropped.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @note Spec `com/e2e`, requirements "Zero-copy E2E ports" and "Receiver
///       supervision".
template <concepts::Message T> class E2eSubscriber {
public:
    /// @brief Open the service and create the envelope subscriber.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the subscriber.
    /// @param name Valid service name shared with the publisher.
    /// @param config E2E profile, data id, and counter tolerance.
    /// @param buffer_size Per-subscriber queue depth; `2` matches the plain
    ///        subscriber default.
    /// @return The constructed subscriber, or `InvalidArgument` when `config`
    ///         selects `E2eProfile::None`, or another mapped transport error.
    /// @note Spec `com/e2e`, requirement "Wrappers require a protection
    ///       profile": the profile is validated before any port is opened.
    [[nodiscard]] static Result<E2eSubscriber> create(Node& node, const ServiceName& name,
                                                      E2eConfig config,
                                                      std::uint64_t buffer_size = 2U) noexcept {
        if (config.profile == E2eProfile::None) {
            return std::unexpected(Error::InvalidArgument);
        }
        auto inner = Subscriber<E2eMessage<T>>::create(node, name, buffer_size);
        if (!inner) {
            return std::unexpected(inner.error());
        }
        return E2eSubscriber{std::move(*inner), config};
    }

    /// @brief Copying is disabled: the subscriber owns the transport handle.
    E2eSubscriber(const E2eSubscriber&) = delete;
    /// @brief Copy assignment is disabled: the subscriber owns the transport handle.
    /// @return This subscriber.
    E2eSubscriber& operator=(const E2eSubscriber&) = delete;
    /// @brief Move-construct a subscriber, transferring the transport handle.
    E2eSubscriber(E2eSubscriber&&) noexcept = default;
    /// @brief Move-assign a subscriber, transferring the transport handle.
    /// @return This subscriber.
    E2eSubscriber& operator=(E2eSubscriber&&) noexcept = default;

    /// @brief Receive one message and classify it.
    ///
    /// @return The received sample with its status and counter snapshot; an
    ///         empty optional when no message is queued; or the mapped receive
    ///         error.
    /// @note Spec `com/e2e`, requirements "Zero-copy E2E ports" and "Receiver
    ///       supervision"; non-blocking.
    [[nodiscard]] Result<std::optional<E2eSample<T>>> receive() noexcept {
        auto received = inner_.receive();
        if (!received) {
            return std::unexpected(received.error());
        }
        if (!received->has_value()) {
            return std::optional<E2eSample<T>>{};
        }
        const E2eStatus status = receiver_.accept((**received).payload());
        return std::optional<E2eSample<T>>{
            E2eSample<T>{std::move(**received), status, receiver_.state()}};
    }

    /// @brief Counters accumulated by this subscriber.
    /// @return Borrowed per-endpoint counters.
    /// @note Spec `com/e2e`, requirement "Receiver supervision".
    [[nodiscard]] const E2eState& state() const noexcept { return receiver_.state(); }

    /// @brief Cold-path diagnostic rendering.
    /// @return `E2eSubscriber(profile=..., data_id=..., E2eState(...))`.
    /// @note Spec `com/e2e`, requirement "Zero-copy E2E ports".
    [[nodiscard]] std::string to_string() const {
        return std::format("E2eSubscriber(profile={}, data_id={}, {})",
                           nexigen::com::to_string(config_.profile), config_.data_id,
                           receiver_.state().to_string());
    }

private:
    /// @brief Adopt an existing enveloped subscriber and its configuration.
    /// @param inner Wrapped `com::Subscriber` instance.
    /// @param config E2E profile, data id, and counter tolerance.
    E2eSubscriber(Subscriber<E2eMessage<T>> inner, E2eConfig config) noexcept
        : inner_(std::move(inner)), config_(config),
          receiver_(config.profile, config.data_id, config.max_delta_counter) {}

    Subscriber<E2eMessage<T>> inner_; ///< Envelope subscriber.
    E2eConfig config_;                ///< Service configuration.
    E2eReceiver receiver_;            ///< Check and counter state.
};

/// @brief Free helper mirroring `E2eSubscriber::to_string()` (mandate 9).
///
/// @tparam T Payload type.
/// @param subscriber Subscriber to render.
/// @return The diagnostic string.
/// @note Spec `com/e2e`, requirement "Zero-copy E2E ports".
template <concepts::Message T>
[[nodiscard]] std::string to_string(const E2eSubscriber<T>& subscriber) {
    return subscriber.to_string();
}

} // namespace nexigen::com

/// @brief `std::formatter` for the E2E subscriber (project mandate 11).
template <nexigen::com::concepts::Message T>
struct std::formatter<nexigen::com::E2eSubscriber<T>> : std::formatter<std::string_view> {
    /// @brief Write the subscriber diagnostic into the format context.
    /// @tparam Ctx `std::format` context type.
    /// @param subscriber Subscriber to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Spec `com/e2e`, requirement "Zero-copy E2E ports".
    template <class Ctx>
    auto format(const nexigen::com::E2eSubscriber<T>& subscriber, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(subscriber.to_string(), ctx);
    }
};
