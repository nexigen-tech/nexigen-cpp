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

/// @brief Sends recovery requests to the Execution Manager and fault
///        notifications to the State Manager.
///
/// Owns three transport handles created on the caller's `com::Node`:
///   * a request-response client for `RecoveryRequest`/`RecoveryResponse`
///     (`RestartProcess` and `TriggerWatchdogReset` stay a direct PHM->EM
///     conversation);
///   * a publisher plus a wake event for `sm::RecoveryNotification`, used by
///     `SafeState` escalation so the State Manager arbitrates the fault and
///     commands the EM through `nexigen::exec::StateClient`.
///
/// The handle is movable but not copyable and must not outlive its node. The
/// direct EM calls block for up to their timeout; SM escalation performs a
/// non-blocking publish plus a wake event. `to_string` is a cold-path
/// diagnostic.
///
/// @note Spec `phm/recovery`, requirements "Local process recovery" and
///       "SafeState escalation" (HLR-SM-030/REQ_SM_007).
class RecoveryClient {
public:
    /// @brief Open every recovery channel used by the daemon.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the returned client.
    /// @param em_channel_name Request-response service shared with the
    ///        Execution Manager.
    /// @param sm_notification_name Publish-subscribe service carrying
    ///        `sm::RecoveryNotification` samples.
    /// @param sm_event_name Event service used to wake the State Manager's
    ///        WaitSet.
    /// @return The constructed client, or the first mapped `com::Error` from
    ///         creating the EM client, the SM publisher, or the SM event
    ///         notifier.
    /// @note Spec `phm/recovery`, requirements "Local process recovery" and
    ///       "SafeState escalation".
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

    /// @brief Copy construction is deleted: the client owns transport handles.
    RecoveryClient(const RecoveryClient&) = delete;

    /// @brief Copy assignment is deleted: the client owns transport handles.
    RecoveryClient& operator=(const RecoveryClient&) = delete;

    /// @brief Move-construct a client, transferring every transport handle.
    ///
    /// The moved-from client no longer owns a channel and must not send.
    RecoveryClient(RecoveryClient&&) noexcept = default;

    /// @brief Move-assign a client, transferring every transport handle.
    ///
    /// @return `*this`.
    RecoveryClient& operator=(RecoveryClient&&) noexcept = default;

    /// @brief Ask the EM to forcibly restart a failed process (direct channel).
    ///
    /// Builds a `RestartProcess` request and blocks for the EM response.
    ///
    /// @param pid Target process id passed through to the EM.
    /// @param name Target process name; copied with `snprintf`, so it is
    ///        truncated to fit `k_recovery_name_length`.
    /// @param timeout Upper bound on the request-response round trip.
    /// @return The EM's `RecoveryResponse`, or the mapped `com::Error` from the
    ///         request-response call (for example `Timeout` or
    ///         `ConnectionFailure`).
    /// @note Spec `phm/recovery`, requirement "Local process recovery": the
    ///       request identifies the process and is delivered without a
    ///       serialization step.
    /// @warning `name` must be NUL-terminated within the viewed range, because
    ///          the field is filled with `%s`.
    [[nodiscard]] com::Result<RecoveryResponse>
    restart_process(std::uint32_t pid, std::string_view name,
                    std::chrono::milliseconds timeout) noexcept {
        RecoveryRequest request;
        request.action = RecoveryAction::RestartProcess;
        request.pid = pid;
        std::snprintf(request.name, sizeof(request.name), "%s", name.data());
        return send(request, timeout);
    }

    /// @brief Escalate: trigger the hardware watchdog reset (direct channel).
    ///
    /// @param timeout Upper bound on the request-response round trip.
    /// @return The EM's `RecoveryResponse`, or the mapped `com::Error` from the
    ///         request-response call.
    /// @note Spec `phm/recovery`, requirement "SafeState escalation": the
    ///       daemon may trigger a hardware watchdog reset when escalation
    ///       cannot be handled in software.
    [[nodiscard]] com::Result<RecoveryResponse>
    trigger_watchdog_reset(std::chrono::milliseconds timeout) noexcept {
        RecoveryRequest request;
        request.action = RecoveryAction::TriggerWatchdogReset;
        return send(request, timeout);
    }

    /// @brief Escalate a critical fault to the State Manager.
    ///
    /// Publishes the `RecoveryNotification` for the failed Function Group and
    /// wakes the SM's WaitSet (HLR-SM-030). The SM arbitrates the fault and
    /// commands the SafeState switch through the `StateClient`; this client
    /// only requests the escalation. `last_action()` becomes
    /// `SwitchFunctionGroupState` once both the sample and the wake event were
    /// delivered.
    ///
    /// @param function_group Failed Function Group; copied with `snprintf`,
    ///        so it is truncated to fit the notification payload.
    /// @return Empty `com::Status` when the notification was published and the
    ///         wake event was sent, or the mapped `com::Error` from the loan,
    ///         the publish, or the notify step.
    /// @note Spec `phm/recovery`, requirement "SafeState escalation"
    ///       (HLR-SM-030/REQ_SM_007): the notification is an event over the
    ///       `com` transport.
    /// @warning `function_group` must be NUL-terminated within the viewed
    ///          range, because the field is filled with `%s`.
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

