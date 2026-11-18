// nexigen::synchro test driver (change add-synchro-event-sync).
//
// Each suite lives in its own translation unit; this file only declares and
// runs them in a deterministic order.
#include <cstdio>

void run_protocol_suite();
void run_schedule_tracker_suite();
void run_generator_suite();
void run_consumer_suite();
void run_runner_suite();
void run_integration_suite();

extern int g_failures;

int main() {
    std::printf("nexigen::synchro test suite\n");
    run_protocol_suite();
    run_schedule_tracker_suite();
    run_generator_suite();
    run_consumer_suite();
    run_runner_suite();
    run_integration_suite();

    if (g_failures == 0) {
        std::printf("ALL SUITES PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
