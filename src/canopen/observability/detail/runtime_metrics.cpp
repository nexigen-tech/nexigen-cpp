// nexigen::canopen::observability::detail — Tier 1 metric registration (change
// add-canopen-observability; spec canopen/observability; design D3/D8).
#include <nexigen/canopen/observability/detail/runtime_metrics.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/metric_provider.hpp>

namespace nexigen::canopen::observability::detail {

namespace {

/// @brief One counter of the Tier 1 catalogue.
struct CounterSpec {
    std::string_view name;                          ///< Metric name.
    MultiThreadedCounter* RuntimeMetrics::* member; ///< Destination field.
};

/// @brief One gauge of the Tier 1 catalogue.
struct GaugeSpec {
    std::string_view name;                        ///< Metric name.
    std::string_view unit;                        ///< Declared unit.
    MultiThreadedGauge* RuntimeMetrics::* member; ///< Destination field.
};

/// @brief Tier 1 counters in registration order.
constexpr std::array<CounterSpec, RuntimeMetrics::counter_count> counter_specs{{
    {"canopen_cycles_total", &RuntimeMetrics::cycles_total},
    {"canopen_cycle_overruns_total", &RuntimeMetrics::cycle_overruns_total},
    {"canopen_frames_received_total", &RuntimeMetrics::frames_received_total},
    {"canopen_frames_processed_total", &RuntimeMetrics::frames_processed_total},
    {"canopen_frames_unrouted_total", &RuntimeMetrics::frames_unrouted_total},
    {"canopen_frames_dropped_total", &RuntimeMetrics::frames_dropped_total},
    {"canopen_rx_overflow_total", &RuntimeMetrics::rx_overflow_total},
    {"canopen_tx_queue_full_total", &RuntimeMetrics::tx_queue_full_total},
    {"canopen_driver_drop_total", &RuntimeMetrics::driver_drop_total},
    {"canopen_pdo_rx_total", &RuntimeMetrics::pdo_rx_total},
    {"canopen_pdo_tx_total", &RuntimeMetrics::pdo_tx_total},
    {"canopen_pdo_decode_errors_total", &RuntimeMetrics::pdo_decode_errors_total},
    {"canopen_pdo_mapping_errors_total", &RuntimeMetrics::pdo_mapping_errors_total},
    {"canopen_sdo_requests_total", &RuntimeMetrics::sdo_requests_total},
    {"canopen_sdo_aborts_total", &RuntimeMetrics::sdo_aborts_total},
    {"canopen_sdo_timeouts_total", &RuntimeMetrics::sdo_timeouts_total},
    {"canopen_nmt_transitions_total", &RuntimeMetrics::nmt_transitions_total},
    {"canopen_heartbeat_misses_total", &RuntimeMetrics::heartbeat_misses_total},
    {"canopen_emcy_total", &RuntimeMetrics::emcy_total},
    {"canopen_signal_updates_total", &RuntimeMetrics::signal_updates_total},
    {"canopen_signal_stale_total", &RuntimeMetrics::signal_stale_total},
    {"canopen_signal_out_of_range_total", &RuntimeMetrics::signal_out_of_range_total},
    {"canopen_tx_frames_total", &RuntimeMetrics::tx_frames_total},
    {"canopen_tx_failures_total", &RuntimeMetrics::tx_failures_total},
    {"canopen_bus_off_total", &RuntimeMetrics::bus_off_total},
}};

/// @brief Tier 1 gauges in registration order.
constexpr std::array<GaugeSpec, RuntimeMetrics::gauge_count> gauge_specs{{
    {"canopen_rx_queue_depth", "frames", &RuntimeMetrics::rx_queue_depth},
    {"canopen_tx_queue_depth", "frames", &RuntimeMetrics::tx_queue_depth},
    {"canopen_sdo_active_transfers", "transfers", &RuntimeMetrics::sdo_active_transfers},
    {"canopen_nodes_online", "nodes", &RuntimeMetrics::nodes_online},
}};

/// @brief Loop-latency histogram bounds in nanoseconds (sub-microsecond to 100 ms).
constexpr std::array<double, 15> latency_bounds{
    500.0,       1'000.0,     2'000.0,     5'000.0,      10'000.0,
    20'000.0,    50'000.0,    100'000.0,   200'000.0,    500'000.0,
    1'000'000.0, 2'000'000.0, 5'000'000.0, 10'000'000.0, 100'000'000.0,
};

/// @brief Map a provider error onto the runtime-facing vocabulary.
///
/// @param code Provider error.
/// @return `ResourceExhausted` for a full pool, `code` otherwise.
[[nodiscard]] core::ErrorCode map_error(core::ErrorCode code) noexcept {
    if (code == core::ErrorCode::BufferFull) {
        return core::ErrorCode::ResourceExhausted;
    }
    return code;
}

} // namespace

core::Result<RuntimeMetrics, RegistrationFailure>
RuntimeMetrics::register_metrics(metrics::Meter& meter) {
    RuntimeMetrics metrics{};

    for (const CounterSpec& spec : counter_specs) {
        auto created =
            meter.create_counter<metrics::ConcurrencyPolicy::MultiThreaded>(spec.name, "1");
        if (!created.has_value()) {
            return std::unexpected(RegistrationFailure{map_error(created.error()), spec.name});
        }
        metrics.*(spec.member) = *created;
    }

    for (const GaugeSpec& spec : gauge_specs) {
        auto created = meter.create_gauge<std::int64_t, metrics::ConcurrencyPolicy::MultiThreaded>(
            spec.name, spec.unit);
        if (!created.has_value()) {
            return std::unexpected(RegistrationFailure{map_error(created.error()), spec.name});
        }
        metrics.*(spec.member) = *created;
    }

    auto state = meter.create_state<metrics::ConcurrencyPolicy::MultiThreaded>("canopen_state", "");
    if (!state.has_value()) {
        return std::unexpected(RegistrationFailure{map_error(state.error()), "canopen_state"});
    }
    metrics.state = *state;

    auto latency = meter.create_explicit_histogram<metrics::ConcurrencyPolicy::MultiThreaded>(
        "canopen_loop_latency_ns", latency_bounds, "ns");
    if (!latency.has_value()) {
        return std::unexpected(
            RegistrationFailure{map_error(latency.error()), "canopen_loop_latency_ns"});
    }
    metrics.loop_latency_ns = *latency;

    return metrics;
}

std::string RegistrationFailure::to_string() const {
    return std::format("RegistrationFailure(code={}, instrument={})", core::to_string(code),
                       instrument);
}

} // namespace nexigen::canopen::observability::detail
