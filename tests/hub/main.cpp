// Entry point for the nexigen::hub tests. Entity files expose one runner each
// and the shared harness/fixtures live in hub_test_support.hpp.

#include "hub_test_support.hpp"

void run_hub_model_suites();
void run_hub_discovery_suites();
void run_hub_streaming_suites();
void run_hub_web_assets_suites();
void run_hub_http_server_suites();

int main() {
    run_hub_model_suites();
    run_hub_discovery_suites();
    run_hub_streaming_suites();
    run_hub_web_assets_suites();
    run_hub_http_server_suites();

    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