    /// @brief Apply the first configured recovery rule of an expired process.
    ///
    /// Dispatches on `process.recovery_rules.front().action`: `RestartProcess`
    /// calls `restart_process`, `SwitchFunctionGroupState` calls
    /// `notify_sm_recovery` (falling back to the process name when the rule has
    /// no function group), and `TriggerWatchdogReset` calls
    /// `trigger_watchdog_reset`. A process with no rules escalates its name to
    /// the State Manager as the safety-critical default.
    ///
    /// @param process Expired process whose first rule selects the action.
    /// @param timeout Upper bound on a direct EM request-response call;
    ///        ignored by the SM escalation path.
    /// @return Empty `com::Status` when the selected action succeeded, the
    ///         mapped `com::Error` it propagated, or `InvalidArgument` for an
    ///         action outside the documented vocabulary.
    /// @note Spec `phm/recovery`, requirements "Local process recovery" and
    ///       "SafeState escalation".
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

    /// @brief The last action this client sent.
    ///
    /// @return The action of the last successful send or SM escalation;
    ///         `RestartProcess` until the first success.
    /// @note Spec `phm/recovery`, requirement "Recovery diagnostics".
    [[nodiscard]] RecoveryAction last_action() const noexcept { return last_action_; }

    /// @brief Cold-path diagnostic naming the EM channel and last action.
    ///
    /// @return Text of the form
    ///         `RecoveryClient(em_channel=<name>, last_action=<action>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `phm/recovery`, requirement "Recovery diagnostics".
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryClient(em_channel={}, last_action={})", em_channel_.to_string(),
                           nexigen::phm::to_string(last_action_));
    }

private:
    /// @brief Adopt the opened recovery channels.
    ///
    /// @param em_channel EM service name retained for `to_string()`.
    /// @param client Request-response client for the EM.
    /// @param sm_notifier Publisher of `sm::RecoveryNotification` samples.
    /// @param sm_event Wake event for the State Manager.
    /// @note Private by design: the only supported construction path is
    ///       `create()`.
    RecoveryClient(com::ServiceName em_channel,
                   com::Client<RecoveryRequest, RecoveryResponse> client,
                   com::Publisher<nexigen::sm::RecoveryNotification> sm_notifier,
                   com::EventNotifier sm_event) noexcept
        : em_channel_(std::move(em_channel)), client_(std::move(client)),
          sm_notifier_(std::move(sm_notifier)), sm_event_(std::move(sm_event)) {}

    /// @brief Send one request and record the action on success.
    ///
    /// @param request Request payload to deliver.
    /// @param timeout Upper bound on the round trip.
    /// @return The EM's response, or the mapped `com::Error`.
    /// @note Spec `phm/recovery`, requirement "Local process recovery".
    com::Result<RecoveryResponse> send(const RecoveryRequest& request,
                                       std::chrono::milliseconds timeout) noexcept {
        auto response = client_.call(request, timeout);
        if (response.has_value()) {
            last_action_ = request.action;
        }
        return response;
    }

    com::ServiceName em_channel_;                                   ///< EM channel name.
    com::Client<RecoveryRequest, RecoveryResponse> client_;         ///< Direct PHM->EM client.
    com::Publisher<nexigen::sm::RecoveryNotification> sm_notifier_; ///< SM notification publisher.
    com::EventNotifier sm_event_;                                   ///< SM WaitSet wake event.
    RecoveryAction last_action_{RecoveryAction::RestartProcess};    ///< Last accepted action.
};

/// @brief Free helper mirroring `RecoveryClient::to_string()` (mandate 9).
///
/// @param recovery Client to render.
/// @return The client's diagnostic text.
/// @note Mandate 9; allocates, so cold path only. Spec `phm/recovery`,
///       requirement "Recovery diagnostics".
[[nodiscard]] inline std::string to_string(const RecoveryClient& recovery) {
    return recovery.to_string();
}

} // namespace nexigen::phm

/// @brief Formats a `RecoveryClient` as its diagnostic string.
///
/// Delegates to the member `to_string`, so `std::format("{}", recovery)` equals
/// the member output (naming the last recovery action).
///
/// @note Mandate 11 (universal `std::formatter`); cold path only. Spec
///       `phm/recovery`, requirement "Recovery diagnostics".
template <> struct std::formatter<nexigen::phm::RecoveryClient> : std::formatter<std::string_view> {
    /// @brief Write the client's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param recovery Client to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::phm::RecoveryClient& recovery, Ctx& ctx) const {
        const std::string text = recovery.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
