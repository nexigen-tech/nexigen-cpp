# Safety traceability

Mapping from the requirements of `AUTOSAR_FO_RS_Safety` (AUTOSAR FO R24-11,
document 986, 44 pages) to the capabilities of this repository, their evidence,
and an explicit coverage status.

The requirement identifiers and titles live in
[`rs-safety-r24-11.tsv`](rs-safety-r24-11.tsv), pinned to the source PDF by its
SHA-256. This page is the living view; `cmake/check_safety_traceability.py`
fails the build when the two drift apart.

## Status legend

| Status | Meaning | Required fields |
|---|---|---|
| `supported` | The mechanism exists and is exercised by the linked evidence. | capability, evidence |
| `partial` | The mechanism exists with a named limitation. | capability, evidence, note |
| `not-implemented` | The mechanism is absent. The note names the closing change (`closing:`) or an accepted deviation (`deviation:`). | note |
| `out-of-scope` | Excluded by the product boundary. The note states the boundary (`boundary:`). | note |

Two review rules the gate cannot evaluate:

- **Abstract rows are never `supported` alone.** The top-level (7) and
  functional (16) requirements are satisfied only through their derived
  technical requirements, so they carry `partial` and a note naming the derived
  rows, unless every derived row is `supported`.
- **The gate checks structure, not truth.** A green run means the mapping is
  complete and its links resolve; the claim itself is reviewed.

Row summaries are our own paraphrase. The identifiers and short titles come from
the catalog; the AUTOSAR requirement prose is not reproduced.

Capability links point at `openspec/specs/<path>/spec.md`. Evidence links point
at the test or spec that exercises the behaviour.

## Programme map

| Change | Phase | Requirement clusters |
|---|---|---|
| `add-safety-traceability` | 0 | This catalog and matrix; the gate and its wiring. |
| `close-phm-safety-loop` | 0.5 + 1 | HM alive and logical supervision, PHM watchdog, EM recovery server, recovery escalation. |
| `add-integrity-primitives` | 2 | Checksums and digests, verified manifest load, fault-log integrity. |
| `add-e2e-protection` | 3 | E2E profiles, envelopes, event and method protection, result reporting. |

Declared out of scope for the programme: the Classic Platform BSW cluster
(`RS_SAF_311xx` through `313xx`), UCM (`RS_UCM_*`), and PER (`RS_PER_*` and
`RS_SAF_10040`).

## Matrix

