// Outcome-to-metric mapping tests (change add-canopen-observability; spec
// canopen/observability): loop/traffic/error/saturation scenarios, metric-first
// hot-error ordering, one-owner counters, null-logger operation, and concurrent
// harvesting.
#include "observability_test_support.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

#include <nexigen/canopen/observability/can_open_error_domain.hpp>
#include <nexigen/canopen/observability/can_open_log_policy.hpp>
#include <nexigen/canopen/observability/can_open_runtime_state.hpp>
#include <nexigen/canopen/observability/detail/error_gate.hpp>
#include <nexigen/canopen/observability/detail/metric_mapping.hpp>
#include <nexigen/canopen/observability/detail/runtime_metrics.hpp>
#include <nexigen/canopen/pdo_decode_result.hpp>
#include <nexigen/logging/null_logger.hpp>

namespace {

namespace metrics = nexigen::metrics;
namespace obs = nexigen::canopen::observability;
namespace detail = nexigen::canopen::observability::detail;

using obs::CanOpenErrorDomain;
using obs::CanOpenLogPolicy;
using obs::CanOpenRuntimeState;
using detail::ErrorGate;
using detail::RuntimeMetrics;
using nexigen::canopen::PdoDecodeStatus;

/// @brief Harvest and return one record by name.
/// @param fixture Recording provider fixture.
/// @param name Metric name.
/// @return Record, or `nullptr` when missing.
const metrics::MetricRecord* harvest_record(RecordingMeterFixture& fixture,
                                            std::string_view name) noexcept {
    if (!fixture.harvest()) {
        return nullptr;
    }
    return find_metric_record(fixture.sink, name);
}

/// @brief Harvest and read one counter.
/// @param fixture Recording provider fixture.
/// @param name Metric name.
/// @return Counter value, or zero when missing/kind-mismatched.
std::uint64_t counter_value(RecordingMeterFixture& fixture, std::string_view name) noexcept {
    const metrics::MetricRecord* record = harvest_record(fixture, name);
    if (record == nullptr || record->kind != metrics::MetricKind::Counter) {
        return 0U;
    }
    return record->scalar.counter;
}

/// @brief Harvest and read one gauge.
/// @param fixture Recording provider fixture.
/// @param name Metric name.
/// @return Gauge value, or zero when missing/kind-mismatched.
std::int64_t gauge_value(RecordingMeterFixture& fixture, std::string_view name) noexcept {
    const metrics::MetricRecord* record = harvest_record(fixture, name);
    if (record == nullptr || record->kind != metrics::MetricKind::Gauge) {
        return 0;
    }
    return record->scalar.gauge_int;
}

/// @brief Register the catalogue on a fresh recording fixture.
/// @param fixture Recording provider fixture.
/// @return Registered instruments.
RuntimeMetrics register_catalogue(RecordingMeterFixture& fixture) {
    NX_CHECK(fixture.meter != nullptr);
    auto registered = RuntimeMetrics::register_metrics(*fixture.meter);
    NX_CHECK(registered.has_value());
    if (!registered.has_value()) {
        return RuntimeMetrics{};
    }
    return *registered;
}

// --- loop metrics ------------------------------------------------------------

void test_loop_metrics() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    detail::record_cycle(metrics);
    detail::record_cycle(metrics);
    detail::record_cycle_overrun(metrics);
    detail::record_loop_latency(metrics, 2'500U);
    detail::set_runtime_state(metrics, CanOpenRuntimeState::Running);

    NX_CHECK(fixture.harvest());
    NX_CHECK_EQ(counter_value(fixture, "canopen_cycles_total"), std::uint64_t{2});
    NX_CHECK_EQ(counter_value(fixture, "canopen_cycle_overruns_total"), std::uint64_t{1});
    const metrics::MetricRecord* latency =
        find_metric_record(fixture.sink, "canopen_loop_latency_ns");
    NX_CHECK(latency != nullptr);
    if (latency != nullptr) {
        NX_CHECK_EQ(latency->histogram.count, std::uint64_t{1});
        NX_CHECK_EQ(latency->histogram.sum, 2'500.0);
    }
    const metrics::MetricRecord* state = find_metric_record(fixture.sink, "canopen_state");
    NX_CHECK(state != nullptr);
    if (state != nullptr) {
        NX_CHECK_EQ(std::string_view(state->state.value), std::string_view("Running"));
    }
}

// --- traffic metrics ---------------------------------------------------------

void test_traffic_metrics() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    detail::record_frame_received(metrics);
    detail::record_frame_processed(metrics);
    detail::record_pdo_rx(metrics);
    detail::record_pdo_tx(metrics);
    detail::record_tx_frame(metrics);
    detail::record_tx_failure(metrics);

    NX_CHECK(fixture.harvest());
    NX_CHECK_EQ(counter_value(fixture, "canopen_frames_received_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_frames_processed_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_pdo_rx_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_pdo_tx_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_tx_frames_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_tx_failures_total"), std::uint64_t{1});
}

void test_unrouted_frame_is_not_processed_or_dropped() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    detail::record_frame_received(metrics);
    detail::record_frame_processed(metrics);
    detail::record_frame_received(metrics);
    detail::record_frame_unrouted(metrics);

