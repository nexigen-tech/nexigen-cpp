// nexigen::com — E2E receiver supervision (change add-e2e-protection).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/com/e2e/e2e_codec.hpp>
#include <nexigen/com/e2e/e2e_config.hpp>
#include <nexigen/com/e2e/e2e_state.hpp>
#include <nexigen/com/e2e/e2e_status.hpp>

namespace nexigen::com {

/// @brief Receiver-side E2E check with counter supervision.
///
/// Integrity is validated before the counter, so a corrupt or foreign message
/// never advances sequence state. Counters are exposed for diagnostics.
///
/// @note Spec `com/e2e`, requirement "Receiver supervision".
class E2eReceiver {
public:
    /// @brief Bind the receiver to one service configuration.
    ///
    /// @param profile Selected protection profile.
    /// @param data_id Expected service identity.
    /// @param max_delta_counter Largest accepted forward counter gap; `0` is
    ///        clamped to `1`.
    /// @note Spec `com/e2e`, requirement "Receiver supervision".
    E2eReceiver(E2eProfile profile, std::uint32_t data_id, std::uint32_t max_delta_counter) noexcept
        : profile_(profile), data_id_(data_id),
          max_delta_(max_delta_counter == 0U ? 1U : max_delta_counter) {}

    /// @brief Classify one received envelope.
    ///
    /// Integrity is checked before the counter, so a corrupt or foreign message
    /// never advances the accepted sequence.
    ///
    /// @tparam T Payload type satisfying `com::concepts::Message`.
    /// @param message Envelope to check.
    /// @return `Ok`, `Repeated`, `WrongSequence`, or `Error` for this message.
    /// @note Spec `com/e2e`, requirement "Receiver supervision"; updates
    ///       `state()` and the accepted counter on success.
    template <concepts::Message T>
    [[nodiscard]] E2eStatus accept(const E2eMessage<T>& message) noexcept {
        ++state_.checked;
        if (profile_ == E2eProfile::None) {
            return E2eStatus::Ok;
        }
        if (!e2e::check_message(message, profile_, data_id_)) {
            ++state_.failed;
            return E2eStatus::Error;
        }
        const std::uint32_t counter = message.header.counter & e2e::counter_mask(profile_);
        if (!have_last_) {
            have_last_ = true;
            last_ = counter;
            return E2eStatus::Ok;
        }
        if (counter == last_) {
            ++state_.repeated;
            return E2eStatus::Repeated;
        }
        const std::uint32_t delta = (counter - last_) & e2e::counter_mask(profile_);
        if (delta <= max_delta_) {
            last_ = counter;
            return E2eStatus::Ok;
        }
        ++state_.wrong_sequence;
        return E2eStatus::WrongSequence;
    }

    /// @brief Counters accumulated by this receiver.
    /// @return Borrowed per-endpoint counters.
    /// @note Spec `com/e2e`, requirement "Receiver supervision".
    [[nodiscard]] const E2eState& state() const noexcept { return state_; }

    /// @brief Cold-path diagnostic rendering.
    /// @return `E2eReceiver(profile=..., data_id=..., E2eState(...))`.
    /// @note Spec `com/e2e`, requirement "Receiver supervision".
    [[nodiscard]] std::string to_string() const {
        return std::format("E2eReceiver(profile={}, data_id={}, {})",
                           nexigen::com::to_string(profile_), data_id_, state_.to_string());
    }

private:
    E2eProfile profile_{E2eProfile::None}; ///< Selected profile.
    std::uint32_t data_id_{0};             ///< Expected data id.
    std::uint32_t max_delta_{1};           ///< Largest accepted counter gap.
    bool have_last_{false};                ///< `last_` is valid.
    std::uint32_t last_{0};                ///< Last accepted counter.
    E2eState state_{};                     ///< Endpoint counters.
};

/// @brief Free helper mirroring `E2eReceiver::to_string()` (mandate 9).
///
/// @param receiver Receiver to render.
/// @return The diagnostic string.
/// @note Spec `com/e2e`, requirement "Receiver supervision".
[[nodiscard]] inline std::string to_string(const E2eReceiver& receiver) {
    return receiver.to_string();
}

} // namespace nexigen::com
