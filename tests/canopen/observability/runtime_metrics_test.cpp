// Tier 1 registration tests (change add-canopen-observability; spec
// canopen/observability): meter identity reservation, the exact instrument
// catalogue, and fail-fast capacity/duplicate diagnostics.
#include "observability_test_support.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/canopen/observability/detail/bus_meter_registry.hpp>
#include <nexigen/canopen/observability/detail/runtime_metrics.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/metrics/metric_kind.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace {

namespace metrics = nexigen::metrics;
namespace detail = nexigen::canopen::observability::detail;

using metrics::AccessPolicy;
using metrics::ConcurrencyPolicy;
using metrics::MetricKind;
using metrics::ResetPolicy;

static_assert(detail::MultiThreadedCounter::concurrency == ConcurrencyPolicy::MultiThreaded);
static_assert(detail::MultiThreadedCounter::access == AccessPolicy::Readable);
static_assert(detail::MultiThreadedCounter::reset == ResetPolicy::Cumulative);
static_assert(detail::MultiThreadedGauge::concurrency == ConcurrencyPolicy::MultiThreaded);
static_assert(detail::MultiThreadedGauge::access == AccessPolicy::Readable);
static_assert(detail::MultiThreadedGauge::reset == ResetPolicy::Cumulative);
static_assert(detail::MultiThreadedStateGauge::concurrency == ConcurrencyPolicy::MultiThreaded);
static_assert(detail::MultiThreadedStateGauge::access == AccessPolicy::Readable);
static_assert(detail::MultiThreadedStateGauge::reset == ResetPolicy::Cumulative);
static_assert(detail::MultiThreadedHistogram::concurrency == ConcurrencyPolicy::MultiThreaded);
static_assert(detail::MultiThreadedHistogram::access == AccessPolicy::Readable);
static_assert(detail::MultiThreadedHistogram::reset == ResetPolicy::Cumulative);

/// @brief One expected catalogue entry.
struct ExpectedMetric {
    std::string_view name; ///< Metric name.
    MetricKind kind;       ///< Instrument family.
    std::string_view unit; ///< Declared unit.
};

/// @brief The complete Tier 1 catalogue in registration order.
constexpr std::array<ExpectedMetric, 31> kExpectedCatalogue{{
    {"canopen_cycles_total", MetricKind::Counter, "1"},
    {"canopen_cycle_overruns_total", MetricKind::Counter, "1"},
    {"canopen_frames_received_total", MetricKind::Counter, "1"},
    {"canopen_frames_processed_total", MetricKind::Counter, "1"},
    {"canopen_frames_unrouted_total", MetricKind::Counter, "1"},
    {"canopen_frames_dropped_total", MetricKind::Counter, "1"},
    {"canopen_rx_overflow_total", MetricKind::Counter, "1"},
    {"canopen_tx_queue_full_total", MetricKind::Counter, "1"},
    {"canopen_driver_drop_total", MetricKind::Counter, "1"},
    {"canopen_pdo_rx_total", MetricKind::Counter, "1"},
    {"canopen_pdo_tx_total", MetricKind::Counter, "1"},
    {"canopen_pdo_decode_errors_total", MetricKind::Counter, "1"},
    {"canopen_pdo_mapping_errors_total", MetricKind::Counter, "1"},
    {"canopen_sdo_requests_total", MetricKind::Counter, "1"},
    {"canopen_sdo_aborts_total", MetricKind::Counter, "1"},
    {"canopen_sdo_timeouts_total", MetricKind::Counter, "1"},
    {"canopen_nmt_transitions_total", MetricKind::Counter, "1"},
    {"canopen_heartbeat_misses_total", MetricKind::Counter, "1"},
    {"canopen_emcy_total", MetricKind::Counter, "1"},
    {"canopen_signal_updates_total", MetricKind::Counter, "1"},
    {"canopen_signal_stale_total", MetricKind::Counter, "1"},
    {"canopen_signal_out_of_range_total", MetricKind::Counter, "1"},
    {"canopen_tx_frames_total", MetricKind::Counter, "1"},
    {"canopen_tx_failures_total", MetricKind::Counter, "1"},
    {"canopen_bus_off_total", MetricKind::Counter, "1"},
    {"canopen_rx_queue_depth", MetricKind::Gauge, "frames"},
    {"canopen_tx_queue_depth", MetricKind::Gauge, "frames"},
    {"canopen_sdo_active_transfers", MetricKind::Gauge, "transfers"},
    {"canopen_nodes_online", MetricKind::Gauge, "nodes"},
    {"canopen_state", MetricKind::State, ""},
    {"canopen_loop_latency_ns", MetricKind::ExplicitHistogram, "ns"},
}};

