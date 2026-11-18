// nexigen::synchro — cold-path serialization of endpoint statistics and the
// per-event context (change add-synchro-event-sync; spec synchro/protocol).
#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/synchro/sync_config.hpp>
#include <nexigen/synchro/sync_consumer_stats.hpp>
#include <nexigen/synchro/sync_context.hpp>
#include <nexigen/synchro/sync_generator_stats.hpp>

namespace nexigen::synchro {

std::string SyncGeneratorStats::to_string() const {
    return std::format(
        "SyncGeneratorStats(cycles={}, phases={}, overrun_slots={}, overrun_episodes={}, "
        "send_failures={}, notify_failures={}, start_ns={}, last_phase_ns={}, last_jitter_ns={}, "
        "max_jitter_ns={})",
        cycles_completed, phases_published, overrun_slots, overrun_episodes, send_failures,
        notify_failures, start_time_ns, last_phase_time_ns, last_jitter_ns, max_jitter_ns);
}

void to_json(nlohmann::json& j, const SyncGeneratorStats& stats) {
    j = nlohmann::json{{"cycles_completed", stats.cycles_completed},
                       {"phases_published", stats.phases_published},
                       {"overrun_slots", stats.overrun_slots},
                       {"overrun_episodes", stats.overrun_episodes},
                       {"send_failures", stats.send_failures},
                       {"notify_failures", stats.notify_failures},
                       {"start_time_ns", stats.start_time_ns},
                       {"last_phase_time_ns", stats.last_phase_time_ns},
                       {"last_jitter_ns", stats.last_jitter_ns},
                       {"max_jitter_ns", stats.max_jitter_ns}};
}

void from_json(const nlohmann::json& j, SyncGeneratorStats& stats) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "cycles_completed", stats.cycles_completed);
    core::detail::read_unsigned(j, "phases_published", stats.phases_published);
    core::detail::read_unsigned(j, "overrun_slots", stats.overrun_slots);
    core::detail::read_unsigned(j, "overrun_episodes", stats.overrun_episodes);
    core::detail::read_unsigned(j, "send_failures", stats.send_failures);
    core::detail::read_unsigned(j, "notify_failures", stats.notify_failures);
    core::detail::read_unsigned(j, "start_time_ns", stats.start_time_ns);
    core::detail::read_unsigned(j, "last_phase_time_ns", stats.last_phase_time_ns);
    core::detail::read_signed(j, "last_jitter_ns", stats.last_jitter_ns);
    core::detail::read_unsigned(j, "max_jitter_ns", stats.max_jitter_ns);
}

std::string SyncConsumerStats::to_string() const {
    return std::format(
        "SyncConsumerStats(received={}, dropped_late={}, late_delivered={}, transport_lost={}, "
        "generator_skips={}, duplicates={}, unexpected_resets={}, producer_restarts={}, "
        "resyncs={}, sync_losses={}, last_ipc_ns={}, last_e2e_ns={}, max_e2e_ns={}, "
        "last_sequence={}, last_epoch={}, absolute_phase={})",
        received, dropped_late, late_delivered, transport_lost, generator_skips, duplicates,
        unexpected_resets, producer_restarts, resyncs, sync_losses, last_ipc_latency_ns,
        last_e2e_latency_ns, max_e2e_latency_ns, last_sequence, last_epoch, absolute_phase);
}

void to_json(nlohmann::json& j, const SyncConsumerStats& stats) {
    j = nlohmann::json{{"received", stats.received},
                       {"dropped_late", stats.dropped_late},
                       {"late_delivered", stats.late_delivered},
                       {"transport_lost", stats.transport_lost},
                       {"generator_skips", stats.generator_skips},
                       {"duplicates", stats.duplicates},
                       {"unexpected_resets", stats.unexpected_resets},
                       {"producer_restarts", stats.producer_restarts},
                       {"resyncs", stats.resyncs},
                       {"sync_losses", stats.sync_losses},
                       {"last_ipc_latency_ns", stats.last_ipc_latency_ns},
                       {"last_e2e_latency_ns", stats.last_e2e_latency_ns},
                       {"max_e2e_latency_ns", stats.max_e2e_latency_ns},
                       {"last_sequence", stats.last_sequence},
                       {"last_epoch", stats.last_epoch},
                       {"absolute_phase", stats.absolute_phase}};
}

