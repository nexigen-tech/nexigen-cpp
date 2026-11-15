# Design

## Context

`ManifestAccessor` already converts the execution-manifest ARXML profile into
the JSON shape and reuses `from_json`. The Machine Manifest can follow the same
path: a generic XML-to-JSON conversion, a dedicated `from_json`, then explicit
reference resolution and validation.

## Goals / Non-Goals

**Goals:**

- A machine model with groups and processes.
- Resolve `/Machine/FunctionGroups/<id-or-name>` references.
- Reject unresolved/duplicate/invalid machine data.
- Cross-check the execution manifest before launch.
- Optional `--machine` in both CLI front-ends.

**Non-Goals:**

- Full AUTOSAR reference semantics (packages, instance refs, deployment).
- Multiple machines per process or dynamic machine selection at runtime.
- ARXML schema/XSD validation.
- Machine-level resource or startup configuration beyond processes/groups.

## Decisions

### D1 — Model

```cpp
struct MachineFunctionGroup { name, id, states };
struct MachineProcess { name, executable, function_group_ref, function_group, args };
struct Machine { name, function_groups, processes };
struct MachineManifest { machines };
```

`function_group` is the resolved name; `function_group_ref` preserves the
source reference for diagnostics.

### D2 — Reference syntax

A process reference is `/Machine/FunctionGroups/<id-or-name>`. The resolver
splits on `/`, requires the machine segment to match the enclosing machine, then
looks up the Function Group by id first and name second. An empty or malformed
reference is `InvalidArgument`.

### D3 — ARXML profile

```xml
<MACHINE-MANIFEST>
  <MACHINES>
    <MACHINE>
      <NAME>Host</NAME>
      <FUNCTION-GROUPS>
        <FUNCTION-GROUP>
          <NAME>demo</NAME><ID>FG_Demo</ID>
          <STATES><STATE>Off</STATE><STATE>Running</STATE></STATES>
        </FUNCTION-GROUP>
      </FUNCTION-GROUPS>
      <PROCESSES>
        <PROCESS>
          <NAME>worker</NAME><EXECUTABLE>/bin/sh</EXECUTABLE>
          <FUNCTION-GROUP-REF>/Host/FunctionGroups/FG_Demo</FUNCTION-GROUP-REF>
          <ARGS><ARG>-c</ARG><ARG>sleep 60</ARG></ARGS>
        </PROCESS>
      </PROCESSES>
    </MACHINE>
  </MACHINES>
</MACHINE-MANIFEST>
```

The generic XML-to-JSON mapping treats `MACHINES`, `FUNCTION-GROUPS`,
`PROCESSES`, `STATES`, and `ARGS` as arrays; scalars are strings. JSON uses the
same keys (`machines`, `name`, `function_groups`, `processes`,
`function_group_ref`, `args`).

### D4 — Validation and execution cross-check

`resolve_references()` fills every process's `function_group`.
`validate()` rejects an empty machine list, duplicate machine/group/process
names, empty ids/executables, and unresolved groups.
`validate_execution_against_machine()` selects the requested machine (or the
single machine) and requires every execution application to have a matching
machine process with the same name, executable, and Function Group.

### D5 — CLI

`nexigen-exec start --config <execution> --machine <machine>` and
`nexigen-em --config <execution> --machine <machine>` load and cross-check both
manifests before `start_suite`. A mismatch is a visible start failure; no
process is forked.

### D6 — No hot path or concurrency

Everything is loaded before the supervisor fork. No atomics, locks, or shard
state are introduced.

## Risks / Trade-offs

- **Reference subset** → only the documented form is resolved; other AUTOSAR
  references are rejected rather than guessed.
- **Strict cross-check** → every execution app must have a machine process.
  This is intentional when `--machine` is supplied; omitting the flag keeps the
  previous behavior.
- **Generic XML mapping** → attributes are ignored; the profile uses elements.

## Migration Plan

1. Add the machine model/JSON and reference resolution.
2. Add the ARXML machine loader and tests.
3. Add execution cross-check and CLI `--machine`.
4. Document the profile and run the matrix.

## Open Questions

None blocking. Deployment/instance references and multiple machines are
follow-ups.
