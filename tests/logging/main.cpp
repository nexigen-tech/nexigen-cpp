// Entry point for the nexigen::logging tests. Entity files expose one thin
// `suite_*` wrapper per suite; this file preserves the historical order.

#include "logging_test_support.hpp"

void suite_levels();
void suite_record();
void suite_producer();
void suite_latency();
void suite_null_logger();
void suite_console();
void suite_json();
void suite_affinity();
void suite_rotation();
void suite_file_logger();
void suite_composite();
void suite_enum_vocab();
void suite_config_strings();
void suite_logger_options();
void suite_settings_vocabulary();
void suite_logging_settings_loader();
void suite_logging_settings_apply();
void suite_logging_worker_thread_settings();
void suite_file_size_rotation();
void suite_settings_full_coverage();
void suite_worker_idle_tuning();
void suite_logger_required_thread_failure();
void suite_spam_protection();
void suite_spam_config_race();
void suite_shutdown_drain_race();
void suite_spam_window_rotation_errors();
void suite_logging_json_rejections();
void suite_drop_visibility();
void suite_producer_edge_cases();
void suite_flush_summary_race();
void suite_stress();

int main() {
    run_suite("harness", test_harness);
    suite_levels();
    suite_record();
    suite_producer();
    suite_latency();
    suite_null_logger();
    suite_console();
    suite_json();
    suite_affinity();
    suite_rotation();
    suite_file_logger();
    suite_composite();
    suite_enum_vocab();
    suite_config_strings();
    suite_logger_options();
    suite_settings_vocabulary();
    suite_logging_settings_loader();
    suite_logging_settings_apply();
    suite_logging_worker_thread_settings();
    suite_file_size_rotation();
    suite_settings_full_coverage();
    suite_worker_idle_tuning();
    suite_logger_required_thread_failure();
    suite_spam_protection();
    suite_spam_config_race();
    suite_shutdown_drain_race();
    suite_spam_window_rotation_errors();
    suite_logging_json_rejections();
    suite_drop_visibility();
    suite_producer_edge_cases();
    suite_flush_summary_race();
    suite_stress();
    // Suites register here as groups 2..8 land (design D18).
    const int failures = g_failures;
    std::printf("total: %d checks, %d failed\n", g_checks, failures);
    return failures;
}
