# Tasks

## 1. Matrix refresh

- [x] 1.1 Promote `RS_SAF_10030` to `supported` (fork/join landed) and correct the `RS_SAF_10037`, `RS_CM_00403`, and `RS_SAF_00001` notes; verify the traceability gate exits 0.
- [x] 1.2 Refresh `docs/em-deferred.md`: mark `REQ_EM_001`/`REQ_EM_005` implemented, record the partial `REQ_EM_010` state (digests yes, signatures/security log no).

## 2. Verification

- [x] 2.1 Run `python3 cmake/check_safety_traceability.py` and `openspec validate --changes --strict`; verify both exit 0.

## Workflow follow-up

- Archive the change after review.
