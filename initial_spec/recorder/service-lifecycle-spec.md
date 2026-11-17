-# Specification: `recorder/service-lifecycle`

## Metadata
- **Module:** `recorder` (daemon: `nexigen-recordd`)
- **Layer:** L3 (integrates with `app`, `exec`, `phm`, `sm`)
- **Build Gates:** Requires `NEXIGEN_ENABLE_COM=ON`
- **Status:** Draft (Proposal)

## Overview
`nexigen-recordd` operates as a managed service under `nexigen_exec` (Execution Manager) and `nexigen_sm` (State Manager). It follows standard platform lifecycle conventions: JSON manifest configuration, `Infrastructure<Policy>` initialization, Function Group state transitions (`Off`, `Standby`, `Recording`, `Draining`), cgroup/CPU isolation, and health reporting to `nexigen_phm`.

---

## Requirements

### REC-SVC-001: Execution Manifest & Resource Isolation
The daemon MUST be launchable via an `exec` suite manifest with strict CPU and memory isolation so that video encoding and disk I/O never preempt real-time workloads.

* **Scenario REC-SVC-001.1: Manifest resource constraints**
  * **Given** a `nexigen_exec` service manifest for `nexigen-recordd`
  * **When** `nexigen-exec` spawns the daemon via `fork`/`setsid`/`execvp`
  * **Then** the process is placed into a dedicated `cgroup` with configured `cpu.max` / `cpuset.cpus` (non-RT cores) and `memory.max` limits prior to `execvp`.
* **Scenario REC-SVC-001.2: CLI & layered configuration merge**
  * **Given** default settings, a JSON service config file (`--config`), and CLI overrides
  * **When** `nexigen_app` initializes the service
  * **Then** configurations are merged deterministically using `nlohmann::json` (`JSON_NOEXCEPTION=1`), and any schema violation returns `core::ErrorCode::InvalidArgument` without throwing exceptions.

### REC-SVC-002: Function Group & State Machine Integration
`nexigen-recordd` MUST participate in `nexigen_sm` arbitration via `nexigen_state_client` (`Nexigen/EM/State/nexigen-recordd` and `Nexigen/EM/StateCommand`).

* **Scenario REC-SVC-002.1: Readiness handshake**
  * **Given** `nexigen-recordd` has initialized its `com::WaitSet`, storage directory, and hardware encoder contexts
  * **When** initialization completes
  * **Then** the daemon reports `Readiness::Ready` to `nexigen_exec` and enters the `Standby` state (subscribed to channels, pre-allocated ring buffers, disk writes idle).
* **Scenario REC-SVC-002.2: Transition to `Recording` state**
  * **Given** the daemon is in `Standby`
  * **When** `nexigen_sm` commands a Function Group transition to `Recording` (e.g., `SessionStart`)
  * **Then** the daemon opens the initial `.nxrec` / `.nxidx` / `.nxvid` segment triplet, begins committing frames, and acknowledges the state transition within the configured timeout.
* **Scenario REC-SVC-002.3: Graceful `Draining` on shutdown or state stop**
  * **Given** an active `Recording` state
  * **When** `nexigen_sm` transitions the service to `Standby`/`Off` or `signalfd` receives `SIGTERM`/`SIGINT`
  * **Then** ingestion stops accepting new frames, encoder and I/O worker queues drain all pending frames, segment footers are finalized with `CRC-32C`, and only then does the daemon acknowledge `Off` and exit.

### REC-SVC-003: Health Monitoring & Storage Guardrails (`phm`)
The service MUST report alive checkpoints and storage health to `nexigen_phm` (`Nexigen/PHM/Claim`) and handle disk exhaustion gracefully.

* **Scenario REC-SVC-003.1: Periodic alive supervision**
  * **Given** active ingestion and disk I/O worker loops
  * **When** each loop completes a cycle without deadlocks
  * **Then** it reports a `phm` alive checkpoint; if any worker stalls beyond `watchdog_timeout`, `phm` triggers the configured recovery action.
* **Scenario REC-SVC-003.2: Low disk space protection**
  * **Given** available filesystem space drops below `min_free_bytes`
  * **When** a new segment rotation is attempted
  * **Then** the daemon either deletes the oldest segments (if `retention_policy = circular`) or halts recording cleanly (if `retention_policy = stop`), emitting a `Critical` log record and a `phm` health status event without crashing.