/// @brief Verify one harvested record against the expected catalogue entry.
/// @param sink Harvested batch.
/// @param expected Catalogue row.
void check_metric(const RecordingMetricSink& sink, const ExpectedMetric& expected) {
    const metrics::MetricRecord* record = find_metric_record(sink, expected.name);
    if (record == nullptr) {
        NX_CHECK(false && "expected metric missing from the harvest");
        return;
    }
    NX_CHECK(record->kind == expected.kind);
    NX_CHECK_EQ(metrics::detail::view_name(record->identity.unit), expected.unit);
    NX_CHECK(record->concurrency == ConcurrencyPolicy::MultiThreaded);
    NX_CHECK(record->access == AccessPolicy::Readable);
    NX_CHECK(record->reset == ResetPolicy::Cumulative);
    NX_CHECK_EQ(metrics::detail::view_name(record->identity.meter_name),
                std::string_view("canopen.can0"));
    NX_CHECK_EQ(metrics::detail::view_name(record->identity.meter_version),
                std::string_view("1.0"));
}

// --- 3.2 bus meter identity --------------------------------------------------

void test_bus_meter_reserve() {
    detail::BusMeterRegistry registry;
    auto identity = registry.reserve("can0", "1.0");
    NX_CHECK(identity.has_value());
    if (identity.has_value()) {
        NX_CHECK_EQ(identity->name, std::string("canopen.can0"));
        NX_CHECK_EQ(identity->version, std::string("1.0"));
        NX_CHECK(identity->to_string().find("canopen.can0") != std::string::npos);
    }
    NX_CHECK_EQ(registry.reserved_count(), std::size_t{1});
    NX_CHECK(registry.to_string().find("canopen.can0") != std::string::npos);
}

void test_bus_meter_rejects_invalid() {
    detail::BusMeterRegistry registry;
    NX_CHECK(!registry.reserve("", "1.0").has_value());
    NX_CHECK(
        !registry
             .reserve(std::string(detail::BusMeterRegistry::max_bus_name_length + 1U, 'b'), "1.0")
             .has_value());
    NX_CHECK(!registry.reserve("can0", "").has_value());
    NX_CHECK(
        !registry.reserve("can0", std::string(metrics::max_name_length + 1U, 'v')).has_value());
    NX_CHECK_EQ(registry.reserved_count(), std::size_t{0});

    auto first = registry.reserve("can0", "1.0");
    NX_CHECK(first.has_value());
    auto duplicate = registry.reserve("can0", "2.0");
    NX_CHECK(!duplicate.has_value());
    if (!duplicate.has_value()) {
        NX_CHECK(duplicate.error() == nexigen::core::ErrorCode::InvalidArgument);
    }
    NX_CHECK_EQ(registry.reserved_count(), std::size_t{1});
}

void test_bus_meter_release_and_capacity() {
    detail::BusMeterRegistry registry;
    NX_CHECK(registry.reserve("can0", "1.0").has_value());
    NX_CHECK(registry.release("can0"));
    NX_CHECK(!registry.release("can0"));
    NX_CHECK_EQ(registry.reserved_count(), std::size_t{0});
    NX_CHECK(registry.reserve("can0", "1.0").has_value());

    detail::BusMeterRegistry full;
    for (std::size_t index = 0U; index < detail::BusMeterRegistry::capacity; ++index) {
        NX_CHECK(full.reserve("bus" + std::to_string(index), "1.0").has_value());
    }
    auto overflow = full.reserve("one-too-many", "1.0");
    NX_CHECK(!overflow.has_value());
    if (!overflow.has_value()) {
        NX_CHECK(overflow.error() == nexigen::core::ErrorCode::ResourceExhausted);
    }
}

// --- 3.1 / 3.3 catalogue -----------------------------------------------------

void test_registration_creates_exact_catalogue() {
    RecordingMeterFixture fixture;
    NX_CHECK(fixture.meter != nullptr);
    if (fixture.meter == nullptr) {
        return;
    }
    auto registered = detail::RuntimeMetrics::register_metrics(*fixture.meter);
    NX_CHECK(registered.has_value());
    if (!registered.has_value()) {
        return;
    }
    NX_CHECK(fixture.harvest());
    NX_CHECK_EQ(fixture.sink.count, static_cast<std::uint32_t>(kExpectedCatalogue.size()));
    for (const ExpectedMetric& expected : kExpectedCatalogue) {
        check_metric(fixture.sink, expected);
    }
}

