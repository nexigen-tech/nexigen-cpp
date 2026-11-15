# Tasks

## 1. Identity primitives

- [x] 1.1 Add `CheckpointClaim`, `ClaimPublisher`, `SecurityEvent`, and `SecurityEventKind` with diagnostics and the message assertions; verify a claim round-trips from publisher to subscriber.
- [x] 1.2 Add `checkpoint_claim_verdict` (claimed uid, `/proc/<pid>` real uid, missing pid, empty name); verify each rejection and the matching-uid success.
- [x] 1.3 Add optional `expected_uid` to `SupervisedProcess` with JSON round-trip and a zero-value rejection; verify the round-trip and validation.

## 2. Daemon wiring

- [x] 2.1 Open `Nexigen/PHM/Claim`/`ClaimEvent` and `Nexigen/PHM/Security`/`SecurityEvent` in the daemon; verify claims are drained on the wake event.
- [x] 2.2 Verify claims against the manifest, mark processes verified, publish a security event for unknown or rejected claims, and gate checkpoints for unverified `expected_uid` processes; verify the legacy path is unchanged.

## 3. Documentation and verification

- [x] 3.1 Document the claim channel and the same-uid limitation in `docs/architecture.md`, update `RS_PHM_00118`/`00119` in the matrix, and verify the traceability gate exits 0.
- [x] 3.2 Run `nexigen_phm_tests`, `nexigen_phmd_safety_sections`, the complexity gate, and `openspec validate --changes --strict`; verify all pass.

## Workflow follow-up

- Archive the change after review.

## Workflow follow-up

- Archive the change after review.

## 4. Review follow-up

- [x] 4.1 Reset the verified flag on a lifecycle stop, re-publish the claim from `SupervisedEntity` on an interval, and extract `ClaimRegistry` so the gate is unit-tested (unknown name, foreign uid, missing pid, legacy entry, reset).
- [x] 4.2 Bound diagnostics with `bounded_name` so a claim buffer without a NUL cannot be read past the sample; test a full buffer.
- [x] 4.3 Reject an `expected_uid` entry whose name is empty or does not fit the 32-byte claim buffer; test both bounds.
- [x] 4.4 Document the manifest constraint, the re-claim, and the lifecycle reset in `docs/architecture.md` and the design.
