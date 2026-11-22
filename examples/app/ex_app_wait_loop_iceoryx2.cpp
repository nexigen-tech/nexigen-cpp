// ex_app_wait_loop_iceoryx2 — Infrastructure wait loop, all telemetry to iceoryx2.
//
// The same wait-loop body as ex_app_wait_loop.cpp, but the policy publishes
// logs, metric batches and spans as fixed frames over iceoryx2. Build with
// NEXIGEN_ENABLE_COM=ON; daemons are optional for the publisher itself.

#include <nexigen/app/policies/iceoryx2_policy.hpp>

#include "wait_loop_common.hpp"

int main() {
    using Policy = nexigen::app::policies::Iceoryx2Policy<'W', 'A', 'I', 'T'>;
    return nexigen::examples::run_wait_loop<Policy>("wait-loop-iceoryx2");
}
