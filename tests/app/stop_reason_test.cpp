// Split from app_test.cpp: nexigen::app stop-reason contract.

#include "app_test_support.hpp"

void test_stop_reason() {
    NX_CHECK(app::to_string(app::InfrastructureStopReason::None) == "none");
    NX_CHECK(app::to_string(app::InfrastructureStopReason::Requested) == "requested");
    NX_CHECK(app::to_string(app::InfrastructureStopReason::Sigint) == "sigint");
    NX_CHECK(app::to_string(app::InfrastructureStopReason::Sigterm) == "sigterm");

    app::InfrastructureStopReason reason = app::InfrastructureStopReason::None;
    NX_CHECK(app::from_string("sigterm", reason));
    NX_CHECK(reason == app::InfrastructureStopReason::Sigterm);
    NX_CHECK(!app::from_string("bogus", reason));
    NX_CHECK(reason == app::InfrastructureStopReason::Sigterm);

    nlohmann::json json;
    app::to_json(json, app::InfrastructureStopReason::Sigint);
    NX_CHECK(json == "sigint");
    app::from_json(nlohmann::json("requested"), reason);
    NX_CHECK(reason == app::InfrastructureStopReason::Requested);
    app::from_json(nlohmann::json(7), reason);
    NX_CHECK(reason == app::InfrastructureStopReason::Requested);
    NX_CHECK(std::format("{}", app::InfrastructureStopReason::Sigterm) == "sigterm");
}

void run_app_stop_reason_suites() {
    run_suite("stop_reason", test_stop_reason);
}
