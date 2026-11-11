// Entry point for the nexigen::metrics tests.
//
// Suite order is load-bearing: per-kind thread-slot budgets are never recycled
// (design D7), so the slot probe, the statistics race and every later thread
// set must keep their historical order. The entity files therefore expose one
// thin `suite_*` wrapper per suite and this file sequences them explicitly.

#include "metrics_test_support.hpp"

void suite_thread_slots_affinity();
void suite_policy_names();
void suite_policy_json();
void suite_policy_format();
void suite_concepts();
void suite_registry();
void suite_counter();
void suite_gauge();
void suite_state_gauge();
void suite_statistics();
void suite_explicit_histogram();
void suite_exponential_histogram();
void suite_composite_injection();
void suite_serialization();
void suite_provider_config();
void suite_registry_zero_alloc();
void suite_sinks_null_console();
void suite_sinks_json();
void suite_writer_bounds_full();
void suite_sink_kind_matrix();
void suite_snapshot_serialization_full();
void suite_metric_frame();
void suite_prometheus_render();
void suite_metric_daemon_config();
void suite_iceoryx2_metric_sink_config();
void suite_daemon_components();
void suite_provider_harvester();
void suite_provider_required_thread_failure();
void suite_metrics_worker_thread_settings();
void suite_registration_race();
void suite_reset_window_race();
void suite_get_meter_harvest_race();
void suite_slot_exhaustion();
void suite_explicit_histogram_mt();
void suite_exponential_histogram_mt();
void suite_latency();

int main() {
    // Suite order matters for the per-kind thread-slot budgets (design D7,
    // slots are never recycled): the slot probe claims the counter/gauge
    // spaces, the statistics race claims the 32 exclusive statistics slots
    // (main + 31 workers), and every later thread set falls to fallbacks.
    run_suite("harness", test_harness);
    suite_thread_slots_affinity();
    suite_policy_names();
    suite_policy_json();
    suite_policy_format();
    suite_concepts();
    suite_registry();
    suite_counter();
    suite_gauge();
    suite_state_gauge();
    suite_statistics();
    suite_explicit_histogram();
    suite_exponential_histogram();
    suite_composite_injection();
    suite_serialization();
    suite_provider_config();
    suite_registry_zero_alloc();
    suite_sinks_null_console();
    suite_sinks_json();
    suite_writer_bounds_full();
    suite_sink_kind_matrix();
    suite_snapshot_serialization_full();
    suite_metric_frame();
    suite_prometheus_render();
    suite_metric_daemon_config();
    suite_iceoryx2_metric_sink_config();
    suite_daemon_components();
    suite_provider_harvester();
    suite_provider_required_thread_failure();
    suite_metrics_worker_thread_settings();
    suite_registration_race();
    suite_reset_window_race();
    suite_get_meter_harvest_race();
    suite_slot_exhaustion();
    suite_explicit_histogram_mt();
    suite_exponential_histogram_mt();
    suite_latency();
    const int failures = g_failures;
    std::printf("total: %d checks, %d failed\n", g_checks, failures);
    return failures;
}
