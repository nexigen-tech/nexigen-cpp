# Proposal

## Why

A suite can run many logical applications, and several of them may be the same executable launched
with different arguments. The current telemetry identity is a four-character tag baked into the
policy type (`Iceoryx2Policy<A,B,C,D>`), so it cannot follow the manifest, cannot exceed four bytes,
and silently collides when two applications reuse a short code. Downstream daemons then merge
unrelated streams under one `app` label, and an operator cannot tell which control loop produced a
record. Identity must become a manifest-driven, 16-byte value that is unique within its suite and is
carried from launch through settings, policies, and every telemetry frame.

## What Changes

- Add an `app_id` field to each suite-manifest application entry: 1..16 printable ASCII bytes,
  `[A-Za-z0-9_-]` only, unique within the suite; absent means the application name, and a name that
  does not fit is a manifest error rather than a truncation.
- Enforce uniqueness at manifest load: duplicates and malformed values fail validation with
  `InvalidArgument` and nothing starts. Two different suites MAY reuse the same identifier; no
  cross-suite registry or reservation is performed.
- Deliver the identifier to the child as `--app-id` in argv, alongside the existing `--app-name`;
  add the matching `-ai/--app-id` option and `Settings::app_id` on the application side.
- Make `Iceoryx2Policy` take the identity from runtime settings. `A..D` remain only a fallback for
  standalone applications and tests that never set an `app_id`.
- **BREAKING** widen the application code from 4 to 16 bytes in `LogFrame`, `MetricFrame`,
  `SpanFrame`, `EventHeader`, and `TransportHeader`, and update every reader, recorder schema, and
  diagnostic renderer.
- Add an explicit wire version to `LogFrame` and bump `SpanFrame::format_version` to 2, so a mixed
  producer/consumer pair rejects the frame instead of misreading offsets.
- Document the identity contract, its suite scope, and the rule that `app_id` is an attribution
  label, not an authentication credential.

## Capabilities

### New Capabilities

- `core/app-id`: the canonical 16-byte application identifier — its value representation, format,
  hard length limit, suite-wide uniqueness scope, and the attribution-versus-authentication
  boundary.

### Modified Capabilities

- `app/settings`: `Settings` carries `app_id` next to `app_name`, with an empty value meaning "no
  runtime identity imposed".
- `app/command-line`: new `-ai/--app-id` option, validation, serialization, usage text, and
  application to `Settings`.
- `app/policies`: `Iceoryx2Policy` stamps the runtime `app_id` into every frame and treats the
  template tag only as a fallback; diagnostics keep reporting the effective identity.
- `exec/app-suite`: the manifest schema and validation gain `app_id`, its format and suite
  uniqueness rules, and the launch contract that passes it to the child.
- `exec/process-supervision`: the child launch contract gains `--app-id` in argv and keeps identity
  values authoritative over caller-supplied environment entries.
- `logging/iceoryx2-logger`: `LogFrame` carries a 16-byte application code and a wire version, and
  keeps its 256-byte pinned size.
- `metrics/iceoryx2-metric-sink`: `MetricFrame` and its configuration carry a 16-byte application
  code.
- `tracing/iceoryx2-trace-sink`: `SpanFrame` carries a 16-byte application code in its reserved
  tail, keeps 384 bytes, and moves to `format_version` 2.
- `com/event-system`: `EventHeader` grows from 72 to 80 bytes and carries a 16-byte application
  code.

## Impact

- **Code:** `include/nexigen/core/app_id.hpp` (new value type), `include/nexigen/app/`
  (`app_settings`, `command_line*`, `policies/iceoryx2_policy`),
  `include/nexigen/exec/` (`app_spec`, `suite_manifest`, launch planning), `src/exec/`
  (manifest validation, admission, child argv), `include/nexigen/logging|metrics|tracing/`
  (frames and sink configs), `include/nexigen/com/` (`event_header`, `transport_header`),
  `src/recorder/builtin_schemas.cpp`, `tools/nexigen-logd`, `tools/nexigen-metricsd`,
  `tools/nexigen-traced`, `tools/nexigen-recd`, examples and suite manifests.
- **APIs:** `AppSpec` and `Settings` gain a field; `CommandLineOptions` gains an option;
  `Iceoryx2Policy` keeps its signature but no longer owns identity; `AppSpec`/`Settings`/frame JSON
  and `to_string` outputs gain the new value. Public frame sizes change.
- **Compatibility:** **BREAKING** wire layout. Producers and consumers of log, metric, trace, and
  event frames must be rebuilt together. Recorder sessions remain readable because the schema is
  embedded in each session; only new sessions use the new offsets. Old recordings stay valid.
- **Performance:** no hot-path allocation and no new branch in tick loops. The identity copy stays
  a fixed-size `memcpy` (16 bytes instead of 4) performed where the frame is formed. `LogFrame`
  stays 256 B (payload 225 → 211 B with the version field); `SpanFrame` stays 384 B; `MetricFrame`
  grows by 16 B including alignment; `EventHeader` grows 72 → 80 B; `TransportHeader` grows
  24 → 40 B. Per-subscriber shared-memory cost grows by the frame delta times queue depth; log and
  span rings do not grow. No measurable latency change is expected; the mandated Release latency
  gates must confirm it.
- **Operations:** an id collision inside one suite is an explicit start failure. Two suites may
  reuse an id; their streams will merge in daemons, so operators should choose distinct ids when
  suites run against the same services.
- **Docs:** `GLOSSARY.md` gains `App ID`; `docs/articles/app.md`, `docs/articles/exec.md`,
  `docs/articles/logging.md`, and the suite-manifest reference gain the field and the limit; a new
  ADR records the decision.