| ID | Summary | Status | Capability | Evidence | Note |
|---|---|---|---|---|---|
| RS_SAF_00001 | Safe execution | partial | `phm/supervision`, `sm/lifecycle` | `tests/phm/phm_test.cpp`, `tests/sm/sm_test.cpp` | Abstract; derived rows 10028 and 10031 remain partial. |
| RS_SAF_00002 | Safe configuration | partial | `exec/app-suite`, `phm/recovery` | `tests/exec/app_suite_test.cpp`, `tests/phm/phm_test.cpp` | Manifests and settings files are digest-verified before launch; no periodic re-verification during operation. |
| RS_SAF_00003 | Safe update or upgrade | partial | `sm/ucm-coordination` | `tests/sm/sm_test.cpp` | No update manager; SM only locks a Function Group during an update. |
| RS_SAF_00004 | Safe information exchange | partial | `com/publish-subscribe`, `com/request-response` | `tests/com/com_test.cpp` | E2E-protected wrappers are available per service; the plain ports stay unprotected. |
| RS_SAF_00005 | Detect data corruption | supported | `exec/trusted-executable`, `phm/recovery`, `com/publish-subscribe` | `tests/exec/app_suite_test.cpp`, `tests/phm/phm_test.cpp`, `tests/com/com_test.cpp` | - |
| RS_SAF_00006 | Safe storage | not-implemented | - | - | deviation: persistency is outside the programme boundary. |
| RS_SAF_00007 | Recovery upon failure | partial | `phm/recovery`, `sm/recovery-handler` | `tests/phm/phm_test.cpp`, `tests/sm/sm_test.cpp` | Restart, SafeState, and the PHM watchdog exist; configuration rollback stays out of scope. |
| RS_SAF_10001 | Safe initialization | partial | `exec/app-suite`, `sm/lifecycle` | `tests/exec/app_suite_test.cpp`, `tests/sm/sm_test.cpp` | Abstract; the Classic Platform half is out of scope. |
| RS_SAF_10002 | Verify software and configuration | supported | `exec/trusted-executable`, `exec/app-suite` | `tests/exec/app_suite_test.cpp`, `tests/core/core_test.cpp` | - |
| RS_SAF_10005 | Safe shutdown and termination | partial | `sm/lifecycle`, `exec/process-supervision` | `tests/exec/execution_manager_test.cpp`, `tests/sm/sm_test.cpp` | Abstract; the Classic Platform half is out of scope. |
| RS_SAF_10006 | Safe state transitions | partial | `sm/state-machine`, `sm/lifecycle` | `tests/sm/sm_test.cpp` | Abstract; the Classic Platform half is out of scope. |
| RS_SAF_10008 | Safe resource management | partial | `exec/resource-groups`, `core/thread-management` | `tests/exec/app_suite_test.cpp`, `tests/core/core_test.cpp` | CPU and memory budgets via cgroup v2 plus affinity; no accounting or admission control. |
| RS_SAF_10014 | Safe communication interface | partial | `com/publish-subscribe`, `com/request-response`, `com/service-management` | `tests/com/com_test.cpp` | E2E protection is opt-in per service; the plain ports remain unprotected by design. |
| RS_SAF_10027 | Prevent loss of valid configuration | not-implemented | - | - | deviation: needs persistency and rollback, outside the programme. |
| RS_SAF_10028 | Dependable scheduling | partial | `core/thread-management`, `phm/supervision` | `tests/core/core_test.cpp`, `tests/phm/phm_test.cpp` | SCHED_FIFO and affinity for daemons; no admission control for applications. |
| RS_SAF_10030 | Safe program execution | supported | `phm/supervision` | `tests/phm/phm_test.cpp` | - |
| RS_SAF_10031 | Detect execution-time violation | partial | `phm/supervision` | `tests/phm/phm_test.cpp` | Deadline supervision covers checkpoint windows; no execution-time budgets. |
| RS_SAF_10037 | Prevent unintended alteration | partial | `exec/process-supervision` | `tests/exec/execution_manager_test.cpp` | Process isolation by the OS; E2E detects alteration but does not prevent it. |
| RS_SAF_10038 | Update only in a safe state | not-implemented | - | - | deviation: no update manager; UCM is outside the programme boundary. |
| RS_SAF_10039 | Detect unintended alteration | supported | `exec/trusted-executable`, `phm/recovery`, `com/publish-subscribe` | `tests/exec/app_suite_test.cpp`, `tests/phm/phm_test.cpp`, `tests/com/com_test.cpp` | - |
| RS_SAF_10040 | Data recovery | out-of-scope | - | - | boundary: persistency is outside the product boundary. |
| RS_SAF_10041 | Configurable safety mechanisms | supported | `phm/manifest`, `exec/app-suite`, `com/service-management` | `tests/phm/phm_test.cpp`, `tests/exec/app_suite_test.cpp`, `tests/com/com_test.cpp` | - |
| RS_SAF_10042 | Detect time-sync violations | supported | `chrono/time-sync`, `chrono/time-base`, `chrono/time-domain` | `tests/chrono/chrono_test.cpp`, `tests/chrono/time_base_test.cpp` (`time_base_step_detection`, `kalman2`, `time_base_hardware`), `tests/app/time_base_e2e_test.cpp` | `time_base_e2e_loss` fails when holdover/expiry supervision is removed; the retry hold is covered by `tests/exec/app_suite_test.cpp`. |
| RS_HM_09125 | Alive supervision | supported | `phm/supervision` | `tests/phm/phm_test.cpp` | - |
| RS_HM_09222 | Logical supervision | supported | `phm/supervision` | `tests/phm/phm_test.cpp` | - |
| RS_HM_09235 | Deadline supervision | supported | `phm/supervision` | `tests/phm/phm_test.cpp` | - |
| RS_PHM_00115 | Watchdog reset when SM supervision fails | supported | `phm/supervision` | `tests/phm/phm_test.cpp`, `examples/phm/safety_manifest.json` | - |
| RS_PHM_00116 | Watchdog reset when EM supervision fails | supported | `phm/supervision` | `tests/phm/phm_test.cpp`, `examples/phm/safety_manifest.json` | - |
| RS_PHM_00117 | Notify SM of other failures | supported | `sm/recovery-handler` | `tests/sm/sm_test.cpp` | - |
| RS_PHM_00118 | Accept checkpoints only from the owning process | partial | `phm/identity`, `phm/health-channel` | `tests/phm/phm_test.cpp`, `tools/nexigen-phmd/identity_test.sh` | Per-process services, UID separation, and a /proc-verified claim; a same-UID impostor can still claim another pid. |
| RS_PHM_00119 | Security event on a foreign checkpoint | supported | `phm/identity`, `phm/health-channel` | `tests/phm/phm_test.cpp`, `tools/nexigen-phmd/identity_test.sh` | - |
| RS_EM_00002 | One process per modelled process | supported | `exec/process-supervision` | `tests/exec/execution_manager_test.cpp` | - |
| RS_EM_00005 | OS resource budgets for processes and groups | supported | `exec/resource-groups` | `tests/exec/app_suite_test.cpp`, `tests/exec/execution_manager_test.cpp` | - |
| RS_EM_00008 | Bind all threads to a core set | supported | `exec/resource-groups`, `core/thread-management` | `tests/exec/app_suite_test.cpp`, `tests/core/core_test.cpp` | - |
| RS_EM_00009 | Control child-process creation | not-implemented | - | - | deviation: no seccomp or privilege separation; child creation is unrestricted. |
| RS_EM_00151 | EM at the highest ASIL it supervises | not-implemented | - | - | deviation: ASIL is a process attribute; no certification programme asserts it. |
| RS_SM_00600 | SM at the highest ASIL it manages | not-implemented | - | - | deviation: ASIL is a process attribute; no certification programme asserts it. |
| RS_SM_00601 | Coordinate recovery actions | supported | `sm/recovery-handler`, `sm/lifecycle` | `tests/sm/sm_test.cpp` | - |
| RS_OSI_00201 | Memory budgeting | supported | `exec/resource-groups` | `tests/exec/app_suite_test.cpp`, `tests/exec/execution_manager_test.cpp` | - |
| RS_OSI_00202 | CPU-time budgeting | supported | `exec/resource-groups` | `tests/exec/app_suite_test.cpp`, `tests/exec/execution_manager_test.cpp` | - |
| RS_OSI_00203 | Bind processes to cores | supported | `core/thread-management`, `exec/resource-groups` | `tests/core/core_test.cpp` | - |
| RS_OSI_00206 | Multi-process isolation | supported | `exec/process-supervision` | `tests/exec/child_process_test.cpp` | - |
| RS_PER_00008 | Detect persistency corruption | out-of-scope | - | - | boundary: persistency is outside the product boundary. |
| RS_PER_00009 | Recover corrupted persistency | out-of-scope | - | - | boundary: persistency is outside the product boundary. |
| RS_CM_00223 | E2E-protected events | partial | `com/publish-subscribe`, `com/service-management` | `tests/com/com_test.cpp` | Protected wrappers exist and are configured per service; protection is not applied behind the plain event API. |
| RS_CM_00224 | E2E information for events | supported | `com/publish-subscribe` | `tests/com/com_test.cpp` | - |
| RS_CM_00400 | E2E-protected methods | partial | `com/request-response`, `com/service-management` | `tests/com/com_test.cpp` | Protected client/server wrappers exist; protection is opt-in per service. |
| RS_CM_00401 | E2E information for requests | supported | `com/request-response` | `tests/com/com_test.cpp` | - |
| RS_CM_00403 | Response-deadline supervision | partial | `com/request-response` | `tests/com/com_test.cpp` | The synchronous call exposes a timeout and E2E status; deadline supervision stays application-level. |
| RS_CM_00404 | E2E information for responses | supported | `com/request-response` | `tests/com/com_test.cpp` | - |
| RS_UCM_00008 | Recover from failed activation | out-of-scope | - | - | boundary: no update manager; UCM is outside the product boundary. |
| RS_UCM_00012 | Check software-package consistency | out-of-scope | - | - | boundary: no update manager; UCM is outside the product boundary. |
| RS_UCM_00027 | Recover from interrupted update | out-of-scope | - | - | boundary: no update manager; UCM is outside the product boundary. |
| RS_UCM_00030 | Verify updated software on activation | out-of-scope | - | - | boundary: no update manager; UCM is outside the product boundary. |
| RS_SAF_31101 | Watchdog Manager ASIL inheritance | out-of-scope | - | - | boundary: Classic Platform BSW is not part of this product. |
| RS_SAF_31102 | Watchdog Manager aliveness | out-of-scope | - | - | boundary: Classic Platform BSW is not part of this product; the AP equivalent is RS_HM_09125. |
| RS_SAF_31103 | Watchdog Manager control flow | out-of-scope | - | - | boundary: Classic Platform BSW is not part of this product; the AP equivalent is RS_HM_09222. |
| RS_SAF_31104 | Watchdog Manager deadline | out-of-scope | - | - | boundary: Classic Platform BSW is not part of this product; the AP equivalent is RS_HM_09235. |
| RS_SAF_31201 | Memory protection of applications | out-of-scope | - | - | boundary: Classic Platform BSW is not part of this product; the AP equivalent is process isolation. |
| RS_SAF_31202 | Timing protection of applications | out-of-scope | - | - | boundary: Classic Platform BSW is not part of this product. |
| RS_SAF_31301 | E2E Transformer and library | out-of-scope | - | - | boundary: Classic Platform BSW is not part of this product; AP protection is planned in add-e2e-protection. |
| RS_SAF_31302 | Configurable E2E mechanisms | out-of-scope | - | - | boundary: Classic Platform BSW is not part of this product; AP profiles are planned in add-e2e-protection. |
