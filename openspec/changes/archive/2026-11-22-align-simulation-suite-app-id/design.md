# Design

## Context

See `proposal.md` — Why. The current state that shapes the change:

- `SimTelemetryPolicy<A,B,C,D>` in `examples/integration/simulation_suite/sim_common.hpp` already
  takes `app::TelemetryIdentity` and uses `identity.app_id` when set, with
  `core::AppId::from_literal(k_tag)` only as the empty fallback.
- `run_simulation_suite.sh` generates the suite manifest and currently omits `app_id`, so
  `resolve_suite_launch` defaults it to the application name.
- The three application names are `vector_generator` (16 bytes, exactly at the limit),
  `vector_receiver` (14), and `flaky_receiver` (14). They validate today, but the example does not
  demonstrate an explicit manifest identity, and a future rename could silently exceed 16 bytes.
- The compiled tags (`GEN0`, `RECV`, `FLKY`) remain useful for the standalone CTest smoke mode,
  which starts an example directly without an ExecutionManager.

## Goals / Non-Goals

**Goals:**

- Make the full-stack simulation suite exercise the manifest-driven `app_id` path end to end.
- Keep standalone smoke runs working without an ExecutionManager.
- Make the example and its documentation say which identity wins.

**Non-Goals:**

- No change to `AppSpec`, wire frames, policies, or daemons.
- No renaming of applications, instances, services, health entities, or Function Groups.
- No new test harness; the existing full-stack driver is the integration check.

## Decisions

### D1 — Explicit identifiers in the generated manifest

Add to each generated application entry:

| Application | `app_id` |
|---|---|
| `vector_generator` | `SIM_GEN` |
| `vector_receiver` | `SIM_RECV` |
| `flaky_receiver` | `SIM_FLKY` |

These are readable, longer than four bytes, unique within the suite, and inside the
`[A-Za-z0-9_-]` alphabet. They deliberately differ from the compiled tags so a test can tell
whether the runtime or fallback path was used.

Alternative considered: rely on the default application name. Rejected because
`vector_generator` already sits exactly on the 16-byte limit, the values are not stable if an
application is renamed, and the example would not demonstrate the feature.

### D2 — Compiled tags stay as the standalone fallback

`SimTelemetryPolicy` keeps its `A..D` template parameters. They are the only identity available
when an example is launched without an ExecutionManager, as in the bounded CTest smoke runs. The
policy already prefers `identity.app_id`, so no code-path change is needed.

Alternative considered: remove the template tags and make the policy non-template. Rejected: it
would require a separate fallback ID in every example translation unit and would not improve the
suite path.

### D3 — Comments and documentation describe precedence

Update the `SimTelemetryPolicy` and per-executable comments from “four-character telemetry tag” to
“standalone fallback tag; the manifest `app_id` wins”. Add a short note to the low-latency guide's
simulation reproduction section so a reader inspecting logs expects `SIM_*`, not `GEN0`/`RECV`/`FLKY`.

## Risks / Trade-offs

- **Suite and standalone identities differ.** A log captured standalone shows `GEN0`, while the
  same executable under the suite shows `SIM_GEN`. → Documented as intentional; the fallback is
  only a fallback.
- **A future reader may treat the tags as the primary identity.** → Comments and the guide now
  name the manifest id as authoritative.
- **No automated assertion on the generated manifest.** → The driver check inspects the generated
  `suite.json` and daemon output; the existing manifest validator already enforces format and
  uniqueness.

## Migration Plan

None. The change only affects the generated example manifest and comments; existing standalone
runs and already-recorded sessions retain their behavior.
