# Proposal

## Why

`examples/integration/simulation_suite` still relies on the four-character tags compiled into
`SimTelemetryPolicy` (`GEN0`, `RECV`, `FLKY`). After the platform moved to a manifest-driven
16-byte `app_id`, the generated suite manifest declares no `app_id`, so the ExecutionManager falls
back to the application name (`vector_generator` is exactly 16 bytes, `vector_receiver` and
`flaky_receiver` are 14). That works by accident, but the example no longer demonstrates the
feature it is supposed to show: identity chosen in the manifest, carried into every telemetry
frame, and independent of the executable's compiled fallback tag.

## What Changes

- Add explicit `app_id` values to the generated suite manifest:
  `vector_generator` → `SIM_GEN`, `vector_receiver` → `SIM_RECV`, `flaky_receiver` → `SIM_FLKY`.
- Keep the compiled four-character tags as the standalone fallback only; no wire or policy changes.
- Update the simulation-suite policy comments and the example documentation to say that the
  manifest id wins and the tag is used only when no `--app-id` is supplied.
- Verify the full-stack driver's logs and metrics attribute records to `SIM_*`, not to the
  executable tag or the long application name.
- No **BREAKING** changes: app names, instance paths, health manifests, and service names stay
  as they are.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `examples/simulation-suite`: the generated suite manifest must declare valid, suite-unique
  application identifiers, and the example must use them for telemetry while keeping the compiled
  tag as the standalone fallback.

## Impact

- **Code:** `examples/integration/simulation_suite/run_simulation_suite.sh` (generated manifest);
  comments in `sim_common.hpp`, `ex_sim_generator.cpp`, `ex_sim_receiver.cpp`, and
  `ex_sim_flaky_receiver.cpp`.
- **Behavior:** suite-mode telemetry now reports `SIM_GEN`, `SIM_RECV`, and `SIM_FLKY`; standalone
  smoke runs without `--app-id` keep `GEN0`, `RECV`, and `FLKY`. No transport, frame, API, or
  manifest-schema change.
- **Tests:** existing CTest smoke runs remain valid; the full-stack driver is checked by inspecting
  the generated `suite.json` and daemon output.
- **Docs:** `docs/low-latency-guide.md` references the ids in the reproduction/observation section
  where useful.
