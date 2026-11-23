# Design

## Context

See `proposal.md` — Why. The log path already has a public options API:
`com::Publisher::create(node, name, PublishSubscribeOptions)` and
`com::Subscriber::create(node, name, options)` (`com/publish-subscribe`, requirements
"Publish-subscribe service options" and "Configurable subscriber buffer depth"). Both the logger sink
and every log subscriber currently call the no-options overload, so the service is created with the
transport defaults (`max_subscribers=8`, `max_publishers=2`, `max_nodes=20`,
`subscriber_max_buffer_size=2`, `publisher_history_size=0`) and with
`enable_safe_overflow=false`, which the `com` wrapper sets explicitly. The logger's
`max_subscribers`/`history_size` config fields are copied into the sink settings but never used.

The service shape is fixed while at least one participant is alive; iceoryx2 verifies it when an
existing service is opened (`service/builder/publish_subscribe.rs`, `verify.*`). This makes the options
a cross-process contract, not a local tuning knob.

## Goals / Non-Goals

**Goals:**

- One authoritative definition of the log-service shape, used by every participant.
- 50 000 msg/s survives scheduling jitter without failing a publisher.
- Three or more logging applications can share `Nexigen/Log/nexigen`.
- A slow subscriber loses its oldest frames (visible as sequence gaps), never stops the producer.

**Non-Goals:**

- No `LogFrame` wire change, no history/backlog guarantee, no changes to non-log services.
- No global flip of `com`'s `enable_safe_overflow` default; only the log service opts in.
- No dynamic resizing of a live service.

## Decisions

### D1. One shared options definition, split across the COM boundary

`nexigen_logging` is COM-free: every `src/logging/*.cpp`, including `iceoryx2_logger_config.cpp`, is
compiled into it, so the config header cannot include `nexigen/com`. Two headers keep the layering:

- `include/nexigen/logging/log_service_capacity.hpp` (COM-free, `<cstdint>` only) with
  `kLogServiceMaxSubscribers = 4`, `kLogServiceMaxPublishers = 64`, `kLogServiceMaxNodes = 64`,
  `kLogServiceBufferDepth = 16384`, `kLogServiceHistoryDepth = 4`,
  `kLogServiceSafeOverflow = true`;
- `include/nexigen/logging/log_service_options.hpp` (com-gated) with
  `[[nodiscard]] constexpr com::PublishSubscribeOptions log_service_options() noexcept;`

Rationale: three binaries (logd, recd, viewer) plus every app's logger must agree byte-for-byte. A
header constant cannot drift the way copied literals do. The options factory returns an existing
public type, so no new entity or serialization surface is introduced. The split keeps the COM-free
config TU compilable with `NEXIGEN_ENABLE_COM=OFF`.

Alternatives: (a) put the numbers in `LogDaemonConfig` — then apps and the viewer still need a copy;
(b) let each binary read a JSON file — service compatibility would become runtime-configurable and
could break on a typo. Rejected.

### D2. Capacity numbers

| Option | Value | Why |
|---|---|---|
| `subscriber_max_buffer_size` | 16 384 | ~327 ms of 50k msg/s per subscriber; absorbs wakeup jitter without hiding real lag |
| `max_subscribers` | 4 | logd + recd + viewer + one spare; each unit multiplies the shm segment |
| `max_publishers` | 64 | one logger per application process; the transport default of 2 blocks the third app |
| `max_nodes` | 64 | publishers + subscribers + tools on one service |
| `history_size` | 4 | existing config default (~1 KB); no backlog promise |
| `enable_safe_overflow` | true | required by `com/publish-subscribe` "Non-blocking overflow"; makes the subscriber evict the oldest sample instead of failing `send_copy` |

Memory is per publishing process:
`max_subscribers * (depth + borrowed) + history + loaned` samples, each `LogFrame` 256 B +
`TransportHeader` 40 B + chunk header. At 4 subscribers and 16 384 depth this is about 23 MB of
`/dev/shm` per logging application; it is not resident in the viewer.

