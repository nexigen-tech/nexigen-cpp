# Proposal

## Why

The platform can detect supervision failures but cannot reliably react to them.
`PHM→EM` restart requests have no server on the EM side (only a test doubles
one), `TriggerWatchdogReset` is sent to an Execution Manager that owns no
watchdog, and no component opens `/dev/watchdog` at all. That breaks the last
line of defence required by `RS_SAF_00007` and leaves `RS_PHM_00115`/`00116`
unimplementable.

Two supervision modes are also weaker than the source requirements: Alive
counts "at least one checkpoint per window" instead of the configured
frequency (`RS_HM_09125`), and Logical validates pairwise edges only, so it
cannot require that every branch of a fork is entered before the join
(`RS_HM_09222`). Closing the loop is what turns the existing detectors into a
safety mechanism.

## What Changes

- **EM serves recovery requests.** The suite control host opens a
  `Server<RecoveryRequest, RecoveryResponse>` on `Nexigen/PHM/Recovery` and
  honours `RestartProcess` by force-restarting the named supervised
  application, bypassing the "restart only on abnormal termination" rule; an
  unknown name is rejected instead of ignored. The PHM client treats a missing
  or late answer as a failed local recovery and escalates as it already does.
- **The watchdog moves to PHM.** `TriggerWatchdogReset` stops being a request
  to the EM: the daemon owns `/dev/watchdog` and implements the action by
  stopping the feed. The now-dead EM branch is removed, not implemented.
- **New `phm::Watchdog`.** Device open with `WDIOC_SETTIMEOUT`, magic close on
  clean shutdown, feed only while no armed process is `Expired` and the
  monitoring loop is making progress, feed period at most half the timeout, and
  a test seam so unit tests run against a fake device while an integration test
  uses a real one when present.
- **Alive supervision gains count semantics.** The manifest gains an expected
  indication count and count-based margins; the verdict fails when the count
  leaves `[expected - min, expected + max]`. A manifest without the new fields
  keeps the current at-least-one behaviour.
- **Logical supervision gains fork/join.** The manifest can declare fork and
  join checkpoints with their branch entry points; reaching a join without
  every declared branch is a logical failure.
- **PHM manifest grows** optional, backwards-compatible sections for the alive
  counts, fork/join regions, and the watchdog.

Explicitly out of scope: checkpoint identity and the security event
(`RS_PHM_00118`/`00119`). Per-service channels already exist, but peer
authentication needs OS-level least privilege (`REQ_EM_009`, deferred), and a
token scheme inside one UID would be security theatre. The traceability matrix
keeps those rows `partial`/`not-implemented` with that rationale.

## Capabilities

### New Capabilities

- `phm/watchdog`: the daemon owns a hardware watchdog, feeds it only while the
  supervised system is healthy, and stops feeding on an unrecoverable state so
  the platform resets itself.

### Modified Capabilities

- `phm/supervision`: Alive supervision moves to expected-count semantics with
  count margins; Logical supervision gains fork/join regions.
- `phm/manifest`: new optional alive-count, fork/join, and watchdog fields.
- `phm/recovery`: `TriggerWatchdogReset` becomes internal actuation; local
  recovery relies on the EM actually serving the channel.
- `exec/process-supervision`: the EM serves recovery requests and force-restarts
  the named application.

## Impact

- **Code**: `include/nexigen/phm/` (new `watchdog*` headers, `supervisor.hpp`,
  `phm_manifest.hpp`, `recovery.hpp`), `src/phm/`, `src/exec/control_host_com.cpp`,
  `tools/nexigen-phmd/main.cpp`, plus tests in `tests/phm/` and `tests/exec/`.
- **Performance**: `on_checkpoint` gains a bounded count increment and a
  fork/join lookup over pre-allocated vectors — a few compares per checkpoint,
  no allocation, no new syscall. Watchdog feeding is one `write` per period
  (tens of microseconds at most) on the monitoring thread, never on a producer
  path. A `TriggerWatchdogReset` action becomes one less request-response
  round trip, so the failure path is cheaper.
- **Memory**: O(fork × branch) fixed counters per supervised process, allocated
  with the supervisor at startup; no per-cycle growth.
- **Latency**: no change on the checkpoint hot path beyond the added compares;
  the recovery path removes a cross-process call for the watchdog action.
- **Compatibility**: manifest additions are optional and default to the legacy
  behaviour, so existing manifests keep working unchanged. The recovery wire
  format does not change.
