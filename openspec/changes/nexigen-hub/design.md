# Design

## Context

`nexigen-run` and its suite layer already produce a deterministic read-only surface: run directories, `resource.json`/`exit.json`, `.suite/<suite_id>/suite.json`, `suite-state.json`, `suite-exit.json`, per-attempt streams, and `runner.jsonl`. See `proposal.md` for motivation. There is no observer, HTTP listener, or UI in the repository today.

Project constraints: C++23, `-fno-exceptions`, `-fno-rtti`, `core::Status`/`Result`, `JSON_NOEXCEPTION=1`, Linux-only syscalls, no new external dependencies, and no work on application hot paths. `nlohmann::json` is the only external dependency already in use.

## Goals / Non-Goals

**Goals:**

- One foreground `nexigen-hub` process that discovers explicit run roots and follows their artifacts.
- A bounded, replayable stream pipeline shared by all clients.
- A small HTTP/1.1 server with JSON snapshots and SSE live updates.
- An embedded, dependency-free browser UI.
- Read-only observation with predictable limits and no application impact.

**Non-Goals:**

- No start/stop/restart or any write to runner metadata.
- No authentication, TLS, multi-user accounts, or non-loopback-by-default binding.
- No typed decoding of metrics/traces/chrono beyond raw JSONL lines.
- No persistence of hub history across restarts.
- No multi-host federation, database, or long-term retention.
- No dependency on a frontend toolchain or external JS/CSS.

## Decisions

### D1. Process and loop model

`nexigen-hub` is one foreground process with one epoll-driven control loop. The loop handles:

- inotify events;
- listening socket accepts and client read/write readiness;
- nonblocking stream-file reads;
- state refresh ticks;
- SSE heartbeats;
- periodic reconciliation scans.

File reads are regular-file reads and can block briefly; each tick reads at most a bounded chunk per stream (for example 256 KiB) so a very large file cannot monopolize HTTP handling. No application thread is involved, and no background thread is required.

Alternative considered: one thread per stream or a thread pool. Rejected for the first version because it adds locking, lifecycle, and shutdown complexity for a control-plane tool; the existing loop budget is sufficient.

### D2. Module layout

```
include/nexigen/hub/
├── model.hpp          # tree, descriptors, records, gap markers, Stream_kind
├── discovery.hpp      # run roots, scan, inotify watcher, state refresh
├── streaming.hpp      # tailers, rings, subscribers
├── http_server.hpp    # listener, request parser, responses, SSE
└── web_assets.hpp     # embedded index.html/app.js/style.css accessors

src/hub/
├── model.cpp
├── discovery.cpp
├── streaming.cpp
├── http_server.cpp
└── web_assets.cpp

tools/nexigen-hub/main.cpp
tests/hub/hub_test.cpp
```

`nexigen_hub` links `nexigen_runner` (for the existing `Suite_metadata`, state enums, and instance metadata readers), `nexigen_core`, and `Threads::Threads`. `nexigen-hub` links `nexigen_hub`.

### D3. Run keys, tree model, and stable identities

Each `--run-dir` is canonicalized (absolute, trailing slash removed, symlink-resolved where possible) and assigned a hub-local `run_key` such as `run-1` in sorted root order. Ties are impossible because duplicate roots are collapsed.

Tree:

```
Hub_tree
└── Hub_run {run_key, run_dir}
    └── Hub_suite {suite_id, suite_dir, artifacts_path, state, apps}
        └── Hub_app {app, state, instances}
            └── Hub_instance {instance, instance_dir, state, attempts}
                └── Hub_attempt {attempt, artifact_dir, streams}
                    └── Hub_stream {id, kind, path}
```

`Hub_instance` represents one application instance and its control directory. `Hub_attempt` groups the streams written by one launch attempt. The stream id is a URL-safe composition:

```
<run_key>:<suite_id>:<app>:<instance>:<attempt-4>:<kind>
```

All path components come from validated runner identifiers. The id is a display/routing key, not a filesystem path.

State uses the existing `runner::Suite_state` and `runner::App_state` enums, read from `suite-state.json`, `suite-exit.json`, `resource.json`, and `exit.json`, with `unknown` when no record exists. No new state vocabulary is introduced.

### D4. Initial scan and inotify

Initial scan for each root:

