# Proposal

## Why

`nexigen-run` writes a deterministic run tree, `resource.json`/`exit.json`, suite state, six runtime streams per attempt, and a `runner.jsonl`. Today an operator must walk directories and tail files by hand. There is no live, read-only view of what the runner is doing, and no local API for the planned observatory UI.

## What Changes

- Add `nexigen-hub`, a foreground observatory daemon with a reusable `nexigen_hub` library and a CLI binary.
- Discover runs from one or more explicit `--run-dir` roots using inotify plus an initial recursive scan. Watch suite metadata, instance directories, and artifact roots recorded in `suite.json`.
- Build a live tree: run → suite → app → instance → attempt, with aggregate/per-app state derived from the existing runner metadata and state files.
- Tail the streams under discovered artifact roots: `app.jsonl`, `metrics.jsonl`, `traces.jsonl`, `chrono.jsonl`, `stdout.log`, `stderr.log`, and suite `runner.jsonl`.
- Keep a bounded in-memory ring per stream; a new client receives a replay of recent records followed by live updates.
- Serve a local HTTP/1.1 API plus Server-Sent Events for live updates, binding to `127.0.0.1` by default with no authentication.
- Ship an embedded plain HTML/CSS/JS Web UI in the binary: run tree, suite/instance state, stream list, and live raw JSON/text viewer.
- Provide GET-only observability. Hub MUST NOT start, stop, restart, or otherwise control applications; `nexigen-run` remains the control interface.
- Add no external dependencies. Use Linux inotify, POSIX sockets, `epoll`, and the existing `nlohmann::json`.
- No change to runner behavior, application layouts, generated settings, or existing CLI syntax.

## Capabilities

### New Capabilities

- `hub/discovery`: explicit run roots, initial scan, inotify watching, artifact-root discovery, live run/suite/app/instance/attempt tree, and state derivation.
- `hub/streaming`: file tailing, line framing, bounded rings, replay-before-live delivery, stream identity, and slow-client behavior.
- `hub/http-api`: HTTP/1.1 endpoint contract, JSON representations, SSE endpoint, localhost binding, limits, and error responses.
- `hub/web-ui`: embedded assets and browser behavior for the run tree, state panels, stream selection, replay, and live viewing.

### Modified Capabilities

None. `runner/*`, `logging/*`, `app/*`, and telemetry layout contracts stay as they are; hub only reads their files.

## Impact

- **Code**: new `include/nexigen/hub/`, `src/hub/`, `tests/hub/`, `tools/nexigen-hub/main.cpp`, `nexigen_hub` library, `nexigen-hub` binary, and embedded web assets.
- **APIs**: new hub library and HTTP/SSE contract; no existing API changes.
- **Dependencies**: none new; Linux inotify/sockets/epoll plus existing `nlohmann::json`.
- **Performance**: control plane only. Hub never runs in an application tick, producer, snapshot, or sink path and does not modify supervised processes. One epoll-driven control loop handles discovery, tailing, HTTP, and SSE. Ring memory is bounded per stream (default 4,096 records / 8 MiB, configurable); slow clients are disconnected rather than growing unbounded. File reads copy into pre-sized buffers and do not touch real-time threads.
- **Operational**: one foreground process per observatory; `--run-dir` may be repeated; `--listen` defaults to loopback; no authentication is provided, so binding to a non-loopback address is an explicit operator choice.