    NX_CHECK(fixture.harvest());
    NX_CHECK_EQ(counter_value(fixture, "canopen_frames_received_total"), std::uint64_t{2});
    NX_CHECK_EQ(counter_value(fixture, "canopen_frames_processed_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_frames_unrouted_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_frames_dropped_total"), std::uint64_t{0});
}

// --- error metrics -----------------------------------------------------------

void test_dropped_total_equals_sum_of_causes() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    detail::record_rx_overflow_drop(metrics);
    detail::record_tx_queue_full_drop(metrics);
    detail::record_driver_drop(metrics);

    NX_CHECK(fixture.harvest());
    const std::uint64_t total = counter_value(fixture, "canopen_frames_dropped_total");
    const std::uint64_t overflow = counter_value(fixture, "canopen_rx_overflow_total");
    const std::uint64_t queue_full = counter_value(fixture, "canopen_tx_queue_full_total");
    const std::uint64_t driver = counter_value(fixture, "canopen_driver_drop_total");
    NX_CHECK_EQ(total, std::uint64_t{3});
    NX_CHECK_EQ(overflow, std::uint64_t{1});
    NX_CHECK_EQ(queue_full, std::uint64_t{1});
    NX_CHECK_EQ(driver, std::uint64_t{1});
    NX_CHECK_EQ(total, overflow + queue_full + driver);
}

void test_pdo_decode_and_mapping_errors_are_distinct() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    detail::record_pdo_decode(metrics, PdoDecodeStatus::Partial);
    NX_CHECK(fixture.harvest());
    NX_CHECK_EQ(counter_value(fixture, "canopen_pdo_decode_errors_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_pdo_mapping_errors_total"), std::uint64_t{0});

    detail::record_pdo_decode(metrics, PdoDecodeStatus::UnknownObject);
    NX_CHECK(fixture.harvest());
    NX_CHECK_EQ(counter_value(fixture, "canopen_pdo_decode_errors_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_pdo_mapping_errors_total"), std::uint64_t{1});

    detail::record_pdo_decode(metrics, PdoDecodeStatus::Ok);
    detail::record_pdo_decode(metrics, PdoDecodeStatus::Invalid);
    NX_CHECK(fixture.harvest());
    NX_CHECK_EQ(counter_value(fixture, "canopen_pdo_decode_errors_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_pdo_mapping_errors_total"), std::uint64_t{1});
}

void test_sdo_failures_are_counted_separately() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    detail::record_sdo_request(metrics);
    detail::record_sdo_abort(metrics);
    detail::record_sdo_timeout(metrics);

    NX_CHECK(fixture.harvest());
    NX_CHECK_EQ(counter_value(fixture, "canopen_sdo_requests_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_sdo_aborts_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_sdo_timeouts_total"), std::uint64_t{1});
}

void test_heartbeat_and_bus_failures_are_counted() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    detail::record_heartbeat_miss(metrics);
    detail::record_bus_off(metrics);
    NX_CHECK(fixture.harvest());
    NX_CHECK_EQ(counter_value(fixture, "canopen_heartbeat_misses_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_bus_off_total"), std::uint64_t{1});
}

// --- saturation metrics ------------------------------------------------------

void test_saturation_gauges_track_current_state() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    detail::set_rx_queue_depth(metrics, 3);
    NX_CHECK_EQ(gauge_value(fixture, "canopen_rx_queue_depth"), std::int64_t{3});
    detail::set_rx_queue_depth(metrics, 4);
    NX_CHECK_EQ(gauge_value(fixture, "canopen_rx_queue_depth"), std::int64_t{4});

    detail::set_tx_queue_depth(metrics, 2);
    detail::set_sdo_active_transfers(metrics, 1);
    detail::set_nodes_online(metrics, 5);
    NX_CHECK_EQ(gauge_value(fixture, "canopen_tx_queue_depth"), std::int64_t{2});
    NX_CHECK_EQ(gauge_value(fixture, "canopen_sdo_active_transfers"), std::int64_t{1});
    NX_CHECK_EQ(gauge_value(fixture, "canopen_nodes_online"), std::int64_t{5});
}

// --- hot-error composition and ownership ------------------------------------

