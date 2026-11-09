// Entry point for the nexigen::app tests. Each entity file exposes one execution_manager
// that preserves the historical suite order; the shared harness lives in
// app_test_support.hpp.

#include "app_test_support.hpp"

void run_app_stop_reason_suites();
void run_app_command_line_suites();
void run_app_settings_suites();
void run_app_telemetry_layout_suites();
void run_app_infrastructure_suites();

int main() {
    run_app_stop_reason_suites();
    run_app_command_line_suites();
    run_app_settings_suites();
    run_app_telemetry_layout_suites();
    run_app_infrastructure_suites();

    std::printf("app: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
