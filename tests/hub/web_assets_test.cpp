// Split from hub_test.cpp: nexigen::hub embedded web assets.

#include "hub_test_support.hpp"

void test_web_assets() {
    NX_CHECK(contains(hub::index_html(), "/app.js"));
    NX_CHECK(contains(hub::index_html(), "/style.css"));
    NX_CHECK(contains(hub::app_js(), "pausedLines"));
    NX_CHECK(contains(hub::app_js(), "scheduleReconnect"));
    NX_CHECK(contains(hub::app_js(), "lastSequence"));
    NX_CHECK(contains(hub::app_js(), "EventSource"));
    NX_CHECK(!contains(hub::app_js(), "/api/start"));
    NX_CHECK(contains(hub::style_css(), "--accent"));
    // app.js is an IIFE `(() => { ... })();`. A missing wrapper parenthesis is
    // a JavaScript syntax error that leaves the connection badge stuck on
    // "connecting" (spec hub/web-ui "Embedded script parses and bootstraps").
    NX_CHECK(contains(hub::app_js(), "(() => {"));
    NX_CHECK(contains(hub::app_js(), "})();"));
    // Structured metric rendering (spec hub/web-ui "Metrics render structured
    // by kind"): the formatter and its batch parser must be present.
    NX_CHECK(contains(hub::app_js(), "renderMetric"));
    NX_CHECK(contains(hub::app_js(), "renderMetricsLine"));
}

void run_hub_web_assets_suites() {
    run_suite("web_assets", test_web_assets);
}
