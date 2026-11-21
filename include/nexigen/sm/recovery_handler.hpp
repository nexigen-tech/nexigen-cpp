// nexigen::sm — PHM recovery handler (change add-sm; reworked by
// complete-phm-safety-loop; spec sm/recovery-handler).
//
// The SM-side receiver of PHM fault notifications. The PHM daemon publishes a
// RecoveryNotification (request id + function-group name, trivially copyable)
// and raises the paired event; the SM's WaitSet wakes on the event and
// handle_pending() drains the notifications, commits the group state, hands the
// transition to the Execution Manager, and answers every notification with a
// RecoveryAck. Nothing is polled.
#pragma once

#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <utility>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/phm/recovery_ack.hpp>
#include <nexigen/sm/recovery_notification.hpp>
#include <nexigen/sm/state_manager.hpp>

namespace nexigen::sm {

/// @brief SM-side handler of PHM fault events.
///
/// Owns the recovery-notification subscription, the acknowledgement publisher,
/// both wake events, and a non-owning pointer to the state manager. The PHM
/// daemon publishes a notification and raises the event; the SM attaches
/// `listener()` to its WaitSet and calls `handle_pending()` only after that
/// wake.
///
/// The actuation callback is installed by the owner: it publishes the committed
/// state and commands the Execution Manager, returning the acknowledgement the
/// SM must forward. Without it the SM cannot take a group over and answers
/// `EmUnavailable`.
///
/// The type is move-only because it owns com handles, and it must be confined
/// to the SM's arbitration thread: `handle_pending()` mutates the state manager
/// directly.
///
/// @note Spec `sm/recovery-handler`, requirement "PHM fault-event interface".
class RecoveryHandler {
public:
    /// @brief Actuate one recovery: publish the state and command the EM.
    ///
    /// @param group_index Function Group index in manifest order.
    /// @param request_id Correlation id of the notification being handled.
    /// @param target_state `SafeState` or `DegradedMode`.
    /// @return The acknowledgement the SM forwards to PHM.
    using ActuationFn = std::function<nexigen::phm::RecoveryAck(
        std::size_t group_index, std::uint64_t request_id, std::string_view target_state)>;

    /// @brief Open the recovery channel, the acknowledgement channel, and their
    ///        wake events.
    ///
    /// @param node Node owning every endpoint.
    /// @param channel_name Service name of the recovery notification channel.
    /// @param event_name Service name of the paired wake event.
    /// @param ack_name Service name of the acknowledgement channel.
    /// @param ack_event_name Service name of the acknowledgement wake event.
    /// @param manager State manager mutated by `handle_pending()`; must outlive
    ///        the handler.
    /// @return The handler by value on success, or the first transport error;
    ///         never calls `value()` on a failed result.
    /// @note Spec `sm/recovery-handler`, requirements "PHM fault-event
    ///       interface" and "Recovery acknowledgement event".
    [[nodiscard]] static com::Result<RecoveryHandler>
    create(com::Node& node, const com::ServiceName& channel_name,
           const com::ServiceName& event_name, const com::ServiceName& ack_name,
           const com::ServiceName& ack_event_name, StateManager& manager) noexcept {
        auto subscriber = com::Subscriber<RecoveryNotification>::create(node, channel_name);
        if (!subscriber) {
            return std::unexpected(subscriber.error());
        }
        auto listener = com::EventListener::create(node, event_name);
        if (!listener) {
            return std::unexpected(listener.error());
        }
        auto publisher = com::Publisher<nexigen::phm::RecoveryAck>::create(node, ack_name, 64);
        if (!publisher) {
            return std::unexpected(publisher.error());
        }
        auto ack_event = com::EventNotifier::create(node, ack_event_name);
        if (!ack_event) {
            return std::unexpected(ack_event.error());
        }
        return RecoveryHandler{channel_name,          std::move(*subscriber), std::move(*listener),
                               std::move(*publisher), std::move(*ack_event),  &manager};
    }

    /// @brief Move the com handles and the manager pointer.
    RecoveryHandler(RecoveryHandler&&) noexcept = default;

    /// @brief Move-assign the com handles and the manager pointer.
    RecoveryHandler& operator=(RecoveryHandler&&) noexcept = default;

    /// @brief Install the actuation callback used for every notification.
    ///
    /// @param actuation Callback that publishes the state and commands the EM.
    void set_actuation(ActuationFn actuation) noexcept { actuation_ = std::move(actuation); }

    /// @brief Drain pending fault notifications, actuate, and acknowledge.
    ///
    /// Receives until the channel is empty. Each named group the manifest knows
    /// is committed to `SafeState`, or `DegradedMode` when `SafeState` is not
    /// declared, published, and handed to the actuation callback. Every drained
    /// notification is answered with exactly one acknowledgement carrying its
    /// request id; an unknown group is acknowledged `Rejected`/`UnknownGroup`
    /// rather than dropped.
    ///
    /// @warning Call only after the WaitSet woke on the recovery event — never
    ///          as a polling loop. Runs on the arbitration thread.
    /// @note Spec `sm/recovery-handler`, requirements "PHM fault-event
    ///       interface" and "Recovery acknowledgement event".
    void handle_pending() noexcept {
        for (;;) {
            auto received = subscriber_.receive();
            if (!received || !received->has_value()) {
                break;
            }
            const RecoveryNotification& notification = (*received)->payload();
            const std::size_t index = manager_->find_group_index(notification.function_group);
            if (index == npos) {
                publish_ack(
                    ack_for(notification.request_id, nexigen::phm::RecoveryReason::UnknownGroup));
                continue;
            }
            std::string_view target{};
            if (manager_->has_state(index, "SafeState")) {
                target = "SafeState";
            } else if (manager_->has_state(index, "DegradedMode")) {
                target = "DegradedMode";
            }
            if (target.empty()) {
                publish_ack(
                    ack_for(notification.request_id, nexigen::phm::RecoveryReason::NoSafeState));
                continue;
            }
            manager_->force_state(index, target);
            nexigen::phm::RecoveryAck ack =
                actuation_
                    ? actuation_(index, notification.request_id, target)
                    : ack_for(notification.request_id, nexigen::phm::RecoveryReason::EmUnavailable);
            ack.request_id = notification.request_id;
            publish_ack(ack);
            last_group_ = manager_->group_name(index);
        }
    }

