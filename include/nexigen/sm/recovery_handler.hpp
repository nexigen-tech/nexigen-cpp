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

/// SM-side handler of PHM fault events.
class RecoveryHandler {
public:
    /// Open the recovery notification channel and its wake event.
    [[nodiscard]] static com::Result<RecoveryHandler> create(
        com::Node& node, const com::ServiceName& channel_name,
        const com::ServiceName& event_name, StateManager& manager) noexcept {
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

    RecoveryHandler(RecoveryHandler&&) noexcept = default;
    RecoveryHandler& operator=(RecoveryHandler&&) noexcept = default;

    /// Drain pending fault notifications and force each failed group into
    /// SafeState (or DegradedMode when SafeState is not declared). Call after
    /// the WaitSet woke on the recovery event — never as polling.
    void handle_pending() noexcept {
        for (;;) {
            auto received = subscriber_.receive();
            if (!received || !received->has_value()) {
                break;
            }
            const RecoveryNotification& notification = (*received)->payload();
            const std::size_t index =
                manager_->find_group_index(notification.function_group);
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

    /// The wake-event listener (the SM attaches it to its WaitSet).
    [[nodiscard]] com::EventListener& listener() noexcept { return listener_; }
    [[nodiscard]] const com::EventListener& listener() const noexcept { return listener_; }

    /// Cold-path diagnostic (mandate 9): the last fault group handled.
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryHandler(channel={}, last_group={})", channel_name_.to_string(),
                           last_group_);
    }

private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    RecoveryHandler(com::ServiceName channel_name,
                    com::Subscriber<RecoveryNotification> subscriber,
                    com::EventListener listener, StateManager* manager) noexcept
        : channel_name_(std::move(channel_name)),
          subscriber_(std::move(subscriber)),
          listener_(std::move(listener)),
          manager_(manager) {}

    com::ServiceName channel_name_;
    com::Subscriber<RecoveryNotification> subscriber_;
    com::EventListener listener_;
    StateManager* manager_{nullptr};
    std::string last_group_{};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const RecoveryHandler& handler) {
    return handler.to_string();
}

} // namespace nexigen::sm

/// The handler formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::sm::RecoveryHandler> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::sm::RecoveryHandler& handler, Ctx& ctx) const {
        const std::string text = handler.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
