# Tasks

## 1. Machine model and references

- [x] 1.1 Add `MachineProcess`, `MachineFunctionGroup`, `Machine`, and `MachineManifest` with conversion sets.
- [x] 1.2 Implement `/Machine/FunctionGroups/<id-or-name>` resolution.
- [x] 1.3 Validate machines, groups, processes, executables, and resolved references.
- [x] 1.4 Add JSON serialization and round-trip tests.

## 2. ARXML and cross-check

- [x] 2.1 Add `ManifestAccessor::load_machine` and `load_machine_manifest_auto`.
- [x] 2.2 Map `<MACHINE-MANIFEST>` through the generic XML-to-JSON path.
- [x] 2.3 Implement `validate_execution_against_machine`.
- [x] 2.4 Add valid ARXML, unresolved reference, duplicate machine, and cross-check tests.

## 3. CLI and docs

- [x] 3.1 Add `--machine` to `nexigen-exec start` and `nexigen-em`.
- [x] 3.2 Extend the CLI smoke with an ARXML execution + machine pair.
- [x] 3.3 Document the machine profile and reference subset.
- [x] 3.4 Run Debug, Release, TSAN, ASAN and gate-OFF; run complexity/naming/result gates and `graphify update .`.
