// ex_app_wait_loop — Infrastructure wait loop with the console policy.
//
// The shared body lives in wait_loop_common.hpp; this TU only selects where
// telemetry goes. See ex_app_wait_loop_iceoryx2.cpp for the same loop with all
// telemetry published to iceoryx2.

#include <nexigen/app/policies/console_policy.hpp>

#include "wait_loop_common.hpp"

int main() {
    return nexigen::examples::run_wait_loop<nexigen::app::policies::ConsolePolicy>("wait-loop");
}
