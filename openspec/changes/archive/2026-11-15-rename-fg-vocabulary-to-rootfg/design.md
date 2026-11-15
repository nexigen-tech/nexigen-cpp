# Design

## Decisions

- **One reserved name:** `RootFG` is the only reserved Function Group name in
  `nexigen_exec`; `is_root_function_group`, `k_root_function_group_name`, and
  `root_function_group_states` replace their `machine_*` counterparts.
- **SM enum:** `sm::RootFG` replaces `sm::PlatformFG`; `StateManager::root_fg()`
  and `root_fg_` are the accessor and member.
- **Lifecycle/query:** `RootLifecycleClient`,
  `InitialRootStateTransitionResultResponse`, and
  `Nexigen/EM/InitialRootStateTransitionResult` replace the Machine-named
  counterparts. The wire service name changes with the type.
- **Machine Manifest stays:** `MachineManifest`, `Machine`, and
  `MachineProcess` are AUTOSAR machine-manifest vocabulary and are not the
  Function Group; they keep their names.
- **No behavior change:** the reserved states, state transition rules, UCM
  blocking, and readiness behavior are unchanged.

## Specs

Because this is an identifier-only rename, the main specs are updated in place
and the change is archived with `skip_specs`. Archived changes keep their
historical names.

## Risks

- Any external manifest or deployment that still uses `MachineFG` will not be
  treated as the reserved group. This is an intentional pre-1.0 breaking rename.
