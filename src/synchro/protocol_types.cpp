// nexigen::synchro — cold-path serialization of the protocol vocabulary
// (change add-synchro-event-sync; spec synchro/protocol).
//
// Every diagnostic and JSON hook lives here so the headers stay free of string
// and nlohmann dependencies on the hot path. Readers are strictly non-throwing
// and leave the destination untouched on hostile input.
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/com/channel_config.hpp>
#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/synchro/past_target_policy.hpp>
#include <nexigen/synchro/start_mode.hpp>
#include <nexigen/synchro/start_policy.hpp>
#include <nexigen/synchro/sync_channel_config.hpp>
#include <nexigen/synchro/sync_config.hpp>
#include <nexigen/synchro/sync_consumer_config.hpp>
#include <nexigen/synchro/sync_error.hpp>
#include <nexigen/synchro/sync_payload.hpp>

namespace nexigen::synchro {

std::string SyncPayload::to_string() const {
    return std::format("SyncPayload(cycle={}, phase={}/{}, scheduled_ns={}, cycle_period_ns={})",
                       cycle_number, phase_index, phases_total, scheduled_time_ns,
                       cycle_period_ns);
}

void to_json(nlohmann::json& j, const SyncPayload& payload) {
    j = nlohmann::json{{"cycle_number", payload.cycle_number},
                       {"phase_index", payload.phase_index},
                       {"phases_total", payload.phases_total},
                       {"scheduled_time_ns", payload.scheduled_time_ns},
                       {"cycle_period_ns", payload.cycle_period_ns}};
}

void from_json(const nlohmann::json& j, SyncPayload& payload) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "cycle_number", payload.cycle_number);
    core::detail::read_unsigned(j, "phase_index", payload.phase_index);
    core::detail::read_unsigned(j, "phases_total", payload.phases_total);
    core::detail::read_unsigned(j, "scheduled_time_ns", payload.scheduled_time_ns);
    core::detail::read_unsigned(j, "cycle_period_ns", payload.cycle_period_ns);
}

std::string SyncError::to_string() const {
    return std::format("SyncError(kind={}, transport={}, core={}, os_errno={})",
                       nexigen::synchro::to_string(kind), com::to_string(transport),
                       core::to_string(core), os_errno);
}

void to_json(nlohmann::json& j, const SyncError& error) {
    j = nlohmann::json{
        {"kind", std::string(nexigen::synchro::to_string(error.kind))},
        {"transport", std::string(com::to_string(error.transport))},
        {"core", std::string(core::to_string(error.core))},
        {"os_errno", error.os_errno},
    };
}

void from_json(const nlohmann::json& j, SyncError& error) noexcept {
    if (!j.is_object()) {
        return;
    }
    std::string text;
    if (core::detail::read_string(j, "kind", text)) {
        SyncErrorKind parsed = error.kind;
        if (from_string(text, parsed)) {
            error.kind = parsed;
        }
    }
    if (core::detail::read_string(j, "transport", text)) {
        com::Error parsed = error.transport;
        if (com::from_string(text, parsed)) {
            error.transport = parsed;
        }
    }
    if (core::detail::read_string(j, "core", text)) {
        core::ErrorCode parsed = error.core;
        if (core::from_string(text, parsed)) {
            error.core = parsed;
        }
    }
    core::detail::read_signed(j, "os_errno", error.os_errno);
}

