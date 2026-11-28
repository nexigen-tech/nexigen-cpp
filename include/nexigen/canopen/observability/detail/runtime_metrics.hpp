// nexigen::canopen::observability::detail — Tier 1 metric registration (change
// add-canopen-observability; spec canopen/observability; design D3/D8).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/metrics/counter.hpp>
#include <nexigen/metrics/explicit_histogram.hpp>
#include <nexigen/metrics/gauge.hpp>
#include <nexigen/metrics/policy_enums.hpp>
#include <nexigen/metrics/state_gauge.hpp>

namespace nexigen::metrics {

class Meter;

} // namespace nexigen::metrics

namespace nexigen::canopen::observability::detail {

/// @brief Tier 1 counter instrument, always multi-threaded and cumulative.
using MultiThreadedCounter = metrics::Counter<metrics::ConcurrencyPolicy::MultiThreaded>;

/// @brief Tier 1 gauge instrument, always multi-threaded and cumulative.
using MultiThreadedGauge = metrics::Gauge<std::int64_t, metrics::ConcurrencyPolicy::MultiThreaded>;

/// @brief Tier 1 state gauge, always multi-threaded and cumulative.
using MultiThreadedStateGauge = metrics::StateGauge<metrics::ConcurrencyPolicy::MultiThreaded>;

/// @brief Tier 1 explicit histogram, always multi-threaded and cumulative.
using MultiThreadedHistogram =
    metrics::ExplicitBucketHistogram<metrics::ConcurrencyPolicy::MultiThreaded>;

/// @brief First instrument that could not be registered.
///
/// Carries the provider error plus the static metric name, so startup can log
/// exactly which instrument failed before the transport is opened.
///
/// @note Spec `canopen/observability`, requirement "Startup registration
///       validates names and capacity".
struct RegistrationFailure {
    core::ErrorCode code{core::ErrorCode::Ok}; ///< Mapped provider error.
    std::string_view instrument{};             ///< Static metric name, or empty.

    /// @brief Cold-path diagnostic string.
    ///
    /// @return `RegistrationFailure(code=..., instrument=...)`.
    /// @note Mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Registered Tier 1 instruments of one bus meter.
///
/// One pointer per catalogue entry. Every instrument is created with
/// `ConcurrencyPolicy::MultiThreaded`, `AccessPolicy::Readable`, and
/// `ResetPolicy::Cumulative`, so a background harvester may read while the
/// runtime thread records and every record aggregates instead of resetting.
///
/// Catalogue and units per bus:
///
/// - Counters (25, unit `"1"`): `canopen_cycles_total`,
///   `canopen_cycle_overruns_total`, `canopen_frames_received_total`,
///   `canopen_frames_processed_total`, `canopen_frames_unrouted_total`,
///   `canopen_frames_dropped_total`, `canopen_rx_overflow_total`,
///   `canopen_tx_queue_full_total`, `canopen_driver_drop_total`,
///   `canopen_pdo_rx_total`, `canopen_pdo_tx_total`,
///   `canopen_pdo_decode_errors_total`, `canopen_pdo_mapping_errors_total`,
///   `canopen_sdo_requests_total`, `canopen_sdo_aborts_total`,
///   `canopen_sdo_timeouts_total`, `canopen_nmt_transitions_total`,
///   `canopen_heartbeat_misses_total`, `canopen_emcy_total`,
///   `canopen_signal_updates_total`, `canopen_signal_stale_total`,
///   `canopen_signal_out_of_range_total`, `canopen_tx_frames_total`,
///   `canopen_tx_failures_total`, `canopen_bus_off_total`.
/// - Gauges (4): `canopen_rx_queue_depth` (`"frames"`),
///   `canopen_tx_queue_depth` (`"frames"`), `canopen_sdo_active_transfers`
///   (`"transfers"`), `canopen_nodes_online` (`"nodes"`).
/// - State gauge (1): `canopen_state` (no unit; value is a
///   `CanOpenRuntimeState` spelling).
/// - Explicit histogram (1): `canopen_loop_latency_ns` (`"ns"`).
///
/// `canopen_frames_dropped_total` is a total: the runtime increments it
/// together with exactly one of `canopen_rx_overflow_total`,
/// `canopen_tx_queue_full_total`, or `canopen_driver_drop_total`.
///
/// @note Spec `canopen/observability`, requirement "Tier 1 loop metrics".
/// @note Spec `canopen/observability`, requirement "Tier 1 traffic metrics".
/// @note Spec `canopen/observability`, requirement "Tier 1 error metrics".
/// @note Spec `canopen/observability`, requirement "Tier 1 saturation metrics".
struct RuntimeMetrics {
    /// @brief Number of Tier 1 counters.
    static constexpr std::size_t counter_count = 25U;
    /// @brief Number of Tier 1 gauges.
    static constexpr std::size_t gauge_count = 4U;

