# ADR 0004 — Application identity is a manifest-driven 16-byte code

- **Status:** Accepted
- **Date:** 2026-11-22
- **Scope:** `nexigen::core`, `nexigen::app`, `nexigen::exec`, `nexigen::com`, `nexigen::logging`,
  `nexigen::metrics`, `nexigen::tracing`, `nexigen::recorder`
- **Change:** `openspec/changes/widen-app-id-to-16-bytes`

## Context

Telemetry attribution currently rests on a four-character tag baked into the policy type:
`Iceoryx2Policy<A,B,C,D>` copies `A..D` into every `LogFrame`, `MetricFrame`, and `SpanFrame`. The
tag cannot follow a suite manifest, cannot exceed four bytes, and is frozen at compile time.

Two production needs break that model:

1. One executable is launched several times with different arguments, and each launch is a distinct
   logical application (for example control loops `CN_LOOP_1` and `CN_LOOP_2`).
2. A suite can run many applications at once, so four characters cannot stay unique and
   human-readable within that suite. `CN_LOOP_1` alone is nine bytes.

The ExecutionManager already delivers `--app-name` to each child, and `Infrastructure` already
passes `app_name` into every policy hook; `Iceoryx2Policy` simply ignores it. Identity therefore
has a path to the sink; it lacks a value and a width.

The wire frames are fixed-size, pointer-free, and zero-copy. `LogFrame` is 256 B and cache-line
aligned, `SpanFrame` is 384 B, `EventHeader` is 72 B and pinned, and `TransportHeader` is 24 B. The
recorder embeds a schema per session, and daemons render `app_id` directly into JSON and Prometheus
labels.

## Decision

1. **Canonical value in `core`.** `core::AppId` is exactly 16 bytes of fixed storage with a
   zero-filled tail, trivially copyable, with `size()`/`text()` derived by trimming at the first
   NUL. Construction from text is fallible and accepts only 1..16 bytes from `[A-Za-z0-9_-]`. It
   never truncates, hashes, or rewrites the input. The type lives in `core` because `com`,
   `logging`, `metrics`, and `tracing` all need it and must not depend on `app`.

2. **Identity comes from the manifest.** `AppSpec` gains an optional `app_id`. An absent value
   defaults to the application name; a name longer than 16 bytes or containing a forbidden byte
   fails validation instead of being clipped. The manifest schema stays `version: 1` because the
   field is additive.

3. **Uniqueness is scoped to the suite and enforced at load.** Duplicates and malformed values
   fail manifest validation with `InvalidArgument`, and nothing starts. Admission does not consult
   other suites and does not reserve identifiers, so two suites may reuse an id. When both publish
   to the same services, daemons merge their streams under one `app` label; that is an explicit
   operator choice, and the documentation recommends distinct ids when it matters. No registry,
   lock directory, or crash-recovery protocol is added.

4. **Delivery is argv, not a new environment variable.** `ExecutionManager::build_argv` appends
   `--app-id <id>` next to `--app-name <name>`. `CommandLineOptions::app_id` populates
   `Settings::app_id`. `NEXIGEN_APP_ID` is deliberately not introduced: one authoritative channel
   avoids a second precedence rule. `RunIdentity` is unchanged because it models run/instance
   paths, and the application name was already excluded from it.

5. **The policy stops owning identity.** `InfrastructurePolicy` hooks take a borrowed
   `app::TelemetryIdentity { app_name, app_id }`. `Iceoryx2Policy` uses the runtime id when set and
   falls back to `A..D` only for standalone applications and tests. The policy remains stateless;
   identity is data, not a type parameter.

