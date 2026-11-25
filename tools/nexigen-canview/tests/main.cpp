#include <cstdio>

#include "suite.hpp"
#include "support/check.hpp"

int main() {
    canview_test::run_canopen_tests();
    canview_test::run_canopen_decode_tests();
    canview_test::run_capture_tests();
    canview_test::run_cli_tests();
    canview_test::run_config_tests();
    canview_test::run_heatmap_tests();
    canview_test::run_model_tests();
    canview_test::run_views_tests();
    canview_test::run_resources_tests();
    canview_test::run_ring_tests();
    canview_test::run_session_tests();
    canview_test::run_time_tests();
    if (canview_test::failures != 0) {
        std::fprintf(stderr, "%d checks failed\n", canview_test::failures);
        return 1;
    }
    std::printf("nexigen_canview_tests: all checks passed\n");
    return 0;
}