1. Enumerate `.suite/*/suite.json`, parse with `runner::Suite_metadata`.
2. For each suite record, enumerate its `apps` and instance directories.
3. Enumerate `<artifacts_path>/<suite_id>/<app>/<instance>/attempt-*` and attach streams.
4. Enumerate direct instance directories under the run root for legacy single-instance runs; if `resource.json` exists, add them with a synthetic suite/app identity.

Watches:

- run root (`IN_CREATE|IN_DELETE|IN_MOVED_*` on children);
- `.suite` directory and each suite directory;
- each instance control directory;
- each artifact root and each attempt directory;
- each followed stream file (`IN_MODIFY|IN_CLOSE_WRITE|IN_MOVE_SELF|IN_DELETE_SELF`).

On a create/delete/move, the affected directory is rescanned. On `IN_Q_OVERFLOW`, every root and artifact root is rescanned and reconciled. A 30-second reconciliation pass catches missed events and validates watch descriptors. Watches on deleted paths are removed; failures to add a watch are logged and retried on the next reconciliation.

Alternative considered: polling the tree. Rejected because the request explicitly calls for inotify and a live UI.

### D5. Tailers and rings

One `Stream_tailer` per stream:

```cpp
struct Stream_tailer {
    Hub_stream descriptor;
    int fd{-1};
    dev_t device{};
    ino_t inode{};
    std::uint64_t offset{0};
    std::string partial;                 // bounded by max_line_bytes
    std::deque<Stream_record> ring;
    std::uint64_t ring_bytes{0};
    std::uint64_t sequence{0};
    std::uint64_t dropped{0};
    std::uint64_t first_dropped_sequence{0};
};
```

Initial open reads up to `ring_bytes` from the file end (or the whole file if smaller), discards the leading partial line, and sets `offset` to the file size. New files start at offset zero. On modification the tailer reads up to `read_chunk_bytes` per tick, splits complete lines, and buffers the trailing partial. `max_line_bytes` defaults to 1 MiB; longer lines are discarded and counted.

Ring capacity defaults: 4,096 records and 8 MiB per stream. `std::deque` stores `Stream_record {sequence, stream_id, kind, received_at, line, truncated}`. Overflow pops the oldest record, adds its bytes to `dropped`, and records the first missing sequence. A gap marker is emitted to a subscriber that observes a sequence jump or is created after a gap.

File replacement: `fstat` on each inotify/read tick compares device/inode; a mismatch or a smaller size reopens the path and resets offset. Truncation also reopens from zero.

Alternatives considered: `inotify`-only tailing without periodic reads. Rejected because writes can complete between events and because regular-file modification does not carry a reliable offset.

### D6. Subscribers and replay

A subscriber is `{stream_filter or all, last_sequence per stream, out_buffer, sse_buffer}`. On subscribe:

1. copy the current ring (sequence-ordered) for each selected stream;
2. emit `snapshot_begin`, one `record` event per copied record, `snapshot_end`;
3. mark `last_sequence` at the snapshot boundary;
4. append live records with a higher sequence.

The control loop owns all ring and subscriber state; there is no cross-thread locking. The outbound buffer is bounded at 1 MiB; a client that exceeds it is disconnected and its drop is counted for diagnostics.

SSE event names:

- `snapshot_begin` / `snapshot_end` — JSON metadata with stream ids and sequence boundaries;
- `record` — `Stream_record` JSON, with `id: <stream-id>:<sequence>`;
- `gap` — dropped count and missing sequence range;
- `tree` — updated `Hub_tree` snapshot for `?tree=1`;
- `heartbeat` — periodic keep-alive comment.

`Last-Event-ID` and `?since=<sequence>` filter replay; the UI uses the last record id it saw. A sequence jump produces a `gap` event.

### D7. HTTP server

Minimal HTTP/1.1 implementation on a nonblocking listening socket:

- `epoll` registration for the listener, each client, inotify fd, and stream fds; a timer for heartbeats/state refresh.
- Request parsing: request line + headers, CRLF, maximum 8 KiB header block, no request body for GET, reject other methods with 405.
- Response: status line, `Content-Length` for finite bodies, `Connection: keep-alive` unless the client asks to close, JSON errors.
- SSE: `200`, `Content-Type: text/event-stream`, `Cache-Control: no-cache`, `Connection: keep-alive`, no `Content-Length`; write ready events from the subscriber buffer.
- Limits: 64 concurrent clients, 4 KiB request line, 8 KiB headers, 64 KiB URL, 1 MiB subscriber buffer.
- Static assets are served from `web_assets.cpp` with exact content types.
- `--listen` parser accepts `host:port`; default `127.0.0.1:8080`; non-loopback emits a warning; bind errors fail startup.