core::Status SyncChannelConfig::validate() const noexcept {
    if (service_name.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const com::Status channel_valid = channel.validate();
    if (!channel_valid.has_value()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (cycle_period_ns == 0U || phases_per_cycle == 0U) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (phase_period_ns() < min_phase_period_ns) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

std::string SyncChannelConfig::to_string() const {
    return std::format(
        "SyncChannelConfig(service={}, cycle_period_ns={}, phases_per_cycle={}, "
        "phase_period_ns={}, channel={})",
        service_name, cycle_period_ns, phases_per_cycle, phase_period_ns(), channel.to_string());
}

void to_json(nlohmann::json& j, const SyncChannelConfig& config) {
    j = nlohmann::json{{"service_name", config.service_name},
                       {"channel", config.channel},
                       {"cycle_period_ns", config.cycle_period_ns},
                       {"phases_per_cycle", config.phases_per_cycle}};
}

void from_json(const nlohmann::json& j, SyncChannelConfig& config) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_string(j, "service_name", config.service_name);
    core::detail::read_object(j, "channel",
                              [&config](const nlohmann::json& inner) { from_json(inner, config.channel); });
    core::detail::read_unsigned(j, "cycle_period_ns", config.cycle_period_ns);
    core::detail::read_unsigned(j, "phases_per_cycle", config.phases_per_cycle);
}

core::Status StartPolicy::validate() const noexcept {
    switch (mode) {
    case StartMode::NextCycle:
    case StartMode::AtTarget:
        break;
    default:
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    switch (past_target) {
    case PastTargetPolicy::Reject:
    case PastTargetPolicy::RealignToNextCycle:
        break;
    default:
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

std::string StartPolicy::to_string() const {
    return std::format("StartPolicy(mode={}, target_time_ns={}, align_target_to_cycle={}, "
                       "past_target={})",
                       nexigen::synchro::to_string(mode), target_time_ns, align_target_to_cycle,
                       nexigen::synchro::to_string(past_target));
}

void to_json(nlohmann::json& j, const StartPolicy& policy) {
    j = nlohmann::json{{"mode", std::string(nexigen::synchro::to_string(policy.mode))},
                       {"target_time_ns", policy.target_time_ns},
                       {"align_target_to_cycle", policy.align_target_to_cycle},
                       {"past_target", std::string(nexigen::synchro::to_string(policy.past_target))}};
}

void from_json(const nlohmann::json& j, StartPolicy& policy) noexcept {
    if (!j.is_object()) {
        return;
    }
    std::string text;
    if (core::detail::read_string(j, "mode", text)) {
        StartMode parsed = policy.mode;
        if (from_string(text, parsed)) {
            policy.mode = parsed;
        }
    }
    core::detail::read_unsigned(j, "target_time_ns", policy.target_time_ns);
    core::detail::read_bool(j, "align_target_to_cycle", policy.align_target_to_cycle);
    if (core::detail::read_string(j, "past_target", text)) {
        PastTargetPolicy parsed = policy.past_target;
        if (from_string(text, parsed)) {
            policy.past_target = parsed;
        }
    }
}

core::Status SyncConfig::validate() const noexcept {
    const core::Status channel_valid = channel.validate();
    if (!channel_valid.has_value()) {
        return std::unexpected(channel_valid.error());
    }
    const core::Status policy_valid = start.validate();
    if (!policy_valid.has_value()) {
        return std::unexpected(policy_valid.error());
    }
    const std::uint64_t phase_period = channel.phase_period_ns();
    if (phase_period == 0U || spin_threshold_ns >= phase_period) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const core::Status thread_valid = thread.validate();
    if (!thread_valid.has_value()) {
        return std::unexpected(thread_valid.error());
    }
    return {};
}

std::string SyncConfig::to_string() const {
    return std::format("SyncConfig(channel={}, start={}, spin_threshold_ns={}, event_code={}, "
                       "thread={})",
                       channel.to_string(), start.to_string(), spin_threshold_ns, event_code,
                       thread.to_string());
}

void to_json(nlohmann::json& j, const SyncConfig& config) {
    j = nlohmann::json{{"channel", config.channel},
                       {"start", config.start},
                       {"spin_threshold_ns", config.spin_threshold_ns},
                       {"event_code", config.event_code},
                       {"thread", config.thread}};
}

void from_json(const nlohmann::json& j, SyncConfig& config) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_object(j, "channel",
                              [&config](const nlohmann::json& inner) { from_json(inner, config.channel); });
    core::detail::read_object(j, "start",
                              [&config](const nlohmann::json& inner) { from_json(inner, config.start); });
    core::detail::read_unsigned(j, "spin_threshold_ns", config.spin_threshold_ns);
    core::detail::read_unsigned(j, "event_code", config.event_code);
    core::detail::read_object(j, "thread",
                              [&config](const nlohmann::json& inner) { from_json(inner, config.thread); });
}

core::Status SyncConsumerConfig::validate() const noexcept {
    const core::Status channel_valid = channel.validate();
    if (!channel_valid.has_value()) {
        return std::unexpected(channel_valid.error());
    }
    switch (late_policy) {
    case LateEventPolicy::DropAndWarn:
    case LateEventPolicy::DeliverWithFlag:
        break;
    default:
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

std::string SyncConsumerConfig::to_string() const {
    return std::format("SyncConsumerConfig(channel={}, max_allowed_latency_ns={}, late_policy={}, "
                       "sync_loss_timeout_ns={}, initial_sync_timeout_ns={})",
                       channel.to_string(), max_allowed_latency_ns,
                       nexigen::synchro::to_string(late_policy), sync_loss_timeout_ns,
                       initial_sync_timeout_ns);
}

void to_json(nlohmann::json& j, const SyncConsumerConfig& config) {
    j = nlohmann::json{
        {"channel", config.channel},
        {"max_allowed_latency_ns", config.max_allowed_latency_ns},
        {"late_policy", std::string(nexigen::synchro::to_string(config.late_policy))},
        {"sync_loss_timeout_ns", config.sync_loss_timeout_ns},
        {"initial_sync_timeout_ns", config.initial_sync_timeout_ns},
    };
}

void from_json(const nlohmann::json& j, SyncConsumerConfig& config) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_object(j, "channel",
                              [&config](const nlohmann::json& inner) { from_json(inner, config.channel); });
    core::detail::read_unsigned(j, "max_allowed_latency_ns", config.max_allowed_latency_ns);
    std::string text;
    if (core::detail::read_string(j, "late_policy", text)) {
        LateEventPolicy parsed = config.late_policy;
        if (from_string(text, parsed)) {
            config.late_policy = parsed;
        }
    }
    core::detail::read_unsigned(j, "sync_loss_timeout_ns", config.sync_loss_timeout_ns);
    core::detail::read_unsigned(j, "initial_sync_timeout_ns", config.initial_sync_timeout_ns);
}

} // namespace nexigen::synchro