    /// @brief The wake-event listener the SM attaches to its WaitSet.
    ///
    /// @return Mutable reference to the handler-owned listener.
    [[nodiscard]] com::EventListener& listener() noexcept { return listener_; }

    /// @brief The wake-event listener the SM attaches to its WaitSet.
    ///
    /// @return Const reference to the handler-owned listener.
    [[nodiscard]] const com::EventListener& listener() const noexcept { return listener_; }

    /// @brief The last acknowledgement this handler published.
    ///
    /// @return The acknowledgement, or a default `Accepted`/`None` before the
    ///         first fault event.
    /// @note Spec `sm/recovery-handler`, requirement "Recovery diagnostics".
    [[nodiscard]] const nexigen::phm::RecoveryAck& last_ack() const noexcept { return last_ack_; }

    /// @brief Render the handler state as a diagnostic string.
    ///
    /// @return Text naming the channel, the last handled group, and the last
    ///         acknowledgement; allocates.
    /// @note Spec `sm/recovery-handler`, requirement "Recovery diagnostics".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryHandler(channel={}, last_group={}, last_ack={})",
                           channel_name_.to_string(), last_group_, last_ack_.to_string());
    }

private:
    /// @brief Sentinel index meaning "group not found".
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    /// @brief Build a rejection acknowledgement with the given reason.
    [[nodiscard]] static nexigen::phm::RecoveryAck
    ack_for(std::uint64_t request_id, nexigen::phm::RecoveryReason reason) noexcept {
        return nexigen::phm::RecoveryAck{request_id, nexigen::phm::RecoveryOutcome::Rejected,
                                         reason};
    }

    /// @brief Publish one acknowledgement and wake its consumer.
    void publish_ack(const nexigen::phm::RecoveryAck& ack) noexcept {
        last_ack_ = ack;
        if (ack_publisher_.send_copy(ack)) {
            static_cast<void>(ack_event_.notify());
        }
    }

    /// @brief Take ownership of the channel endpoints and manager pointer.
    ///
    /// Private so the only construction path is `create()`.
    RecoveryHandler(com::ServiceName channel_name, com::Subscriber<RecoveryNotification> subscriber,
                    com::EventListener listener,
                    com::Publisher<nexigen::phm::RecoveryAck> ack_publisher,
                    com::EventNotifier ack_event, StateManager* manager) noexcept
        : channel_name_(std::move(channel_name)), subscriber_(std::move(subscriber)),
          listener_(std::move(listener)), ack_publisher_(std::move(ack_publisher)),
          ack_event_(std::move(ack_event)), manager_(manager) {}

    com::ServiceName channel_name_;                           ///< Recovery channel name.
    com::Subscriber<RecoveryNotification> subscriber_;        ///< Fault-notification subscription.
    com::EventListener listener_;                             ///< Paired WaitSet wake event.
    com::Publisher<nexigen::phm::RecoveryAck> ack_publisher_; ///< Acknowledgement publisher.
    com::EventNotifier ack_event_;                            ///< Acknowledgement wake event.
    StateManager* manager_{nullptr};                          ///< Non-owning state manager.
    ActuationFn actuation_{};                                 ///< Installed actuation callback.
    std::string last_group_{};             ///< Last group recovered, for diagnostics.
    nexigen::phm::RecoveryAck last_ack_{}; ///< Last acknowledgement, for diagnostics.
};

/// @brief Free diagnostic helper mirroring `RecoveryHandler::to_string`.
///
/// @param handler Handler to render.
/// @return The member `to_string()` output.
/// @note Spec `sm/recovery-handler`, requirement "Recovery diagnostics".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const RecoveryHandler& handler) {
    return handler.to_string();
}

} // namespace nexigen::sm

/// @brief `std::formatter` for `RecoveryHandler` (project mandate 11).
///
/// Delegates to `std::formatter<std::string_view>` after rendering the member
/// diagnostic, so `std::format("{}", handler)` equals `to_string()`.
///
/// @note Spec `sm/recovery-handler`, requirement "Recovery diagnostics".
template <> struct std::formatter<nexigen::sm::RecoveryHandler> : std::formatter<std::string_view> {
    /// @brief Format the handler as its diagnostic string.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param handler Handler to render.
    /// @param ctx Output iterator context receiving the text.
    /// @return Iterator past the written characters.
    template <class Ctx> auto format(const nexigen::sm::RecoveryHandler& handler, Ctx& ctx) const {
        const std::string text = handler.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
