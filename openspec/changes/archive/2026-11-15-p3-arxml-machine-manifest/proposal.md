# Proposal

## Why

The ARXML accessor currently understands only the execution manifest. AUTOSAR
also separates a Machine Manifest that defines machines, Function Groups, and
processes, and execution manifests reference those definitions. Without the
machine model, an ARXML deployment cannot resolve
`/Machine/FunctionGroups/<id>` references or verify that the execution manifest
actually names processes and groups that exist on the machine.

## What Changes

- Add `MachineManifest`, `Machine`, `MachineFunctionGroup`, and
  `MachineProcess` with the full conversion set.
- Load Machine Manifests from JSON or the ARXML profile and resolve
  `/Machine/FunctionGroups/<id-or-name>` references into concrete Function
  Group names.
- Validate machine names, group names/ids, process names, executables, and
  resolved references.
- Cross-check an Execution Manifest against a Machine Manifest: every
  application must map to a machine process with the same name, command, and
  Function Group.
- Add `--machine <path>` to `nexigen-exec start` and `nexigen-em`; a mismatch
  fails before any suite starts.

## Capabilities

### New Capabilities

- `exec/machine-manifest`: machine model, reference resolution, validation, and
  execution cross-checking.

### Modified Capabilities

- `exec/manifest-accessor`: the accessor also loads Machine Manifests.
- `exec/app-suite`: an execution manifest may be validated against a machine
  manifest before launch.

## Impact

- **Performance:** cold-path parsing and validation only; reference resolution
  is a small linear scan per process before the suite starts.
- **Compatibility:** `--machine` is optional; existing JSON/ARXML starts are
  unchanged.
- **Scope:** the reference syntax is the documented
  `/Machine/FunctionGroups/<id-or-name>` form; full AUTOSAR reference
  resolution (nested packages, instance refs) is deferred.
