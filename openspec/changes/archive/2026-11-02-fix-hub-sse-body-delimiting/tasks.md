# Tasks

## 1. Frame the SSE body as chunked

- [x] 1.1 Add a `chunked` client flag, a `Transfer-Encoding: chunked` SSE header, and an `append_sse` helper in `src/hub/http_server.cpp`, and route the SSE body writes (`start_sse`, `dispatch_live`, `timers`) plus the terminating chunk (`close_client`) through it; verify a browser-facing HTTP client streams the SSE body incrementally

## 2. Pin the contract and verify

- [x] 2.1 Change the SSE headers assertion in `tests/hub/hub_test.cpp` to require `Transfer-Encoding: chunked`; verify `nexigen_hub_tests` passes
- [x] 2.2 Build and run the full hub test suite (`nexigen_hub_tests` + `hub_cli_test.sh`) in Debug; verify 100% pass
- [x] 2.3 Build and run the build-mode matrix — Debug, Release (`-O3 -march=native -flto`), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) — running the hub tests; verify no sanitizer report
- [x] 2.4 Run an end-to-end check: start the suite demo and the hub, and confirm the SSE response headers carry `Transfer-Encoding: chunked` (no `Content-Length`), so the web UI leaves "connecting"

## Workflow follow-up

- Archive the change after review requirements are satisfied.
- Verify the archived `hub/http-api` spec carries the updated SSE header contract.