void test_registration_records_and_harvests() {
    RecordingMeterFixture fixture;
    NX_CHECK(fixture.meter != nullptr);
    if (fixture.meter == nullptr) {
        return;
    }
    auto registered = detail::RuntimeMetrics::register_metrics(*fixture.meter);
    NX_CHECK(registered.has_value());
    if (!registered.has_value()) {
        return;
    }
    registered->cycles_total->inc();
    registered->frames_dropped_total->inc();
    registered->rx_overflow_total->inc();
    registered->state->set("Running");
    registered->loop_latency_ns->record(2'500.0);
    NX_CHECK(fixture.harvest());

    const metrics::MetricRecord* cycles = find_metric_record(fixture.sink, "canopen_cycles_total");
    NX_CHECK(cycles != nullptr);
    if (cycles != nullptr) {
        NX_CHECK_EQ(cycles->scalar.counter, std::uint64_t{1});
    }
    const metrics::MetricRecord* state = find_metric_record(fixture.sink, "canopen_state");
    NX_CHECK(state != nullptr);
    if (state != nullptr) {
        NX_CHECK_EQ(std::string_view(state->state.value), std::string_view("Running"));
    }
    const metrics::MetricRecord* latency =
        find_metric_record(fixture.sink, "canopen_loop_latency_ns");
    NX_CHECK(latency != nullptr);
    if (latency != nullptr) {
        NX_CHECK_EQ(latency->histogram.count, std::uint64_t{1});
        NX_CHECK_EQ(latency->histogram.sum, 2'500.0);
    }
}

// --- 3.4 fail-fast registration ---------------------------------------------

void test_capacity_failure_names_the_instrument() {
    RecordingMetricSink sink;
    metrics::MetricProviderConfig config{};
    config.max_counters = 6U;
    metrics::MetricProvider provider{config, sink};
    auto meter = provider.get_meter("canopen.can0", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter.has_value()) {
        return;
    }

    // Startup order: register metrics, and only then open the transport.
    bool transport_opened = false;
    auto registered = detail::RuntimeMetrics::register_metrics(**meter);
    if (registered.has_value()) {
        transport_opened = true;
    }
    NX_CHECK(!registered.has_value());
    NX_CHECK(!transport_opened);
    if (!registered.has_value()) {
        NX_CHECK(registered.error().code == nexigen::core::ErrorCode::ResourceExhausted);
        NX_CHECK_EQ(registered.error().instrument, std::string_view("canopen_rx_overflow_total"));
        NX_CHECK(registered.error().to_string().find("canopen_rx_overflow_total") !=
                 std::string::npos);
    }
}

void test_gauge_capacity_failure() {
    RecordingMetricSink sink;
    metrics::MetricProviderConfig config{};
    config.max_gauges = 2U;
    metrics::MetricProvider provider{config, sink};
    auto meter = provider.get_meter("canopen.can0", "1.0");
    NX_CHECK(meter.has_value());
    if (!meter.has_value()) {
        return;
    }
    auto registered = detail::RuntimeMetrics::register_metrics(**meter);
    NX_CHECK(!registered.has_value());
    if (!registered.has_value()) {
        NX_CHECK(registered.error().code == nexigen::core::ErrorCode::ResourceExhausted);
        NX_CHECK_EQ(registered.error().instrument,
                    std::string_view("canopen_sdo_active_transfers"));
    }
}

void test_duplicate_metric_names_fail_startup() {
    RecordingMeterFixture fixture;
    NX_CHECK(fixture.meter != nullptr);
    if (fixture.meter == nullptr) {
        return;
    }
    NX_CHECK(detail::RuntimeMetrics::register_metrics(*fixture.meter).has_value());
    auto duplicate = detail::RuntimeMetrics::register_metrics(*fixture.meter);
    NX_CHECK(!duplicate.has_value());
    if (!duplicate.has_value()) {
        NX_CHECK(duplicate.error().code == nexigen::core::ErrorCode::InvalidArgument);
        NX_CHECK_EQ(duplicate.error().instrument, std::string_view("canopen_cycles_total"));
    }
}

} // namespace

void suite_runtime_metrics() {
    test_bus_meter_reserve();
    test_bus_meter_rejects_invalid();
    test_bus_meter_release_and_capacity();
    test_registration_creates_exact_catalogue();
    test_registration_records_and_harvests();
    test_capacity_failure_names_the_instrument();
    test_gauge_capacity_failure();
    test_duplicate_metric_names_fail_startup();
}
