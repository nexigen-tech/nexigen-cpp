// nexigen::phm — recovery client (change add-phm; change add-sm re-routes
// SafeState; spec phm/recovery "SafeState escalation" / "Local process
// recovery"; design D5 of add-sm).
//
// The daemon's sender side of recovery:
//   * RestartProcess / TriggerWatchdogReset stay a direct PHM→EM
//     request-response (REQ_PHM_006 — an EM concern).
//   * SafeState escalation emits a RecoveryNotification to the State Manager
//     (publish-subscribe + wake event, HLR-SM-030/REQ_SM_007); the SM
//     arbitrates and commands the EM through nexigen::exec::StateClient.
#pragma once

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/com/client.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/phm/phm_manifest.hpp>
#include <nexigen/phm/recovery_action.hpp>
#include <nexigen/phm/recovery_request.hpp>
#include <nexigen/phm/recovery_response.hpp>
#include <nexigen/sm/recovery_notification.hpp>

namespace nexigen::phm {

/// Sends recovery requests to the Execution Manager and fault notifications
/// to the State Manager.
class RecoveryClient {
public:
    /// Open the recovery channels: the EM request-response channel and the
    /// SM notification channel (publish-subscribe) with its wake event.
    [[nodiscard]] static com::Result<RecoveryClient>
    create(com::Node& node, const com::ServiceName& em_channel_name,
           const com::ServiceName& sm_notification_name,
           const com::ServiceName& sm_event_name) noexcept {
        auto client = com::Client<RecoveryRequest, RecoveryResponse>::create(node, em_channel_name);
        if (!client) {
            return std::unexpected(client.error());
        }
        auto notifier =
            com::Publisher<nexigen::sm::RecoveryNotification>::create(node, sm_notification_name);
        if (!notifier) {
            return std::unexpected(notifier.error());
        }
        auto event = com::EventNotifier::create(node, sm_event_name);
        if (!event) {
            return std::unexpected(event.error());
        }
        return RecoveryClient{em_channel_name, std::move(*client), std::move(*notifier),
                              std::move(*event)};
    }

    RecoveryClient(const RecoveryClient&) = delete;
    RecoveryClient& operator=(const RecoveryClient&) = delete;
    RecoveryClient(RecoveryClient&&) noexcept = default;
    RecoveryClient& operator=(RecoveryClient&&) noexcept = default;

    /// Ask the EM to forcibly restart a failed process (direct channel).
    [[nodiscard]] com::Result<RecoveryResponse>
    restart_process(std::uint32_t pid, std::string_view name,
                    std::chrono::milliseconds timeout) noexcept {
        RecoveryRequest request;
        request.action = RecoveryAction::RestartProcess;
        request.pid = pid;
        std::snprintf(request.name, sizeof(request.name), "%s", name.data());
        return send(request, timeout);
    }

    /// Escalate: trigger the hardware watchdog reset (direct channel).
    [[nodiscard]] com::Result<RecoveryResponse>
    trigger_watchdog_reset(std::chrono::milliseconds timeout) noexcept {
        RecoveryRequest request;
        request.action = RecoveryAction::TriggerWatchdogReset;
        return send(request, timeout);
    }

    /// Escalate a critical fault to the State Manager: publish the
    /// RecoveryNotification for the failed Function Group and wake the SM's
    /// WaitSet (HLR-SM-030). The SM arbitrates and commands the SafeState
    /// switch through the StateClient.
    [[nodiscard]] com::Status notify_sm_recovery(std::string_view function_group) noexcept {
        nexigen::sm::RecoveryNotification notification{};
        std::snprintf(notification.function_group, sizeof(notification.function_group), "%s",
                      function_group.data());
        auto loaned = sm_notifier_.loan_uninit();
        if (!loaned) {
            return std::unexpected(loaned.error());
        }
        auto sample = loaned->write_payload(std::move(notification));
        const com::Status sent = sm_notifier_.send(std::move(sample));
        if (!sent) {
            return sent;
        }
        const com::Status woken = sm_event_.notify();
        if (!woken) {
            return woken;
        }
        last_action_ = RecoveryAction::SwitchFunctionGroupState;
        return {};
    }

    /// Apply the first configured recovery rule of an expired process
    /// (spec phm/recovery "Local process recovery" / "SafeState escalation").
    [[nodiscard]] com::Status apply_rule(const SupervisedProcess& process,
                                         std::chrono::milliseconds timeout) noexcept {
        if (process.recovery_rules.empty()) {
            // No configured rule: escalate the safety-critical default to SM.
            return notify_sm_recovery(process.name);
        }
        const RecoveryRule& rule = process.recovery_rules.front();
        switch (rule.action) {
            case RecoveryAction::RestartProcess: {
                const auto response = restart_process(0, process.name, timeout);
                return response.has_value() ? com::Status{}
                                            : com::Status{std::unexpected(response.error())};
            }
            case RecoveryAction::SwitchFunctionGroupState:
                return notify_sm_recovery(rule.function_group.empty() ? process.name
                                                                      : rule.function_group);
            case RecoveryAction::TriggerWatchdogReset: {
                const auto response = trigger_watchdog_reset(timeout);
                return response.has_value() ? com::Status{}
                                            : com::Status{std::unexpected(response.error())};
            }
        }
        return std::unexpected(com::Error::InvalidArgument);
    }

    /// The last action this client sent.
    [[nodiscard]] RecoveryAction last_action() const noexcept { return last_action_; }

    /// Cold-path diagnostic (mandate 9): names the channel and last action.
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryClient(em_channel={}, last_action={})", em_channel_.to_string(),
                           nexigen::phm::to_string(last_action_));
    }

private:
    RecoveryClient(com::ServiceName em_channel,
                   com::Client<RecoveryRequest, RecoveryResponse> client,
                   com::Publisher<nexigen::sm::RecoveryNotification> sm_notifier,
                   com::EventNotifier sm_event) noexcept
        : em_channel_(std::move(em_channel)), client_(std::move(client)),
          sm_notifier_(std::move(sm_notifier)), sm_event_(std::move(sm_event)) {}

    com::Result<RecoveryResponse> send(const RecoveryRequest& request,
                                       std::chrono::milliseconds timeout) noexcept {
        auto response = client_.call(request, timeout);
        if (response.has_value()) {
            last_action_ = request.action;
        }
        return response;
    }

    com::ServiceName em_channel_;
    com::Client<RecoveryRequest, RecoveryResponse> client_;
    com::Publisher<nexigen::sm::RecoveryNotification> sm_notifier_;
    com::EventNotifier sm_event_;
    RecoveryAction last_action_{RecoveryAction::RestartProcess};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const RecoveryClient& recovery) {
    return recovery.to_string();
}

} // namespace nexigen::phm

/// The recovery client formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::phm::RecoveryClient> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::RecoveryClient& recovery, Ctx& ctx) const {
        const std::string text = recovery.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
