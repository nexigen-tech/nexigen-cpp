// Entry point for the nexigen::canopen::observability test suite.
//
// Suites are added as the change lands: vocabulary, metric registration,
// error gate, allocation freedom, cold-path helpers, and the hot-path
// benchmark. Each suite is one `suite_*` wrapper so a failure names its file.
#include "observability_test_support.hpp"

void suite_vocabulary();
void suite_runtime_metrics();
void suite_error_gate();
void suite_allocation();
void suite_cold_path();
void suite_metric_mapping();
void suite_benchmark();

int main() {
    run_suite("vocabulary", suite_vocabulary);
    run_suite("runtime_metrics", suite_runtime_metrics);
    run_suite("error_gate", suite_error_gate);
    run_suite("allocation", suite_allocation);
    run_suite("cold_path", suite_cold_path);
    run_suite("metric_mapping", suite_metric_mapping);
    run_suite("benchmark", suite_benchmark);
    return g_failures == 0 ? 0 : 1;
}
