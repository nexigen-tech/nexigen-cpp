// nexigen::phm — recovery client (change add-phm; spec phm/recovery;
// design D4).
//
// The daemon's sender side of the PHM→EM recovery channel: a typed
// com::Client over the request-response transport. The EM-side responder is
// the nexigen::exec::StateClient, implemented by the later nexigen::sm
// change; until then tests run against a mock EM responder.
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
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/phm/phm_manifest.hpp>
#include <nexigen/phm/recovery_action.hpp>
#include <nexigen/phm/recovery_request.hpp>
#include <nexigen/phm/recovery_response.hpp>

namespace nexigen::phm {

/// Sends recovery requests to the Execution Manager.
class RecoveryClient {
public:
    /// Open (or create) the recovery channel service on the node.
    [[nodiscard]] static com::Result<RecoveryClient> create(
        com::Node& node, const com::ServiceName& name) noexcept {
        auto client = com::Client<RecoveryRequest, RecoveryResponse>::create(node, name);
        if (!client) {
            return std::unexpected(client.error());
        }
        return RecoveryClient{name, std::move(*client)};
    }

    RecoveryClient(const RecoveryClient&) = delete;
    RecoveryClient& operator=(const RecoveryClient&) = delete;
    RecoveryClient(RecoveryClient&&) noexcept = default;
    RecoveryClient& operator=(RecoveryClient&&) noexcept = default;

    /// Ask the EM to forcibly restart a failed process.
    [[nodiscard]] com::Result<RecoveryResponse> restart_process(
        std::uint32_t pid, std::string_view name,
        std::chrono::milliseconds timeout) noexcept {
        RecoveryRequest request;
        request.action = RecoveryAction::RestartProcess;
        request.pid = pid;
        std::snprintf(request.name, sizeof(request.name), "%s", name.data());
        return send(request, timeout);
    }

    /// Escalate: move a function group into its SafeState.
    [[nodiscard]] com::Result<RecoveryResponse> switch_function_group(
        std::string_view function_group, std::chrono::milliseconds timeout) noexcept {
        RecoveryRequest request;
        request.action = RecoveryAction::SwitchFunctionGroupState;
        std::snprintf(request.function_group, sizeof(request.function_group), "%s",
                      function_group.data());
        return send(request, timeout);
    }

    /// Escalate: trigger the hardware watchdog reset.
    [[nodiscard]] com::Result<RecoveryResponse> trigger_watchdog_reset(
        std::chrono::milliseconds timeout) noexcept {
        RecoveryRequest request;
        request.action = RecoveryAction::TriggerWatchdogReset;
        return send(request, timeout);
    }

    /// Apply the first configured recovery rule of an expired process
    /// (spec phm/recovery "Local process recovery" / "SafeState escalation").
    [[nodiscard]] com::Result<RecoveryResponse> apply_rule(
        const SupervisedProcess& process, std::chrono::milliseconds timeout) noexcept {
        if (process.recovery_rules.empty()) {
            // No configured rule: escalate the safety-critical default.
            return switch_function_group(process.name, timeout);
        }
        const RecoveryRule& rule = process.recovery_rules.front();
        switch (rule.action) {
            case RecoveryAction::RestartProcess:
                return restart_process(0, process.name, timeout);
            case RecoveryAction::SwitchFunctionGroupState:
                return switch_function_group(rule.function_group.empty() ? process.name
                                                                         : rule.function_group,
                                            timeout);
            case RecoveryAction::TriggerWatchdogReset:
                return trigger_watchdog_reset(timeout);
        }
        return std::unexpected(com::Error::InvalidArgument);
    }

    /// The last action this client sent.
    [[nodiscard]] RecoveryAction last_action() const noexcept { return last_action_; }

    /// Cold-path diagnostic (mandate 9): names the channel and last action.
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryClient(service={}, last_action={})", service_name_.to_string(),
                           nexigen::phm::to_string(last_action_));
    }

private:
    RecoveryClient(com::ServiceName name,
                   com::Client<RecoveryRequest, RecoveryResponse> client) noexcept
        : service_name_(std::move(name)), client_(std::move(client)) {}

    com::Result<RecoveryResponse> send(const RecoveryRequest& request,
                                       std::chrono::milliseconds timeout) noexcept {
        auto response = client_.call(request, timeout);
        if (response.has_value()) {
            last_action_ = request.action;
        }
        return response;
    }

    com::ServiceName service_name_;
    com::Client<RecoveryRequest, RecoveryResponse> client_;
    RecoveryAction last_action_{RecoveryAction::RestartProcess};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const RecoveryClient& recovery) {
    return recovery.to_string();
}

} // namespace nexigen::phm

/// The recovery client formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::phm::RecoveryClient> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::phm::RecoveryClient& recovery, Ctx& ctx) const {
        const std::string text = recovery.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