### D3. Safe overflow is opted in by the log service only

`PublishSubscribeOptions::enable_safe_overflow` remains `false` by default for other services. The log
service passes `true` through the shared helper. This keeps the change scoped to logging and avoids
silently changing delivery semantics for event, metric, or trace streams.

Consequence: a subscriber that falls behind loses the oldest frames. `TransportHeader::sequence`
increments before the send attempt, so the gap is observable through `Sample::lost_before()` in the
next received frame. The viewer renders these as gap rows.

### D4. Config fields are validated, not authoritative

`Iceoryx2LoggerConfig::max_subscribers` and `history_size` stay in the struct and JSON (diagnostics,
backward-compatible files), but their values must equal the shared constants. The defaults change to
4 and 4; any other explicit value fails validation with `InvalidArgument` before the worker starts.
Silently clamping was rejected: an operator who believes they raised the depth to 16 would otherwise
get a different shape without a signal.

### D5. First participant defines the service; incompatibility is loud

`nexigen-logd` starts first in the platform boot order, so it creates the service. Every participant —
the daemon, the recorder, the viewer, and every application logger — MUST pass `log_service_options()`.
There is no "zero-valued fields" path: `com` always writes `enable_safe_overflow` (false for an
unset option set), and iceoryx2 verifies it when the existing service is opened, so a default-option
participant is rejected by a service created with safe overflow enabled.

If an old process still holds a service created with the old options, the logger and the daemon read
the live shape through `detail::query_log_service_shape()` and fail before creating their port, with
one shared diagnostic (`detail::format_shape_mismatch()`) that names the required and existing
capacities instead of a generic `ConnectionFailure`. The logger stores it in `diagnostic_`/`to_string()`
and exposes it via `startup_diagnostic()`; the daemon prints it to stderr and exits non-zero. A test
starts the daemon against a 2-sample service and asserts both depths appear in the output.

### D6. No new concurrency primitives

The change adds no atomics, no shards, and no hot-path code. The existing `LogRecord`/`LogFrame` layouts
(`alignas(64)`, 256 B) and SPSC shard memory orders are untouched. `log_service_options()` is a
cold-path call executed once during initialization.

## Risks / Trade-offs

- **23 MB shm per publishing app** → one constant, documented formula, and a single place to lower the
  depth. If a target cannot afford it, reducing `max_subscribers` is the first lever (linear), depth
  the second.
- **Old live service has old options** → explicit startup error; operators restart the log daemon (or
  stop all logging processes) before upgrading. The error names the mismatch.
- **Deployed configs with `max_subscribers=16`** → rejected after the upgrade; the migration notes the
  new default 4 and that the value is a service contract, not a local knob.
- **Sequence gaps become visible** → the platform now admits loss instead of hiding it. This is the
  intended contract; consumers (viewer) must render gaps rather than assume a contiguous stream.
- **`dropped_counter` still does not count a failed send** → with safe overflow enabled the send
  failure path should not occur for a full subscriber queue. Residual failures (segment exhaustion)
  remain sequence-visible. A follow-up may fold publish failures into `DroppedStats`.
- **TSAN/ASAN** → no new shared mutable state; the existing logging tests already exercise the
  publisher/subscriber pair under sanitizers.

## Migration Plan

1. Land the header, sink, daemon, recorder, and config changes in one commit; the viewer change builds
   on top.
2. Deployment order: stop the existing `nexigen-logd` and all apps that publish logs, install the new
   binaries, start logd first, then apps. Any service left alive with the old shape produces the D5
   diagnostic.
3. Rollback: stop all log participants, reinstall the previous binaries; the service is recreated with
   the old defaults on the next start. No on-disk format changes.

## Open Questions

None that affect the specs or the task breakdown. The exact chunk-header size (used only for the
memory estimate) is measured once in the test and does not change the design.