void test_filtered_hot_error_still_counts_the_metric() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    CaptureLogger logger;
    logger.set_level(nexigen::core::LogLevel::Fatal);
    const CanOpenLogPolicy policy{};
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();

    for (std::uint64_t index = 0U; index < 10U; ++index) {
        static_cast<void>(detail::record_hot_error(*metrics.sdo_aborts_total, gate,
                                                   CanOpenErrorDomain::Sdo, 3U, 0x0503'0000U,
                                                   index * ms_to_ns(1U)));
    }
    NX_CHECK_EQ(captured_records().size(), std::size_t{0});
    NX_CHECK_EQ(counter_value(fixture, "canopen_sdo_aborts_total"), std::uint64_t{10});
}

void test_repeated_hot_errors_keep_counting_the_metric() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 60'000U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();

    for (std::uint64_t index = 0U; index < 10U; ++index) {
        static_cast<void>(detail::record_hot_error(*metrics.pdo_decode_errors_total, gate,
                                                   CanOpenErrorDomain::Pdo, 4U, 7U,
                                                   index * ms_to_ns(1U)));
    }
    NX_CHECK_EQ(captured_records().size(), std::size_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_pdo_decode_errors_total"), std::uint64_t{10});
}

void test_null_logger_keeps_metric_recording_unchanged() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    nexigen::logging::NullLogger logger;
    const CanOpenLogPolicy policy{};
    ErrorGate<nexigen::logging::NullLogger> gate{policy, logger};

    detail::record_cycle(metrics);
    static_cast<void>(detail::record_hot_error(*metrics.sdo_aborts_total, gate,
                                               CanOpenErrorDomain::Sdo, 1U, 9U, 0U));
    detail::record_rx_overflow_drop(metrics);

    NX_CHECK(fixture.harvest());
    NX_CHECK_EQ(counter_value(fixture, "canopen_cycles_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_sdo_aborts_total"), std::uint64_t{1});
    NX_CHECK_EQ(counter_value(fixture, "canopen_frames_dropped_total"), std::uint64_t{1});
}

void test_sdo_abort_has_exactly_one_owner() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    detail::record_sdo_abort(metrics);

    NX_CHECK(fixture.harvest());
    NX_CHECK_EQ(fixture.sink.count, std::uint32_t{31});
    for (std::uint32_t index = 0U; index < fixture.sink.count; ++index) {
        const metrics::MetricRecord& record = fixture.sink.records[index];
        const std::string_view name = metrics::detail::view_name(record.identity.metric_name);
        if (name == "canopen_sdo_aborts_total") {
            NX_CHECK_EQ(record.scalar.counter, std::uint64_t{1});
            continue;
        }
        if (record.kind == metrics::MetricKind::Counter) {
            NX_CHECK_EQ(record.scalar.counter, std::uint64_t{0});
        }
    }
}

void test_no_canopen_publish_counter_exists() {
    RecordingMeterFixture fixture;
    static_cast<void>(register_catalogue(fixture));
    NX_CHECK(fixture.harvest());
    for (std::uint32_t index = 0U; index < fixture.sink.count; ++index) {
        const std::string_view name =
            metrics::detail::view_name(fixture.sink.records[index].identity.metric_name);
        NX_CHECK(name.find("publish") == std::string_view::npos);
    }
}

// --- concurrent harvesting ---------------------------------------------------

void test_concurrent_harvest_is_race_free() {
    RecordingMeterFixture fixture;
    RuntimeMetrics metrics = register_catalogue(fixture);
    constexpr std::uint64_t kRecords = 200'000U;
    std::atomic<bool> start{false};
    std::thread producer([&metrics, &start] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::uint64_t index = 0U; index < kRecords; ++index) {
            detail::record_cycle(metrics);
        }
    });

    start.store(true, std::memory_order_release);
    std::uint64_t observed = 0U;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    while (observed < kRecords && std::chrono::steady_clock::now() < deadline) {
        if (fixture.harvest()) {
            const metrics::MetricRecord* record =
                find_metric_record(fixture.sink, "canopen_cycles_total");
            if (record != nullptr) {
                observed = record->scalar.counter;
            }
        }
        std::this_thread::yield();
    }
    producer.join();
    NX_CHECK_EQ(counter_value(fixture, "canopen_cycles_total"), kRecords);
}

} // namespace

void suite_metric_mapping() {
    test_loop_metrics();
    test_traffic_metrics();
    test_unrouted_frame_is_not_processed_or_dropped();
    test_dropped_total_equals_sum_of_causes();
    test_pdo_decode_and_mapping_errors_are_distinct();
    test_sdo_failures_are_counted_separately();
    test_heartbeat_and_bus_failures_are_counted();
    test_saturation_gauges_track_current_state();
    test_filtered_hot_error_still_counts_the_metric();
    test_repeated_hot_errors_keep_counting_the_metric();
    test_null_logger_keeps_metric_recording_unchanged();
    test_sdo_abort_has_exactly_one_owner();
    test_no_canopen_publish_counter_exists();
    test_concurrent_harvest_is_race_free();
}