Endpoints:

| Method | Path | Response |
|---|---|---|
| GET | `/api/health` | `{status, version}` |
| GET | `/api/tree` | `Hub_tree` |
| GET | `/api/streams` | `Hub_stream[]` |
| GET | `/api/streams/<id>/recent?limit=N` | `{stream, records, gap?}` |
| GET | `/api/events?stream=<id>&since=N` | SSE records for one stream |
| GET | `/api/events?tree=1` | SSE tree snapshots |
| GET | `/`, `/index.html`, `/app.js`, `/style.css` | embedded UI |

No other methods or control paths exist.

### D8. Embedded UI

`web_assets.cpp` stores three static string literals:

- `index.html` — layout, tree pane, state pane, stream pane, connection status;
- `style.css` — dark, compact, responsive; no external fonts;
- `app.js` — fetch tree, render tree, subscribe to `/api/events?tree=1`, subscribe to the selected stream, replay/live rendering, pause/follow, clear, connection retry with `since` cursor, no control actions.

The UI is deliberately framework-free. It renders records as `<pre>`/text nodes, never injects untrusted HTML, and caps displayed lines (for example 5,000) to bound DOM memory; older lines remain in the hub ring and can be reloaded by reselecting the stream.

### D9. Configuration and defaults

CLI:

```
nexigen-hub --run-dir <dir> [--run-dir <dir>...]
            [--listen <host:port>]
            [--ring-records <n>] [--ring-bytes <n>]
            [--max-line-bytes <n>]
```

Defaults: `127.0.0.1:8080`, 4,096 records, 8 MiB ring bytes, 1 MiB line, 64 clients, 1 s state refresh, 15 s heartbeat, 30 s reconciliation.

The process handles `SIGINT`/`SIGTERM` by closing clients and exiting cleanly; no files are written.

### D10. Build and tests

- `nexigen_hub` static library and `nexigen-hub` binary.
- Unit tests for model serialization, run-root validation, line framing, ring overflow/gap, replay/live ordering, HTTP parsing/limits, endpoint JSON, and SSE framing.
- Integration test starts a hub on `127.0.0.1:0` (ephemeral port) with a temporary run tree, connects a POSIX socket client, asserts `/api/health`, `/api/tree`, recent records, and an SSE replay.
- A shell smoke test starts the binary with a fixture, checks `/`, assets, and a JSON endpoint via a minimal client.
- Four build configurations: Debug, Release, TSan, ASan.

## Risks / Trade-offs

- **inotify can miss events under overflow** → rescan on overflow plus a 30-second reconciliation pass.
- **Tailer reads can block briefly on disk** → bounded per-stream read chunks; control loop remains responsive under normal loads.
- **A huge file could consume memory** → initial read is bounded by ring byte capacity; line size, record count, and byte count are capped.
- **SSE clients can be slow or malicious** → per-client outbound cap, global client cap, disconnect on overflow; no application impact.
- **No authentication is a security risk if bound externally** → loopback default; explicit non-loopback warning; README/design state that a reverse proxy/auth layer is required for remote access.
- **Raw JSONL means no charts/typed metrics initially** → deliberate first-version scope; the UI can render raw lines, and typed decoding is a follow-up change.
- **Filesystem paths contain arbitrary identifiers** → identifiers are validated by the runner, stream ids are URL-safe and never used as paths; all file access uses canonical paths from suite metadata.
- **Atomic writer renames can race the scanner** → read failures are tolerated and retried on the next inotify event or reconciliation.
- **Single-threaded server may stall under many clients** → client, buffer, and request limits bound the work; the first version targets a local observatory.

## Migration and Rollback

Additive: new module, binary, and tests. Existing runner/application behavior is untouched. Rollback removes `nexigen-hub`; run trees and streams remain valid.

## Open Questions

None. Authentication, remote access, typed metrics/traces, persistence, and retention are explicit non-goals for this change.
