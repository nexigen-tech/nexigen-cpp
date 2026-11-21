# nexigen

Hard-real-time C++ platform for simulators and control systems. This language describes suite execution: how applications are grouped, launched, supervised, and reported.

## Language

### Suite execution

**Suite**:
A set of Applications and Function Groups described by one manifest, launched and supervised as a unit.
_Avoid_: run, job

**Supervisor**:
The detached process that launches a Suite's Applications and Function Groups and tracks them until the Suite stops.
_Avoid_: runner, orchestrator

**Application**:
A declared executable together with the startup configurations it may be launched under.
_Avoid_: binary, program

**Instance**:
One launch of an Application, owning its directory, generated settings, and resource and exit metadata.
_Avoid_: process, copy

**Function Group**:
A named subset of Applications that share one state vocabulary; the group's state selects which startup configuration each bound Application runs under.
_Avoid_: group, mode

**RootFG**:
The reserved Function Group whose states describe the Suite as a whole rather than a subset of Applications.
_Avoid_: root group

**Resting state**:
The state a Function Group holds before any transition and after a reset: always `Off`.
_Avoid_: initial state, idle state

**Suite-start state**:
The state requested for a Function Group when the Suite starts: its declared initial state, otherwise the first non-`Off` state, and `Startup` for `RootFG`.
_Avoid_: initial state

**Off**:
The inactive state of a Function Group, in which its bound Applications are not started. Every group can rest in `Off` whether or not it declares `Off`.
_Avoid_: stopped, disabled

**SetState**:
A request to move a Function Group to a named state, identified by a request id and answered with an acknowledgement.
_Avoid_: command, transition (the transition is what happens, not the request)

**Restart budget**:
The number of restart attempts an Application is allowed before the Suite reports it as failed.
_Avoid_: retry limit

### Application state

**`AppState`**:
What the Suite reports about an Application: `Running`, `Restarting`, `Exited`, `Stopped`, `Failed`, `Unknown`.
_Avoid_: process state, status

**`ProcessState`**:
The Execution Manager's own lifecycle bookkeeping for a process: `Idle`, `Starting`, `Running`, `Terminating`, `Terminated`.
_Avoid_: app state
