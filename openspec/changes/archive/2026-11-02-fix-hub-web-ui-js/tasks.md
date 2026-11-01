# Tasks

## 1. Fix the embedded script

- [x] 1.1 Fix `app_js()` in `src/hub/web_assets.cpp` so the script is a well-formed IIFE (`(() => { ... })();`); verify the served `/app.js` parses with no syntax error and bootstraps the SSE tree subscription

## 2. Pin the contract and verify

- [x] 2.1 Add a `web_assets` test assertion in `tests/hub/hub_test.cpp` that `app_js()` opens with `(() => {` and closes with `})();`; verify `nexigen_hub_tests` passes
- [x] 2.2 Build and run the full hub test suite (`nexigen_hub_tests` + `hub_cli_test.sh`) in Debug; verify 100% pass
- [x] 2.3 Build and run the build-mode matrix — Debug, Release (`-O3 -march=native -flto`), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) — running the hub tests; verify no sanitizer report
- [x] 2.4 Run an end-to-end check: open the hub in a browser (or execute the script in a DOM/EventSource mock) and confirm the connection badge transitions from `connecting` to `connected` and the tree renders

## Workflow follow-up

- Archive the change after review requirements are satisfied.
- Verify the archived `hub/web-ui` spec carries the new requirement.