void from_json(const nlohmann::json& j, SyncConsumerStats& stats) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "received", stats.received);
    core::detail::read_unsigned(j, "dropped_late", stats.dropped_late);
    core::detail::read_unsigned(j, "late_delivered", stats.late_delivered);
    core::detail::read_unsigned(j, "transport_lost", stats.transport_lost);
    core::detail::read_unsigned(j, "generator_skips", stats.generator_skips);
    core::detail::read_unsigned(j, "duplicates", stats.duplicates);
    core::detail::read_unsigned(j, "unexpected_resets", stats.unexpected_resets);
    core::detail::read_unsigned(j, "producer_restarts", stats.producer_restarts);
    core::detail::read_unsigned(j, "resyncs", stats.resyncs);
    core::detail::read_unsigned(j, "sync_losses", stats.sync_losses);
    core::detail::read_signed(j, "last_ipc_latency_ns", stats.last_ipc_latency_ns);
    core::detail::read_signed(j, "last_e2e_latency_ns", stats.last_e2e_latency_ns);
    core::detail::read_unsigned(j, "max_e2e_latency_ns", stats.max_e2e_latency_ns);
    core::detail::read_unsigned(j, "last_sequence", stats.last_sequence);
    core::detail::read_unsigned(j, "last_epoch", stats.last_epoch);
    core::detail::read_unsigned(j, "absolute_phase", stats.absolute_phase);
}

std::string SyncContext::to_string() const {
    return std::format(
        "SyncContext(cycle={}, phase={}/{}, scheduled_ns={}, cycle_period_ns={}, publish_ns={}, "
        "receive_ns={}, sequence={}, epoch={}, ipc_latency_ns={}, e2e_latency_ns={}, "
        "is_late={})",
        cycle_number, phase_index, phases_total, scheduled_time_ns, cycle_period_ns,
        publish_time_ns, receive_time_ns, sequence_id, producer_epoch, ipc_latency_ns,
        e2e_latency_ns, is_late);
}

void to_json(nlohmann::json& j, const SyncContext& context) {
    j = nlohmann::json{{"cycle_number", context.cycle_number},
                       {"phase_index", context.phase_index},
                       {"phases_total", context.phases_total},
                       {"scheduled_time_ns", context.scheduled_time_ns},
                       {"cycle_period_ns", context.cycle_period_ns},
                       {"publish_time_ns", context.publish_time_ns},
                       {"receive_time_ns", context.receive_time_ns},
                       {"sequence_id", context.sequence_id},
                       {"producer_epoch", context.producer_epoch},
                       {"ipc_latency_ns", context.ipc_latency_ns},
                       {"e2e_latency_ns", context.e2e_latency_ns},
                       {"is_late", context.is_late}};
}

void from_json(const nlohmann::json& j, SyncContext& context) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "cycle_number", context.cycle_number);
    core::detail::read_unsigned(j, "phase_index", context.phase_index);
    core::detail::read_unsigned(j, "phases_total", context.phases_total);
    core::detail::read_unsigned(j, "scheduled_time_ns", context.scheduled_time_ns);
    core::detail::read_unsigned(j, "cycle_period_ns", context.cycle_period_ns);
    core::detail::read_unsigned(j, "publish_time_ns", context.publish_time_ns);
    core::detail::read_unsigned(j, "receive_time_ns", context.receive_time_ns);
    core::detail::read_unsigned(j, "sequence_id", context.sequence_id);
    core::detail::read_unsigned(j, "producer_epoch", context.producer_epoch);
    core::detail::read_signed(j, "ipc_latency_ns", context.ipc_latency_ns);
    core::detail::read_signed(j, "e2e_latency_ns", context.e2e_latency_ns);
    core::detail::read_bool(j, "is_late", context.is_late);
}

} // namespace nexigen::synchro
