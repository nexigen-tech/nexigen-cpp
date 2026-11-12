# Trace Daemon (`nexigen-traced`)

The platform flight recorder. It subscribes to the `SpanFrame` stream produced
by `nexigen::tracing::Iceoryx2TraceSink`, keeps the last seconds of spans in a
bounded RAM ring, and writes one versioned snapshot when the PHM recovery
channel reports an incident. In steady state it performs no disk I/O.

Built only with `NEXIGEN_ENABLE_COM=ON`; the core `nexigen::tracing` library
stays independent of iceoryx2.

## Running

```bash
nexigen-traced --config /opt/nexigen/etc/trace_daemon_config.json
```

`--help` prints usage. The process is launched and supervised by the Execution
Manager; it reports `Running` through `Nexigen/EM/State/<app_name>` once its
ports are open.

## Configuration

```json
{
  "service_name": "nexigen/trace/stream",
  "recovery_service_name": "Nexigen/SM/Recovery",
  "recovery_event_name": "Nexigen/SM/RecoveryEvent",
  "subscriber_max_buffer_depth": 4096,
  "drain_interval_ms": 20,
  "ring_capacity_records": 65536,
  "dump_directory": "/var/log/nexigen/traces",
  "max_dump_files": 8,
  "trigger_on_recovery": true,
  "app_name": "TRCD",
  "bg_cpu_affinity": { "cpus": [0], "mode": "best_effort" }
}
```

- `drain_interval_ms` — WaitSet interval, `5..1000`.
- `ring_capacity_records` — frame slots; 65 536 × 384 B ≈ 24 MiB (about 6.5 s
  at 10 000 spans/s).
- `dump_directory` — absolute snapshot root, created at startup.
- `max_dump_files` — retained snapshots; the oldest is deleted first.
- `trigger_on_recovery` — when false the daemon records but never dumps.
- `bg_cpu_affinity` — `core::ThreadSpec`; empty inherits OS placement.

`mlockall(MCL_CURRENT | MCL_FUTURE)` is attempted best-effort; a denial is
reported on stderr and does not abort startup.

## Snapshot format

Snapshots are named `incident_trace_<YYYYMMDDTHHMMSSZ>.nxtr` (UTC names sort
chronologically; a same-second collision gets a `-<n>` suffix). The writer
uses a `.tmp` file, `fsync`, and `rename`, so a partial file never appears
under the final name.

| Offset | Size | Field |
|---|---:|---|
| 0 | 8 | magic `NXTRA001` |
| 8 | 2 | format version (1) |
| 10 | 2 | header size (96) |
| 12 | 4 | flags (bit 0 = recovery-triggered) |
| 16 | 8 | frame count |
| 24 | 8 | first retained `start_ns` |
| 32 | 8 | last retained `end_ns` |
| 40 | 8 | ring overwrite count |
| 48 | 32 | trigger function group (NUL-terminated) |
| 80 | 8 | trigger timestamp (monotonic ns) |
| 88 | 8 | reserved |
| 96 | `count × 384` | `SpanFrame` records, oldest first |

The header is pointer-free and fixed-width; frames are the same `SpanFrame`
records published on the wire (publisher identity and `version` inline,
source locations as FNV-1a hashes).

## Trigger and shutdown

- A `RecoveryNotification` on `Nexigen/SM/Recovery` (woken through
  `Nexigen/SM/RecoveryEvent`) drains pending spans, writes one snapshot, and
  clears the ring after success. Several notifications in one wake-up coalesce
  into one snapshot naming the last group.
- A failed write leaves no partial file and does not stop the daemon.
- `SIGTERM`/`SIGINT` exit with code 0 and **do not** write a snapshot: a
  graceful stop must not overwrite an incident dump.

## Deferred

On-demand sessions and category gating (`add-trace-session-control`), kernel
ftrace (`add-ftrace-capture`), and Perfetto `.pftrace` rendering
(`add-perfetto-export`) are separate changes. The `.nxtr` snapshot is the
stable input for the renderer.
