// Entry point for the nexigen::tracing tests. Entity files expose one thin
// `suite_*` wrapper per suite; this file preserves the historical order.

#include "tracing_test_support.hpp"

void suite_span_enums();
void suite_ids_and_context();
void suite_attributes();
void suite_span_record();
void suite_parent_stack();
void suite_concepts();
void suite_scoped_span();
void suite_latency();
void suite_sinks();
void suite_writer_bounds_full();
void suite_writer_escape_matrix();
void suite_serialization_full();
void suite_provider();
void suite_tracing_required_thread_failure();
void suite_tracing_worker_thread_settings();
void suite_trace_propagation();

int main() {
    run_suite("harness", test_harness);
    suite_span_enums();
    suite_ids_and_context();
    suite_attributes();
    suite_span_record();
    suite_parent_stack();
    suite_concepts();
    suite_scoped_span();
    suite_latency();
    suite_sinks();
    suite_writer_bounds_full();
    suite_writer_escape_matrix();
    suite_serialization_full();
    suite_provider();
    suite_tracing_required_thread_failure();
    suite_tracing_worker_thread_settings();
    suite_trace_propagation();
    std::printf("tracing: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
