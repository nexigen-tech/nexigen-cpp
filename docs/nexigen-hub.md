# nexigen-hub

`nexigen-hub` is a read-only observatory for nexigen runs. It watches explicit run
roots, follows the runtime streams written by `nexigen-run`, and serves a local
HTTP/SSE API with an embedded Web UI. It never starts, stops, or modifies
applications.

## Run

```bash
nexigen-hub --run-dir /var/run/nexigen
nexigen-hub --run-dir /var/run/nexigen-a --run-dir /var/run/nexigen-b \
            --listen 127.0.0.1:8080
```

Options:

| Option | Default | Meaning |
|---|---|---|
| `--run-dir <dir>` | required, repeatable | Run root to observe. Duplicates collapse. |
| `--listen <host:port>` | `127.0.0.1:8080` | HTTP listener; `localhost` maps to `127.0.0.1`. |
| `--ring-records <n>` | `4096` | Replay records retained per stream. |
| `--ring-bytes <n>` | `8388608` | Replay bytes retained per stream. |
| `--max-line-bytes <n>` | `1048576` | Oversized lines are dropped and reported as a gap. |
| `--help` | — | Usage text. |

Run the hub in the foreground; use a process manager or systemd unit for
long-running operation. `SIGINT`/`SIGTERM` close clients and exit cleanly.

## Discovery

For each root the hub watches:

- `.suite/*/suite.json`;
- instance control directories;
- `artifacts_path` recorded by each suite, including roots outside the run
  directory;
- `attempt-*` directories and their streams.

The live tree is `run → suite → app → instance → attempt`. State is derived from
`suite-state.json`, `suite-exit.json`, `resource.json`, and `exit.json` and uses
the runner vocabulary (`running`, `degraded`, `failed`, `stopped`, `unknown`).
inotify drives updates; a periodic reconciliation rescans on overflow or missed
events.

## Streams

The hub tails:

```text
<attempt>/app.jsonl
<attempt>/metrics.jsonl
<attempt>/traces.jsonl
<attempt>/chrono.jsonl
<attempt>/stdout.log
<attempt>/stderr.log
<artifacts>/<suite_id>/runner.jsonl
```

Each stream has a bounded replay ring. New subscribers receive recent records,
a snapshot boundary, then live records. Slow subscribers are disconnected
rather than growing memory without bound. Lines are emitted only when complete;
oversized lines become a gap.

## HTTP API

All endpoints are `GET` only.

| Path | Response |
|---|---|
| `/api/health` | `{"status":"ok","version":"nexigen-hub"}` |
| `/api/tree` | Current run/suite/app/instance/attempt tree |
| `/api/streams` | Stream descriptors |
| `/api/streams/<id>/recent?limit=N` | Recent records for one stream |
| `/api/events?stream=<id>&since=N` | SSE replay + live records for one stream |
| `/api/events` | SSE for all streams |
| `/api/events?tree=1` | SSE tree snapshots |
| `/` | Embedded Web UI |
| `/app.js`, `/style.css` | Embedded assets |

SSE events are named: `snapshot_begin`, `record`, `gap`, `snapshot_end`, `tree`,
and periodic `: heartbeat` comments. Record events carry an SSE `id` of
`<stream-id>:<sequence>`; pass it back as `since` (or `Last-Event-ID`) to resume.

### Limits

- 64 concurrent clients;
- 4 KiB request line, 8 KiB headers;
- 1 MiB subscriber buffer;
- 4,096 records / 8 MiB per stream by default;
- 1 MiB maximum line;
- 15 s heartbeat.

## Web UI

Open `http://127.0.0.1:8080/`. The UI shows the run tree and state panels, lets
you pick a stream, replays recent records, follows live records, pauses/resumes,
clears the view, and reconnects with a sequence cursor. It is plain embedded
HTML/CSS/JS: no CDN, no build step, no external requests.

There are no start/stop/restart controls.

## Security

The default listener is loopback-only and there is no authentication. Binding to
a non-loopback address is an explicit operator choice and prints a warning. If
remote access is required, put the hub behind an authenticating reverse proxy;
do not expose it directly to an untrusted network.

## Limitations

- Raw JSONL lines are shown; metrics/traces are not decoded into typed series or
  spans.
- History is in memory only and is lost when the hub restarts.
- No retention, compaction, federation, or multi-host aggregation.
- No control-plane operations; use `nexigen-run` for start/stop/status.
