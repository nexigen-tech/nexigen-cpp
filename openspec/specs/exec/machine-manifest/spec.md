# exec/machine-manifest Specification

## Purpose
TBD - created by archiving change p3-arxml-machine-manifest. Update Purpose after archive.

## Requirements

### Requirement: Machine Manifest vocabulary

`nexigen::exec` SHALL expose `MachineManifest`, `Machine`,
`MachineFunctionGroup`, and `MachineProcess`. Each type SHALL provide a
cold-path `to_string()` member plus free helper, ADL `to_json`/`from_json` under
`JSON_NOEXCEPTION=1`, and a `std::formatter` specialization.

#### Scenario: Machine round-trips

- **WHEN** a machine with groups and processes passes through `to_json`, `from_json`, `to_string`, and `std::format`
- **THEN** every name, id, executable, reference, and argument is preserved

#### Scenario: Hostile JSON leaves the machine unchanged

- **WHEN** `from_json` receives a non-object or wrong-typed field
- **THEN** the destination machine keeps its previous values

### Requirement: Machine reference resolution

A process SHALL declare `function_group_ref` in the form
`/Machine/FunctionGroups/<id-or-name>`. Resolution SHALL match the machine
segment to the enclosing machine, then resolve the Function Group by id first
and name second. An empty, malformed, wrong-machine, or missing target SHALL
fail with `InvalidArgument`.

#### Scenario: Reference resolves by id

- **WHEN** a process references `/Host/FunctionGroups/FG_Demo` and `FG_Demo` is a declared id
- **THEN** the process's resolved Function Group is that group's name

#### Scenario: Reference resolves by name

- **WHEN** the reference target matches a Function Group name
- **THEN** the process resolves to that group

#### Scenario: Missing target is rejected

- **WHEN** the reference names a group that does not exist
- **THEN** resolution fails with `InvalidArgument`

### Requirement: Machine validation

Validation SHALL reject an empty machine list, duplicate machine names,
duplicate Function Group names or ids within one machine, duplicate process
names, empty executables, and processes whose resolved Function Group is
missing.

#### Scenario: Duplicate machine is rejected

- **WHEN** two machines share a name
- **THEN** validation fails

#### Scenario: Unresolved process is rejected

- **WHEN** a process has no resolved Function Group
- **THEN** validation fails

### Requirement: Execution cross-check

`validate_execution_against_machine()` SHALL select the named machine, or the
single declared machine, and SHALL require every execution application to have
a machine process with the same name, executable, and Function Group. A
different command, group, or a missing process SHALL fail with
`InvalidArgument`.

#### Scenario: Matching manifests pass

- **WHEN** every execution app maps to the same named machine process with the same command and group
- **THEN** the cross-check succeeds

#### Scenario: Command mismatch fails

- **WHEN** an execution app and its machine process name the executable differently
- **THEN** the cross-check fails

#### Scenario: Missing process fails

- **WHEN** an execution app has no machine process with its name
- **THEN** the cross-check fails
