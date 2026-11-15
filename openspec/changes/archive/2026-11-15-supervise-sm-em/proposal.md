# Proposal

## Why

`RS_PHM_00115`/`00116` require a watchdog reset when State Management or
Execution Management stops being healthy. The platform side exists — the PHM
watchdog feed gate stops feeding when any armed process is `Expired` — but the
EM never reports a checkpoint, so a deployment cannot declare it as a supervised
process. SM already emits one (`Nexigen/SM/Health`, id 1); EM emits nothing.
The two requirements therefore stay `partial` only because of a missing
heartbeat.

## What Changes

- The suite control host emits a heartbeat checkpoint (event id `1`) on
  `Nexigen/EM/Health` once per control poll, mirroring the SM channel.
- The shipped example PHM manifest (`examples/phm/safety_manifest.json`)
  declares `sm` and `em` as supervised processes on their standard health
  channels, each with an explicit `TriggerWatchdogReset` recovery rule and
  `safety_critical: true`.
- The PHM daemon session test keeps working with the larger example (it checks
  the process count).
- The safety matrix promotes `RS_PHM_00115`/`00116` to `supported` with the
  deployment note (enable the watchdog and declare both daemons).

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `exec/app-suite`: the suite supervisor now emits a health heartbeat on a
  standard channel; this is new observable behavior.

## Impact

- **Code**: `src/exec/control_host_com.cpp` opens one `com::EventNotifier` and
  notifies it once per poll.
- **Performance**: one non-blocking event notification per control poll
  (~tens of microseconds at most, cold supervision path), no allocation, no
  new thread.
- **Memory**: one transport endpoint.
- **Compatibility**: additive. A deployment that does not list the EM channel
  in its PHM manifest is unaffected.
