# Tasks

## 1. EM heartbeat

- [x] 1.1 Open `Nexigen/EM/Health` in the COM control host and notify event id `1` once per `poll()`; verify an `EventListener` on that service receives the event after one poll.

## 2. Example and matrix

- [x] 2.1 Add `sm` and `em` entries (health services, 1 s cycle, `safety_critical`, `TriggerWatchdogReset`) to `examples/phm/safety_manifest.json`; verify `nexigen-phmd` loads it and supervises three processes.
- [x] 2.2 Promote `RS_PHM_00115`/`00116` to `supported` with the deployment note, and document the EM health channel in `docs/architecture.md`; verify the traceability gate exits 0.

## 3. Verification

- [x] 3.1 Run `nexigen_phm_tests`, `nexigen_suite_control_tests`, `nexigen_phmd_safety_sections`, the complexity gate, and `openspec validate --changes --strict`; verify all pass.

## Workflow follow-up

- Archive the change after review.
