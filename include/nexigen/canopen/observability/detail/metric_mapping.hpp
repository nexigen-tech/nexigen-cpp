// nexigen::canopen::observability::detail — outcome-to-metric mapping (change
// add-canopen-observability; spec canopen/observability; design D1/D3/D6).
//
// The runtime observes structured core outcomes (`PdoDecodeStatus`, frame
// routing, SDO lifecycle) and maps them to exactly one Tier 1 instrument. These
// helpers keep that mapping in one place so every call site records the same
// counters, the drop total always moves with exactly one cause, and the
// metric-first ordering of the hot-error path cannot be inverted by accident.
//
// PRE-CONDITION: every helper takes a fully registered `RuntimeMetrics`
// (`RuntimeMetrics::register_metrics()` returned a value). A partially
// registered set must never reach these functions; design D8 fails startup
// instead of running a partially instrumented bus.
#pragma once

#include <cstdint>

#include <nexigen/canopen/observability/can_open_error_domain.hpp>
#include <nexigen/canopen/observability/can_open_runtime_state.hpp>
#include <nexigen/canopen/observability/detail/error_gate.hpp>
#include <nexigen/canopen/observability/detail/runtime_metrics.hpp>
#include <nexigen/canopen/pdo_decode_result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>

namespace nexigen::canopen::observability::detail {

// --- loop metrics ------------------------------------------------------------

/// @brief Count one completed runtime cycle.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 loop metrics".
inline void record_cycle(RuntimeMetrics& metrics) noexcept {
    metrics.cycles_total->inc();
}

/// @brief Count one cycle that started after its deadline.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 loop metrics".
inline void record_cycle_overrun(RuntimeMetrics& metrics) noexcept {
    metrics.cycle_overruns_total->inc();
}

/// @brief Record one loop-latency sample in nanoseconds.
///
/// @param metrics Registered Tier 1 instruments.
/// @param latency_ns Elapsed cycle time in nanoseconds.
/// @note Hot path; spec "Tier 1 loop metrics".
inline void record_loop_latency(RuntimeMetrics& metrics, std::uint64_t latency_ns) noexcept {
    metrics.loop_latency_ns->record(static_cast<double>(latency_ns));
}

/// @brief Publish the current runtime state into `canopen_state`.
///
/// @param metrics Registered Tier 1 instruments.
/// @param state New lifecycle state.
/// @note Cold-edge path; spec "Tier 1 loop metrics".
inline void set_runtime_state(RuntimeMetrics& metrics, CanOpenRuntimeState state) noexcept {
    static_cast<void>(metrics.state->set(to_string(state)));
}

// --- traffic metrics ---------------------------------------------------------

/// @brief Count one frame read from the transport.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 traffic metrics".
inline void record_frame_received(RuntimeMetrics& metrics) noexcept {
    metrics.frames_received_total->inc();
}

/// @brief Count one successfully handled frame.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 traffic metrics".
inline void record_frame_processed(RuntimeMetrics& metrics) noexcept {
    metrics.frames_processed_total->inc();
}

/// @brief Count one frame that had no COB-ID route.
///
/// The caller must not also call `record_frame_processed()` for the same frame:
/// an unrouted frame is not processed and not dropped.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 traffic metrics".
inline void record_frame_unrouted(RuntimeMetrics& metrics) noexcept {
    metrics.frames_unrouted_total->inc();
}

/// @brief Count one received PDO frame.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 traffic metrics".
inline void record_pdo_rx(RuntimeMetrics& metrics) noexcept {
    metrics.pdo_rx_total->inc();
}

/// @brief Count one transmitted PDO frame.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 traffic metrics".
inline void record_pdo_tx(RuntimeMetrics& metrics) noexcept {
    metrics.pdo_tx_total->inc();
}

/// @brief Count one frame accepted by the transport for transmission.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path.
inline void record_tx_frame(RuntimeMetrics& metrics) noexcept {
    metrics.tx_frames_total->inc();
}

/// @brief Count one failed transmit attempt.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path.
inline void record_tx_failure(RuntimeMetrics& metrics) noexcept {
    metrics.tx_failures_total->inc();
}

// --- error metrics -----------------------------------------------------------

/// @brief Count one frame dropped because the ingress queue overflowed.
///
/// Moves `canopen_frames_dropped_total` and exactly one cause counter, so the
/// total equals the sum of the causes.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 error metrics".
inline void record_rx_overflow_drop(RuntimeMetrics& metrics) noexcept {
    metrics.frames_dropped_total->inc();
    metrics.rx_overflow_total->inc();
}

/// @brief Count one frame dropped because the transmit queue was full.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 error metrics".
inline void record_tx_queue_full_drop(RuntimeMetrics& metrics) noexcept {
    metrics.frames_dropped_total->inc();
    metrics.tx_queue_full_total->inc();
}

/// @brief Count one frame dropped by the driver.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 error metrics".
inline void record_driver_drop(RuntimeMetrics& metrics) noexcept {
    metrics.frames_dropped_total->inc();
    metrics.driver_drop_total->inc();
}

/// @brief Map one PDO decode outcome onto its Tier 1 error counter.
///
/// `Partial` counts a decode error; `UnknownObject` counts a mapping error; the
/// two never move together. `Ok` and `Invalid` increment neither.
///
/// @param metrics Registered Tier 1 instruments.
/// @param status Outcome returned by the core decoder.
/// @note Hot path; spec "Tier 1 error metrics".
inline void record_pdo_decode(RuntimeMetrics& metrics, canopen::PdoDecodeStatus status) noexcept {
    if (status == canopen::PdoDecodeStatus::Partial) {
        metrics.pdo_decode_errors_total->inc();
        return;
    }
    if (status == canopen::PdoDecodeStatus::UnknownObject) {
        metrics.pdo_mapping_errors_total->inc();
    }
}

/// @brief Count one SDO request started by the runtime.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path.
inline void record_sdo_request(RuntimeMetrics& metrics) noexcept {
    metrics.sdo_requests_total->inc();
}

/// @brief Count one observed SDO abort.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 error metrics".
inline void record_sdo_abort(RuntimeMetrics& metrics) noexcept {
    metrics.sdo_aborts_total->inc();
}

/// @brief Count one segmented SDO transfer that expired.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 error metrics".
inline void record_sdo_timeout(RuntimeMetrics& metrics) noexcept {
    metrics.sdo_timeouts_total->inc();
}

/// @brief Count one heartbeat timeout episode.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 error metrics".
inline void record_heartbeat_miss(RuntimeMetrics& metrics) noexcept {
    metrics.heartbeat_misses_total->inc();
}

/// @brief Count one NMT state transition.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Cold-edge path.
inline void record_nmt_transition(RuntimeMetrics& metrics) noexcept {
    metrics.nmt_transitions_total->inc();
}

/// @brief Count one received emergency object.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path.
inline void record_emcy(RuntimeMetrics& metrics) noexcept {
    metrics.emcy_total->inc();
}

/// @brief Count one bus-off entry.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path; spec "Tier 1 error metrics".
inline void record_bus_off(RuntimeMetrics& metrics) noexcept {
    metrics.bus_off_total->inc();
}

// --- signal metrics ----------------------------------------------------------

/// @brief Count one accepted signal update.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path.
inline void record_signal_update(RuntimeMetrics& metrics) noexcept {
    metrics.signal_updates_total->inc();
}

/// @brief Count one signal observation rejected as stale.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path.
inline void record_signal_stale(RuntimeMetrics& metrics) noexcept {
    metrics.signal_stale_total->inc();
}

/// @brief Count one signal observation rejected as out of range.
///
/// @param metrics Registered Tier 1 instruments.
/// @note Hot path.
inline void record_signal_out_of_range(RuntimeMetrics& metrics) noexcept {
    metrics.signal_out_of_range_total->inc();
}

// --- saturation gauges -------------------------------------------------------

/// @brief Publish the current ingress queue depth.
///
/// @param metrics Registered Tier 1 instruments.
/// @param depth Frames currently queued.
/// @note Spec "Tier 1 saturation metrics".
inline void set_rx_queue_depth(RuntimeMetrics& metrics, std::int64_t depth) noexcept {
    metrics.rx_queue_depth->set(depth);
}

/// @brief Publish the current transmit queue depth.
///
/// @param metrics Registered Tier 1 instruments.
/// @param depth Frames currently queued.
/// @note Spec "Tier 1 saturation metrics".
inline void set_tx_queue_depth(RuntimeMetrics& metrics, std::int64_t depth) noexcept {
    metrics.tx_queue_depth->set(depth);
}

/// @brief Publish the number of in-flight segmented SDO transfers.
///
/// @param metrics Registered Tier 1 instruments.
/// @param count Active transfers.
/// @note Spec "Tier 1 saturation metrics".
inline void set_sdo_active_transfers(RuntimeMetrics& metrics, std::int64_t count) noexcept {
    metrics.sdo_active_transfers->set(count);
}

/// @brief Publish the number of online nodes.
///
/// @param metrics Registered Tier 1 instruments.
/// @param count Online nodes.
/// @note Spec "Tier 1 saturation metrics".
inline void set_nodes_online(RuntimeMetrics& metrics, std::int64_t count) noexcept {
    metrics.nodes_online->set(count);
}

// --- hot-error composition ---------------------------------------------------

/// @brief Record one hot-path error: metric first, then the episode gate.
///
/// This is the only supported way to combine the two Tier 1 surfaces. The
/// counter is incremented before any level or suppression check, so a filtered
/// or suppressed record never hides the metric.
///
/// @tparam Logger Logger satisfying `logging::concepts::Logger`.
/// @param metric Error counter owned by this event.
/// @param gate Episode gate owned by the runtime.
/// @param domain Subsystem that produced the error.
/// @param node Node identifier; the gate uses the low seven bits.
/// @param code Domain-specific error code.
/// @param now_ns Monotonic timestamp in nanoseconds.
/// @return The gate's action for this occurrence.
/// @note Hot path; spec "Hot-path errors always increment a metric" and
///       "Hot-path error logging follows episode policy".
template <logging::concepts::Logger Logger>
[[nodiscard]] typename ErrorGate<Logger>::ErrorAction
record_hot_error(MultiThreadedCounter& metric, ErrorGate<Logger>& gate, CanOpenErrorDomain domain,
                 std::uint8_t node, std::uint32_t code, std::uint64_t now_ns) noexcept {
    metric.inc();
    return gate.on_error(domain, node, code, now_ns);
}

} // namespace nexigen::canopen::observability::detail
