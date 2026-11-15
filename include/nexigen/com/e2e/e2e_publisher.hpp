// nexigen::com — E2E-protected publisher (change add-e2e-protection).
#pragma once

#include <concepts>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include <nexigen/com/e2e/e2e_codec.hpp>
#include <nexigen/com/e2e/e2e_config.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/sample_mut.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::com {

template <concepts::Message T> class E2ePublisher;

/// @brief One loaned E2E slot: write the payload in place, then send.
///
/// Must not outlive its publisher. Move-only, owns the transport loan.
template <concepts::Message T> class E2ePending {
public:
    E2ePending(SampleMut<E2eMessage<T>> sample, Publisher<E2eMessage<T>>& publisher,
               const E2eConfig& config, std::uint32_t& next_counter) noexcept
        : sample_(std::move(sample)), publisher_(publisher), config_(config),
          next_counter_(next_counter) {}

    E2ePending(const E2ePending&) = delete;
    E2ePending& operator=(const E2ePending&) = delete;
    E2ePending(E2ePending&&) noexcept = default;
    E2ePending& operator=(E2ePending&&) noexcept = default;

    /// @brief Mutable payload inside the shared-memory slot.
    [[nodiscard]] T& payload() noexcept { return sample_.payload().payload; }

    /// @brief Seal the envelope and publish it.
    [[nodiscard]] Status send() noexcept {
        e2e::seal_message(sample_.payload(), config_.profile, config_.data_id, next_counter_);
        next_counter_ = (next_counter_ + 1U) & e2e::counter_mask(config_.profile);
        return publisher_.send(std::move(sample_));
    }

private:
    SampleMut<E2eMessage<T>> sample_;      ///< Owned loan.
    Publisher<E2eMessage<T>>& publisher_;  ///< Borrowed publisher.
    const E2eConfig& config_;              ///< Borrowed configuration.
    std::uint32_t& next_counter_;          ///< Borrowed sender counter.
};

/// @brief Publisher of E2E envelopes.
///
/// @note Spec `com/e2e`, requirements "Zero-copy E2E ports" and "Sender
///       counter progression".
template <concepts::Message T> class E2ePublisher {
public:
    /// @brief Open the service and create the envelope publisher.
    [[nodiscard]] static Result<E2ePublisher> create(Node& node, const ServiceName& name,
                                                     E2eConfig config,
                                                     std::uint64_t history_size = 0U) noexcept {
        if (config.profile == E2eProfile::None) {
            return std::unexpected(Error::InvalidArgument);
        }
        auto inner = Publisher<E2eMessage<T>>::create(node, name, history_size);
        if (!inner) {
            return std::unexpected(inner.error());
        }
        return E2ePublisher{std::move(*inner), config};
    }

    E2ePublisher(const E2ePublisher&) = delete;
    E2ePublisher& operator=(const E2ePublisher&) = delete;
    E2ePublisher(E2ePublisher&&) noexcept = default;
    E2ePublisher& operator=(E2ePublisher&&) noexcept = default;

    /// @brief Seal and publish one payload.
    ///
    /// Writes the payload directly into the shared-memory slot (one copy, no
    /// intermediate envelope), seals there, and hands the slot over.
    template <class U = T>
        requires std::default_initializable<U>
    [[nodiscard]] Status publish(const T& payload) noexcept {
        auto uninitialized = inner_.loan_uninit();
        if (!uninitialized) {
            return std::unexpected(uninitialized.error());
        }
        auto sample = uninitialized->write_payload(E2eMessage<T>{});
        sample.payload().payload = payload;
        e2e::seal_message(sample.payload(), config_.profile, config_.data_id, next_counter_);
        next_counter_ = (next_counter_ + 1U) & e2e::counter_mask(config_.profile);
        return inner_.send(std::move(sample));
    }

    /// @brief Loan a slot for an in-place payload write.
    template <class U = T>
        requires std::default_initializable<U>
    [[nodiscard]] Result<E2ePending<T>> loan() noexcept {
        auto uninitialized = inner_.loan_uninit();
        if (!uninitialized) {
            return std::unexpected(uninitialized.error());
        }
        auto sample = uninitialized->write_payload(E2eMessage<T>{});
        return E2ePending<T>{std::move(sample), inner_, config_, next_counter_};
    }

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("E2ePublisher(profile={}, data_id={}, next_counter={})",
                           nexigen::com::to_string(config_.profile), config_.data_id,
                           next_counter_);
    }

private:
    E2ePublisher(Publisher<E2eMessage<T>> inner, E2eConfig config) noexcept
        : inner_(std::move(inner)), config_(config) {}

    Publisher<E2eMessage<T>> inner_; ///< Envelope publisher.
    E2eConfig config_;               ///< Service configuration.
    std::uint32_t next_counter_{1};  ///< Next sender counter.
};

/// @brief Free helper mirroring `E2ePublisher::to_string()` (mandate 9).
template <concepts::Message T>
[[nodiscard]] std::string to_string(const E2ePublisher<T>& publisher) {
    return publisher.to_string();
}

} // namespace nexigen::com

/// @brief `std::formatter` for the E2E publisher (project mandate 11).
template <nexigen::com::concepts::Message T>
struct std::formatter<nexigen::com::E2ePublisher<T>> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::com::E2ePublisher<T>& publisher,
                                     Ctx& ctx) const {
        return std::formatter<std::string_view>::format(publisher.to_string(), ctx);
    }
};
