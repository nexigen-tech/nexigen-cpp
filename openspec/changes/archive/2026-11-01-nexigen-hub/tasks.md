# Tasks

## 1. Hub model

- [x] 1.1 Add `include/nexigen/hub/model.hpp` and `src/hub/model.cpp` with `Stream_kind`, `Hub_stream`, `Hub_attempt`, `Hub_instance`, `Hub_app`, `Hub_suite`, `Hub_run`, `Hub_tree`, record envelope, and gap marker; implement `to_string` member/free, `std::formatter`, and non-throwing JSON. Verify: serialization tests cover every enum/struct, hostile JSON, and round-trip.
- [x] 1.2 Implement run-root canonicalization, duplicate collapse, validation, and hub-local `run_key` assignment. Verify: tests cover one root, multiple roots, duplicates, relative/missing roots, and stable ids.

## 2. Discovery and inotify

- [x] 2.1 Implement the initial recursive scan that builds run → suite → app → instance → attempt from `suite.json`, `resource.json`, and attempt directories. Verify: a fixture tree produces the expected model and ignores unrelated files.
- [x] 2.2 Implement the inotify watcher for roots, `.suite`, suite/instance directories, and attempt trees, including create/delete/move handling and queue-overflow rescan. Verify: fixture tests add/remove a suite and attempt and observe tree updates.
- [x] 2.3 Discover artifact roots from `suite.json`, including roots outside the run directory; tolerate missing/invalid roots. Verify: tests cover external root, missing root, and a new attempt.
- [x] 2.4 Derive suite/app state from `suite-state.json`, `suite-exit.json`, `resource.json`, and `exit.json`, with `unknown` fallback. Verify: tests cover running, degraded, failed, stopped, and unknown.
- [x] 2.5 Prove discovery is read-only: compare a fixture tree before and after scan/watch/reconcile. Verify: no file content, mtime, or directory entry changes.

## 3. Streaming

- [x] 3.1 Implement `Stream_tailer` initial bounded read, offset following, partial-line buffering, max-line handling, and new-file start. Verify: tests cover seeded tail, partial writes, oversized line, and new file.
- [x] 3.2 Implement bounded record/byte rings with oldest-drop and gap marker accounting. Verify: tests overflow both limits and check retained records, dropped count, and sequence range.
- [x] 3.3 Implement subscriber replay-before-live ordering, snapshot boundaries, and sequence filtering. Verify: tests subscribe during writes and assert no duplicates or gaps.
- [x] 3.4 Implement slow-subscriber outbound bounds and disconnect isolation. Verify: one stalled subscriber does not stop records for another.
- [x] 3.5 Handle truncation, rename replacement, and new attempts. Verify: tests reopen from zero, retain stream identity on replacement, and separate attempt streams.

## 4. HTTP API and SSE

- [x] 4.1 Implement the minimal HTTP/1.1 parser and epoll server: GET only, limits, keep-alive, 400/404/405, JSON errors. Verify: socket tests cover valid/invalid requests and limits.
- [x] 4.2 Implement `/api/health`, `/api/tree`, `/api/streams`, and `/api/streams/<id>/recent`. Verify: JSON shapes and limit clamping match the specs.
- [x] 4.3 Implement SSE for one stream, all streams, and `tree=1`, including replay, snapshot boundaries, heartbeats, and disconnect. Verify: a socket test reads replay then a live record and a slow client is closed.
- [x] 4.4 Serve the embedded `/`, `/index.html`, `/app.js`, and `/style.css` assets with correct content types. Verify: tests check status, content type, and non-empty bodies.

## 5. Web UI

- [x] 5.1 Add embedded `index.html`, `style.css`, and `app.js` with the run tree, state panel, and connection status. Verify: tree rendering updates from `/api/tree` and `tree=1` SSE.
- [x] 5.2 Add the stream list and viewer with replay, live follow, pause/resume, clear, and raw text/JSON display. Verify: selection, replay/live ordering, and DOM line cap work against a fixture hub.
- [x] 5.3 Handle empty hub, unknown stream, disconnect/reconnect with `since`, and make the UI read-only. Verify: static/UI tests confirm no control endpoints or buttons and the reconnect cursor is sent.

## 6. CLI, build, and documentation

- [x] 6.1 Add `tools/nexigen-hub/main.cpp` with repeatable `--run-dir`, `--listen`, ring/line limits, `--help`, and SIGINT/SIGTERM shutdown. Verify: CLI smoke test accepts/validates arguments and exits cleanly.
- [x] 6.2 Add `nexigen_hub` + `nexigen-hub` CMake targets and register tests. Verify: Debug build links both and `ctest -R nexigen_hub` passes.
- [x] 6.3 Document the hub in `docs/nexigen-hub.md`: scope, CLI, API, SSE, UI, limits, and localhost/security notes. Verify: every documented command and endpoint matches the implementation.

## 7. Integration verification

- [x] 7.1 Build and run 100% of unit tests in Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify: all four CTest runs pass.
- [x] 7.2 Run `openspec validate nexigen-hub --strict`. Verify: the change validates cleanly.
- [x] 7.3 Address verification warnings: non-loopback bind warning, separate request-line/header/URL limits, `Last-Event-ID` resume, UI pause buffering and cursor reconnect, and empty-artifact-root stream suppression. Verify: `nexigen_hub_tests` and `nexigen_hub_cli_tests` cover the new behavior.
- [x] 7.4 Stabilize the pre-existing SPSC concurrency test race exposed by TSan verification. Verify: repeated `nexigen_logging_tests` runs complete without TSan reports.
