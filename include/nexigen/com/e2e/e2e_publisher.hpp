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

/// @brief Forward declaration so `E2ePending` can name its owning publisher.
/// @tparam T Payload type satisfying `com::concepts::Message`.
template <concepts::Message T> class E2ePublisher;

/// @brief One loaned E2E slot: write the payload in place, then send.
///
/// The slot already contains a default-constructed envelope; `payload()`
/// exposes the application payload for in-place writing, and `send()` seals the
/// header/checksum before handing the slot to the transport. The handle must
/// not outlive its publisher and is move-only because it owns the transport
/// loan.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @note Spec `com/e2e`, requirements "Zero-copy E2E ports" and "Sender counter
///       progression".
template <concepts::Message T> class E2ePending {
public:
    /// @brief Adopt a loaned envelope and the sender counter to advance.
    /// @param sample Owned shared-memory slot.
    /// @param publisher Publisher that will send the slot; must outlive this
    ///        handle.
    /// @param config E2E profile and data id used when sealing.
    /// @param next_counter Sender counter storage updated on `send()`.
    E2ePending(SampleMut<E2eMessage<T>> sample, Publisher<E2eMessage<T>>& publisher,
               const E2eConfig& config, std::uint32_t& next_counter) noexcept
        : sample_(std::move(sample)), publisher_(publisher), config_(config),
          next_counter_(next_counter) {}

    /// @brief Copying is disabled: the handle owns the transport loan.
    E2ePending(const E2ePending&) = delete;
    /// @brief Copy assignment is disabled: the handle owns the transport loan.
    /// @return This handle.
    E2ePending& operator=(const E2ePending&) = delete;
    /// @brief Move-construct a pending handle, transferring the loan.
    E2ePending(E2ePending&&) noexcept = default;
    /// @brief Move-assign a pending handle, transferring the loan.
    /// @return This handle.
    E2ePending& operator=(E2ePending&&) noexcept = default;

    /// @brief Mutable payload inside the shared-memory slot.
    /// @return Reference to the application payload for in-place writing.
    /// @note The reference is valid until `send()` or destruction of the handle.
    [[nodiscard]] T& payload() noexcept { return sample_.payload().payload; }

    /// @brief Seal the envelope and publish it.
    ///
    /// Fills `data_id`, `counter`, `length`, and the profile checksum, advances
    /// the sender counter, and sends the slot through the owning publisher.
    ///
    /// @return Empty status on success, or the mapped send error.
    /// @note Spec `com/e2e`, requirements "E2E envelope and checksum" and
    ///       "Sender counter progression"; one sealed publication per handle.
    [[nodiscard]] Status send() noexcept {
        e2e::seal_message(sample_.payload(), config_.profile, config_.data_id, next_counter_);
        next_counter_ = (next_counter_ + 1U) & e2e::counter_mask(config_.profile);
        return publisher_.send(std::move(sample_));
    }

private:
    SampleMut<E2eMessage<T>> sample_;     ///< Owned loan.
    Publisher<E2eMessage<T>>& publisher_; ///< Borrowed publisher.
    const E2eConfig& config_;             ///< Borrowed configuration.
    std::uint32_t& next_counter_;         ///< Borrowed sender counter.
};

/// @brief Publisher of E2E envelopes.
///
/// Wraps `com::Publisher` and seals every message in place with the configured
/// profile before sending. Two publication styles mirror the plain publisher:
/// `publish()` copies the caller's payload into a loaned slot, while `loan()`
/// returns an `E2ePending` for fully in-place construction.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @note Spec `com/e2e`, requirements "Zero-copy E2E ports" and "Sender counter
///       progression".
template <concepts::Message T> class E2ePublisher {
public:
    /// @brief Open the service and create the envelope publisher.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the publisher.
    /// @param name Valid service name shared with the subscriber.
    /// @param config E2E profile, data id, and counter tolerance.
    /// @param history_size Optional late-join history depth; `0` keeps the
    ///        transport default.
    /// @return The constructed publisher, or `InvalidArgument` when `config`
    ///         selects `E2eProfile::None`, or another mapped transport error.
    /// @note Spec `com/e2e`, requirement "Wrappers require a protection
    ///       profile": the profile is validated before any port is opened.
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

    /// @brief Copying is disabled: the publisher owns the transport handle.
    E2ePublisher(const E2ePublisher&) = delete;
    /// @brief Copy assignment is disabled: the publisher owns the transport handle.
    /// @return This publisher.
    E2ePublisher& operator=(const E2ePublisher&) = delete;
    /// @brief Move-construct a publisher, transferring the transport handle.
    E2ePublisher(E2ePublisher&&) noexcept = default;
    /// @brief Move-assign a publisher, transferring the transport handle.
    /// @return This publisher.
    E2ePublisher& operator=(E2ePublisher&&) noexcept = default;

    /// @brief Seal and publish one payload.
    ///
    /// Writes the payload directly into the shared-memory slot (one copy, no
    /// intermediate envelope), seals there, and hands the slot over.
    ///
    /// @tparam U Payload type used for the default-initialization check;
    ///         defaults to `T`.
    /// @param payload Application payload to copy into the loaned slot.
    /// @return Empty status on success, or the mapped loan/send error.
    /// @note Spec `com/e2e`, requirements "Zero-copy E2E ports" and "Sender
    ///       counter progression"; allocation-free after creation.
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
    ///
    /// @tparam U Payload type used for the default-initialization check;
    ///         defaults to `T`.
    /// @return A pending handle whose `payload()` is writable in place, or the
    ///         mapped loan error.
    /// @note Spec `com/e2e`, requirement "Zero-copy E2E ports"; the returned
    ///       handle must be sent or destroyed before the publisher.
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
    /// @return `E2ePublisher(profile=..., data_id=..., next_counter=...)`.
    /// @note Spec `com/e2e`, requirement "Sender counter progression".
    [[nodiscard]] std::string to_string() const {
        return std::format("E2ePublisher(profile={}, data_id={}, next_counter={})",
                           nexigen::com::to_string(config_.profile), config_.data_id,
                           next_counter_);
    }

private:
    /// @brief Adopt an existing enveloped publisher and its configuration.
    /// @param inner Wrapped `com::Publisher` instance.
    /// @param config E2E profile, data id, and counter tolerance.
    E2ePublisher(Publisher<E2eMessage<T>> inner, E2eConfig config) noexcept
        : inner_(std::move(inner)), config_(config) {}

    Publisher<E2eMessage<T>> inner_; ///< Envelope publisher.
    E2eConfig config_;               ///< Service configuration.
    std::uint32_t next_counter_{1};  ///< Next sender counter.
};

/// @brief Free helper mirroring `E2ePublisher::to_string()` (mandate 9).
///
/// @tparam T Payload type.
/// @param publisher Publisher to render.
/// @return The diagnostic string.
/// @note Spec `com/e2e`, requirement "Zero-copy E2E ports".
template <concepts::Message T>
[[nodiscard]] std::string to_string(const E2ePublisher<T>& publisher) {
    return publisher.to_string();
}

} // namespace nexigen::com

/// @brief `std::formatter` for the E2E publisher (project mandate 11).
template <nexigen::com::concepts::Message T>
struct std::formatter<nexigen::com::E2ePublisher<T>> : std::formatter<std::string_view> {
    /// @brief Write the publisher diagnostic into the format context.
    /// @tparam Ctx `std::format` context type.
    /// @param publisher Publisher to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Spec `com/e2e`, requirement "Zero-copy E2E ports".
    template <class Ctx>
    auto format(const nexigen::com::E2ePublisher<T>& publisher, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(publisher.to_string(), ctx);
    }
};
