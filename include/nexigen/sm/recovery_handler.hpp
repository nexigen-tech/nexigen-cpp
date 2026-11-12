// nexigen::sm — PHM recovery handler (change add-sm; spec sm/recovery-handler;
// design D2/D5).
//
// The SM-side receiver of PHM fault notifications. The PHM daemon publishes a
// RecoveryNotification (function-group name, trivially copyable) and raises
// the paired event; the SM's WaitSet wakes on the event and handle_pending()
// drains the notifications, forcing the failed group into SafeState — the
// arbitration thread never polls.
#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <utility>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/sm/recovery_notification.hpp>
#include <nexigen/sm/state_manager.hpp>

namespace nexigen::sm {

/// @brief SM-side handler of PHM fault events.
///
/// Owns the recovery-notification subscription, the paired wake event, and a
/// non-owning pointer to the state manager. The PHM daemon publishes a
/// notification and raises the event; the SM attaches `listener()` to its
/// WaitSet and calls `handle_pending()` only after that wake, which forces the
/// failed group into `SafeState` (or `DegradedMode` when `SafeState` is not
/// declared) within the one-millisecond recovery budget. Nothing is polled.
///
/// The type is move-only because it owns com handles, and it must be confined
/// to the SM's arbitration thread: `handle_pending()` mutates the state manager
/// directly.
///
/// @note Spec `sm/recovery-handler`, requirement "PHM fault-event interface".
class RecoveryHandler {
public:
    /// @brief Open the recovery notification channel and its wake event.
    ///
    /// @param node Node owning both com endpoints.
    /// @param channel_name Service name of the recovery notification channel.
    /// @param event_name Service name of the paired wake event.
    /// @param manager State manager mutated by `handle_pending()`; must outlive
    ///        the handler.
    /// @return The handler by value on success, or the first transport error
    ///         from `Subscriber::create` or `EventListener::create`; never
    ///         calls `value()` on a failed result.
    /// @note Spec `sm/recovery-handler`, requirement "PHM fault-event
    ///       interface".
    [[nodiscard]] static com::Result<RecoveryHandler> create(com::Node& node,
                                                             const com::ServiceName& channel_name,
                                                             const com::ServiceName& event_name,
                                                             StateManager& manager) noexcept {
        auto subscriber = com::Subscriber<RecoveryNotification>::create(node, channel_name);
        if (!subscriber) {
            return std::unexpected(subscriber.error());
        }
        auto listener = com::EventListener::create(node, event_name);
        if (!listener) {
            return std::unexpected(listener.error());
        }
        return RecoveryHandler{channel_name, std::move(*subscriber), std::move(*listener),
                               &manager};
    }

    /// @brief Move the com handles and the manager pointer.
    RecoveryHandler(RecoveryHandler&&) noexcept = default;

    /// @brief Move-assign the com handles and the manager pointer.
    RecoveryHandler& operator=(RecoveryHandler&&) noexcept = default;

    /// @brief Drain pending fault notifications and force recovery states.
    ///
    /// Receives until the channel is empty. Each named group that the manifest
    /// knows is forced into `SafeState`, or into `DegradedMode` when
    /// `SafeState` is not declared; unknown groups are ignored. The last
    /// handled group is retained for diagnostics.
    ///
    /// @warning Call only after the WaitSet woke on the recovery event — never
    ///          as a polling loop. Runs on the arbitration thread and bypasses
    ///          normal arbitration so the switch fits the 1 ms budget.
    /// @note Spec `sm/recovery-handler`, requirement "PHM fault-event
    ///       interface" (interrupt non-critical transitions, then recover).
    void handle_pending() noexcept {
        for (;;) {
            auto received = subscriber_.receive();
            if (!received || !received->has_value()) {
                break;
            }
            const RecoveryNotification& notification = (*received)->payload();
            const std::size_t index = manager_->find_group_index(notification.function_group);
            if (index != npos) {
                if (manager_->has_state(index, "SafeState")) {
                    manager_->force_state(index, "SafeState");
                } else if (manager_->has_state(index, "DegradedMode")) {
                    manager_->force_state(index, "DegradedMode");
                }
                last_group_ = manager_->group_name(index);
            }
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

    /// @brief Render the handler state as a diagnostic string.
    ///
    /// @return Text naming the channel and the last handled group; allocates.
    /// @note Spec `sm/recovery-handler`, requirement "Recovery diagnostics".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryHandler(channel={}, last_group={})", channel_name_.to_string(),
                           last_group_);
    }

private:
    /// @brief Sentinel index meaning "group not found".
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    /// @brief Take ownership of the channel endpoints and manager pointer.
    ///
    /// Private so the only construction path is `create()`.
    ///
    /// @param channel_name Recovery channel name retained for diagnostics.
    /// @param subscriber Open notification subscription.
    /// @param listener Open wake-event listener.
    /// @param manager Non-owning state manager pointer.
    RecoveryHandler(com::ServiceName channel_name, com::Subscriber<RecoveryNotification> subscriber,
                    com::EventListener listener, StateManager* manager) noexcept
        : channel_name_(std::move(channel_name)), subscriber_(std::move(subscriber)),
          listener_(std::move(listener)), manager_(manager) {}

    com::ServiceName channel_name_;                    ///< Recovery channel name.
    com::Subscriber<RecoveryNotification> subscriber_; ///< Fault-notification subscription.
    com::EventListener listener_;                      ///< Paired WaitSet wake event.
    StateManager* manager_{nullptr};                   ///< Non-owning state manager.
    std::string last_group_{};                         ///< Last group recovered, for diagnostics.
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