    /// @brief Completed runtime cycles.
    MultiThreadedCounter* cycles_total{nullptr};
    /// @brief Cycles that started after their deadline.
    MultiThreadedCounter* cycle_overruns_total{nullptr};
    /// @brief Frames read from the transport.
    MultiThreadedCounter* frames_received_total{nullptr};
    /// @brief Frames handled successfully.
    MultiThreadedCounter* frames_processed_total{nullptr};
    /// @brief Frames with no COB-ID route.
    MultiThreadedCounter* frames_unrouted_total{nullptr};
    /// @brief Total dropped frames; equals the sum of the three cause counters.
    MultiThreadedCounter* frames_dropped_total{nullptr};
    /// @brief Frames dropped because the ingress queue overflowed.
    MultiThreadedCounter* rx_overflow_total{nullptr};
    /// @brief Frames dropped because the transmit queue was full.
    MultiThreadedCounter* tx_queue_full_total{nullptr};
    /// @brief Frames dropped by the driver.
    MultiThreadedCounter* driver_drop_total{nullptr};
    /// @brief PDO frames received.
    MultiThreadedCounter* pdo_rx_total{nullptr};
    /// @brief PDO frames transmitted.
    MultiThreadedCounter* pdo_tx_total{nullptr};
    /// @brief PDO payload decode failures.
    MultiThreadedCounter* pdo_decode_errors_total{nullptr};
    /// @brief PDO mapping failures (`UnknownObject` outcomes).
    MultiThreadedCounter* pdo_mapping_errors_total{nullptr};
    /// @brief SDO requests started.
    MultiThreadedCounter* sdo_requests_total{nullptr};
    /// @brief SDO aborts observed.
    MultiThreadedCounter* sdo_aborts_total{nullptr};
    /// @brief Segmented SDO transfers that expired.
    MultiThreadedCounter* sdo_timeouts_total{nullptr};
    /// @brief NMT state transitions observed.
    MultiThreadedCounter* nmt_transitions_total{nullptr};
    /// @brief Heartbeat timeout episodes started.
    MultiThreadedCounter* heartbeat_misses_total{nullptr};
    /// @brief Emergency objects received.
    MultiThreadedCounter* emcy_total{nullptr};
    /// @brief Signal values updated.
    MultiThreadedCounter* signal_updates_total{nullptr};
    /// @brief Signal observations rejected as stale.
    MultiThreadedCounter* signal_stale_total{nullptr};
    /// @brief Signal observations rejected as out of range.
    MultiThreadedCounter* signal_out_of_range_total{nullptr};
    /// @brief Frames accepted by the transport for transmission.
    MultiThreadedCounter* tx_frames_total{nullptr};
    /// @brief Transmit attempts that failed.
    MultiThreadedCounter* tx_failures_total{nullptr};
    /// @brief Bus-off entries observed.
    MultiThreadedCounter* bus_off_total{nullptr};

    /// @brief Current ingress queue depth.
    MultiThreadedGauge* rx_queue_depth{nullptr};
    /// @brief Current transmit queue depth.
    MultiThreadedGauge* tx_queue_depth{nullptr};
    /// @brief Segmented SDO transfers in flight.
    MultiThreadedGauge* sdo_active_transfers{nullptr};
    /// @brief Nodes currently considered online.
    MultiThreadedGauge* nodes_online{nullptr};

    /// @brief Current runtime state (a `CanOpenRuntimeState` spelling).
    MultiThreadedStateGauge* state{nullptr};

    /// @brief Loop latency samples in nanoseconds.
    MultiThreadedHistogram* loop_latency_ns{nullptr};

    /// @brief Create every Tier 1 instrument on one bus meter.
    ///
    /// Registration is a one-time cold-path step performed before the transport
    /// is opened. On the first failure it stops, leaves the already-created
    /// instruments in place (there is no unregister in the provider), and
    /// returns the mapped error with the failing metric name.
    ///
    /// Provider `BufferFull` (pool exhausted) maps to `ResourceExhausted`;
    /// duplicate or invalid names stay `InvalidArgument`.
    ///
    /// @param meter Bus meter that must outlive the returned handles.
    /// @return The registered handle set, or the first failure.
    /// @note Spec `canopen/observability`, requirement "Startup registration
    ///       validates names and capacity"; design D8.
    [[nodiscard]] static core::Result<RuntimeMetrics, RegistrationFailure>
    register_metrics(metrics::Meter& meter);
};

/// @brief Free helper mirroring `RegistrationFailure::to_string()`.
///
/// @param failure Failure to render.
/// @return The same text as the member.
/// @note Mandate 9.
[[nodiscard]] inline std::string to_string(const RegistrationFailure& failure) {
    return failure.to_string();
}

} // namespace nexigen::canopen::observability::detail
