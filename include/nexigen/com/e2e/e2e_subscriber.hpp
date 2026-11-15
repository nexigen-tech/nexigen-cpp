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
template <concepts::Message T> class E2eSample {
public:
    E2eSample(Sample<E2eMessage<T>> sample, E2eStatus status, E2eState state) noexcept
        : sample_(std::move(sample)), status_(status), state_(state) {}

    E2eSample(const E2eSample&) = delete;
    E2eSample& operator=(const E2eSample&) = delete;
    E2eSample(E2eSample&&) noexcept = default;
    E2eSample& operator=(E2eSample&&) noexcept = default;

    /// @brief The producer's payload bytes.
    [[nodiscard]] const T& payload() const noexcept { return sample_.payload().payload; }

    /// @brief Classification of this message.
    [[nodiscard]] E2eStatus status() const noexcept { return status_; }

    /// @brief Counters after this message.
    [[nodiscard]] const E2eState& state() const noexcept { return state_; }

    /// @brief The wire header that carried the payload.
    [[nodiscard]] const E2eHeader& header() const noexcept { return sample_.payload().header; }

private:
    Sample<E2eMessage<T>> sample_; ///< Owned slot borrow.
    E2eStatus status_;             ///< Check result.
    E2eState state_;               ///< Counter snapshot.
};

/// @brief Subscriber of E2E envelopes with counter supervision.
///
/// @note Spec `com/e2e`, requirements "Zero-copy E2E ports" and "Receiver
///       supervision".
template <concepts::Message T> class E2eSubscriber {
public:
    /// @brief Open the service and create the envelope subscriber.
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

    E2eSubscriber(const E2eSubscriber&) = delete;
    E2eSubscriber& operator=(const E2eSubscriber&) = delete;
    E2eSubscriber(E2eSubscriber&&) noexcept = default;
    E2eSubscriber& operator=(E2eSubscriber&&) noexcept = default;

    /// @brief Receive one message and classify it.
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
    [[nodiscard]] const E2eState& state() const noexcept { return receiver_.state(); }

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("E2eSubscriber(profile={}, data_id={}, {})",
                           nexigen::com::to_string(config_.profile), config_.data_id,
                           receiver_.state().to_string());
    }

private:
    E2eSubscriber(Subscriber<E2eMessage<T>> inner, E2eConfig config) noexcept
        : inner_(std::move(inner)), config_(config),
          receiver_(config.profile, config.data_id, config.max_delta_counter) {}

    Subscriber<E2eMessage<T>> inner_; ///< Envelope subscriber.
    E2eConfig config_;                ///< Service configuration.
    E2eReceiver receiver_;            ///< Check and counter state.
};

/// @brief Free helper mirroring `E2eSubscriber::to_string()` (mandate 9).
template <concepts::Message T>
[[nodiscard]] std::string to_string(const E2eSubscriber<T>& subscriber) {
    return subscriber.to_string();
}

} // namespace nexigen::com

/// @brief `std::formatter` for the E2E subscriber (project mandate 11).
template <nexigen::com::concepts::Message T>
struct std::formatter<nexigen::com::E2eSubscriber<T>> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::E2eSubscriber<T>& subscriber, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(subscriber.to_string(), ctx);
    }
};
