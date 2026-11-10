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

/// Maximum length of the target-state buffer in the pre-notification.
inline constexpr std::size_t k_preparation_state_length = 32;

/// The published pre-notification payload (trivially copyable).
struct TransitionPreparationPayload {
    char target_state[k_preparation_state_length]{};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("TransitionPreparationPayload(target_state={})",
                           std::string(target_state));
    }
};

/// The payload contract: plain bytes, safe for zero-copy transport.
static_assert(com::concepts::Message<TransitionPreparationPayload>);

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const TransitionPreparationPayload& payload) {
    return payload.to_string();
}

/// Publishes TransitionPreparation pre-notifications for one service.
class TransitionPreparation {
public:
    /// Open (or create) the preparation channel on the node.
    [[nodiscard]] static com::Result<TransitionPreparation> create(
        com::Node& node, const com::ServiceName& name) noexcept {
        auto publisher = com::Publisher<TransitionPreparationPayload>::create(node, name);
        if (!publisher) {
            return std::unexpected(publisher.error());
        }
        return TransitionPreparation{name, std::move(*publisher)};
    }

    TransitionPreparation(TransitionPreparation&&) noexcept = default;
    TransitionPreparation& operator=(TransitionPreparation&&) noexcept = default;

    /// Publish a pre-notification naming the upcoming target state.
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

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("TransitionPreparation(service={})", service_name_.to_string());
    }

private:
    TransitionPreparation(com::ServiceName name,
                          com::Publisher<TransitionPreparationPayload> publisher) noexcept
        : service_name_(std::move(name)), publisher_(std::move(publisher)) {}

    com::ServiceName service_name_;
    com::Publisher<TransitionPreparationPayload> publisher_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const TransitionPreparation& preparation) {
    return preparation.to_string();
}

} // namespace nexigen::sm

/// The preparation notifier formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::sm::TransitionPreparation> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::sm::TransitionPreparation& preparation, Ctx& ctx) const {
        const std::string text = preparation.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