6. **The wire field is 16 bytes.** `LogFrame`, `MetricFrame`, `SpanFrame`, `EventHeader`, and
   `TransportHeader` carry `char app_id[16]`. `LogFrame` stays 256 B with a 211-byte payload and
   gains `format_version = 2`; `SpanFrame` stays 384 B by consuming its reserved tail and moves to
   `format_version = 2`; `MetricFrame` grows by 16 bytes; `EventHeader` grows 72 → 80 B;
   `TransportHeader` grows 24 → 40 B. The two same-size frames need the version field because the
   transport's layout check cannot detect their change.

7. **The charset is a wire constraint.** `[A-Za-z0-9_-]` is safe in JSON, in `log.<id>` channel
   topics, and in Prometheus labels. A dot is excluded because it is the recorder topic separator;
   quotes and backslashes are excluded because `nexigen-logd` renders the id with `%.*s` and does not
   escape it.

8. **The id is attribution, not authentication.** Any process can publish another application's
   id. Consumers may use it for diagnostics, filtering, and joins, never for authorization,
   capability grants, or health-claim acceptance. PHM's claim and process credentials remain the
   identity boundary.

## Consequences

**Positive**

- One stable identity per logical application, visible identically in logs, metrics, traces,
  events, recorder channels, and the hub.
- A manifest edit is enough to add or rename a control loop; no recompilation and no policy type
  change.
- Duplicate ids inside one suite fail at load instead of silently merging streams.
- Full-width ids fit; no truncation and no heap allocation on the formation path.
- Recorder sessions stay self-describing, so old recordings remain readable.

**Negative / accepted**

- **BREAKING** wire layout. Producers and consumers must be rebuilt together; old peers are rejected
  by the version fields or the size check.
- **BREAKING** policy hook signature. Custom policies need a one-line mechanical update.
- `LogFrame` payload shrinks from 225 to 211 bytes. Long messages are clipped at the existing frame
  boundary; a larger frame would cost 25% more ring memory.
- Two suites may reuse an id and their streams merge in daemons. Accepted: uniqueness is scoped to
  the suite; operators choose distinct ids when suites share a platform.
- `app_id` uniqueness is not a platform-wide invariant. Nothing prevents a second suite from
  starting with the same code.
- `app_id` can be spoofed; it is not a security control.

## Alternatives considered

- **Keep four bytes and distinguish instances by PID.** Rejected: requires a live metadata lookup,
  breaks standalone applications and archived recordings, and still renders `CN_L` in logs.
- **Use a numeric id or hash plus a registry.** Rejected: compact, but loses readability in logs and
  makes every daemon depend on a registry or `run_dir` metadata to resolve a name.
- **Variable-length string in the frame.** Rejected: kills the fixed-size zero-copy contract.
- **Require `app_id` in every manifest.** Rejected: breaks every existing manifest and example even
  when the application name already fits and is unique.
- **Platform-wide reservation or a shared registry.** Rejected by scope: it adds a runtime
  dependency, a permissions surface, crash-recovery semantics, and its own locking for a guarantee
  the suite boundary does not need. Two suites may reuse an id.
- **Read `NEXIGEN_APP_ID` inside the policy.** Rejected: hidden dependency, untestable precedence,
  and a second source of truth next to argv.
- **Pass `app_id` through each subsystem settings file.** Rejected: the ExecutionManager would have
  to generate the value into JSON files, and settings are user-editable; runtime identity is not a
  user preference.

## References

- `openspec/specs/core/app-id/spec.md` — identifier format, uniqueness, attribution boundary.
- `openspec/specs/exec/app-suite/spec.md` — manifest field, suite-scoped validation, launch.
- `openspec/specs/app/policies/spec.md` — runtime identity and fallback.
- `openspec/specs/logging/iceoryx2-logger/spec.md`,
  `openspec/specs/metrics/iceoryx2-metric-sink/spec.md`,
  `openspec/specs/tracing/iceoryx2-trace-sink/spec.md`,
  `openspec/specs/com/event-system/spec.md` — widened wire contracts.
- `docs/adr/0001-time-source-policy.md` — the platform's precedent for fixed, explicit wire
  contracts.
