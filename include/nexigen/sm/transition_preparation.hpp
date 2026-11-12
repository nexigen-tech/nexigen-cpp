// nexigen::sm — prepare-for-transition handshake (change add-sm; spec sm/ipc
// "Prepare-for-transition handshake"; design D2).
//
// Before commanding the EM to stop processes, the SM publishes a
// TransitionPreparation(TargetState) pre-notification (publish-subscribe,
// trivially copyable payload) and lets key services acknowledge during
// preparation_timeout; the final EM command follows on acks or on timeout.
#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>

namespace nexigen::sm {

/// @brief Maximum length of the target-state buffer in the pre-notification.
///
/// The buffer includes its terminating NUL, so a state name is stored as at
/// most 31 bytes.
inline constexpr std::size_t k_preparation_state_length = 32;

/// @brief The published pre-notification payload (trivially copyable).
///
/// Carries only the upcoming target state in a fixed inline buffer, which is
/// what lets subscribers receive it zero-copy through the com publish-subscribe
/// transport. The SM still owns the timeout and the final EM command.
///
/// @note Spec `sm/ipc`, requirement "Prepare-for-transition handshake".
struct TransitionPreparationPayload {
    char target_state[k_preparation_state_length]{}; ///< NUL-terminated target state.

    /// @brief Render the payload as a diagnostic string.
    ///
    /// @return Text naming the target state; allocates through `std::string`
    ///         and `std::format`.
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("TransitionPreparationPayload(target_state={})",
                           std::string(target_state));
    }
};

/// @brief Compile-time proof that the payload satisfies `Message`.
///
/// The concept requires plain bytes and trivially copyable storage, so the
/// pre-notification can travel zero-copy.
static_assert(com::concepts::Message<TransitionPreparationPayload>);

/// @brief Free diagnostic helper mirroring
///        `TransitionPreparationPayload::to_string`.
///
/// @param payload Payload to render.
/// @return The member `to_string()` output.
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const TransitionPreparationPayload& payload) {
    return payload.to_string();
}

/// @brief Publishes pre-transition notifications for one service.
///
/// Owns one publisher for `TransitionPreparationPayload`; `notify()` announces
/// the target state so key services can quiesce and acknowledge within the
/// group's `preparation_timeout_ms`. The SM sends its final execution_manager
/// command after the acknowledgements arrive or the timeout expires. The type
/// is move-only and confined to the arbitration thread.
///
/// @note Spec `sm/ipc`, requirement "Prepare-for-transition handshake".
class TransitionPreparation {
public:
    /// @brief Open (or create) the preparation channel on the node.
    ///
    /// @param node Node owning the publisher.
    /// @param name Service name of the preparation channel.
    /// @return The notifier by value on success, or the publisher creation
    ///         error; never calls `value()` on a failed result.
    /// @note Spec `sm/ipc`, requirement "Prepare-for-transition handshake".
    [[nodiscard]] static com::Result<TransitionPreparation>
    create(com::Node& node, const com::ServiceName& name) noexcept {
        auto publisher = com::Publisher<TransitionPreparationPayload>::create(node, name);
        if (!publisher) {
            return std::unexpected(publisher.error());
        }
        return TransitionPreparation{name, std::move(*publisher)};
    }

    /// @brief Move the service name and publisher to a new owner.
    TransitionPreparation(TransitionPreparation&&) noexcept = default;

    /// @brief Move-assign the service name and publisher.
    TransitionPreparation& operator=(TransitionPreparation&&) noexcept = default;

    /// @brief Publish a pre-notification naming the upcoming target state.
    ///
    /// Loans a sample, writes the payload, and sends it. The name is copied as
    /// a C string and truncated to `k_preparation_state_length - 1` bytes.
    ///
    /// @param target_state NUL-terminated state name to announce.
    /// @return Empty `com::Status` when the sample was sent, or the loan/send
    ///         error.
    /// @warning `target_state` must be NUL-terminated because it is formatted
    ///          as a C string.
    /// @note Spec `sm/ipc`, requirement "Prepare-for-transition handshake"
    ///       (services acknowledge before the switch, or the timeout forces
    ///       it).
    [[nodiscard]] com::Status notify(std::string_view target_state) noexcept {
        TransitionPreparationPayload payload{};
        std::snprintf(payload.target_state, sizeof(payload.target_state), "%s",
                      target_state.data());
        auto loaned = publisher_.loan_uninit();
        if (!loaned) {
            return std::unexpected(loaned.error());
        }
        auto sample = loaned->write_payload(std::move(payload));
        return publisher_.send(std::move(sample));
    }

    /// @brief Render the notifier as a diagnostic string.
    ///
    /// @return Text naming the service; allocates.
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("TransitionPreparation(service={})", service_name_.to_string());
    }

private:
    /// @brief Take ownership of an open channel.
    ///
    /// Private so the only construction path is `create()`.
    ///
    /// @param name Service name retained for diagnostics.
    /// @param publisher Open preparation publisher.
    TransitionPreparation(com::ServiceName name,
                          com::Publisher<TransitionPreparationPayload> publisher) noexcept
        : service_name_(std::move(name)), publisher_(std::move(publisher)) {}

    com::ServiceName service_name_;                          ///< Preparation channel name.
    com::Publisher<TransitionPreparationPayload> publisher_; ///< Pre-notification publisher.
};

/// @brief Free diagnostic helper mirroring `TransitionPreparation::to_string`.
///
/// @param preparation Notifier to render.
/// @return The member `to_string()` output.
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const TransitionPreparation& preparation) {
    return preparation.to_string();
}

} // namespace nexigen::sm

/// @brief `std::formatter` for `TransitionPreparation` (project mandate 11).
///
/// Delegates to `std::formatter<std::string_view>` after rendering the member
/// diagnostic, so `std::format("{}", preparation)` equals `to_string()`.
template <>
struct std::formatter<nexigen::sm::TransitionPreparation> : std::formatter<std::string_view> {
    /// @brief Format the notifier as its diagnostic string.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param preparation Notifier to render.
    /// @param ctx Output iterator context receiving the text.
    /// @return Iterator past the written characters.
    template <class Ctx>
    auto format(const nexigen::sm::TransitionPreparation& preparation, Ctx& ctx) const {
        const std::string text = preparation.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
