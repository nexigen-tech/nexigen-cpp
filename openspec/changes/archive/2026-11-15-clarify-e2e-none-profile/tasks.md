# Tasks

## 1. Guard

- [x] 1.1 Add the `None` check to `E2ePublisher`, `E2eSubscriber`, `E2eClient`, and `E2eServer` `create()` before any transport call, returning `InvalidArgument`; verify each factory rejects `None`.
- [x] 1.2 Add tests that every wrapper rejects `None` and that a protected profile still opens; verify no service is created for the rejected case.

## 2. Copy and diagnostics

- [x] 2.1 Make `E2ePublisher::publish` write the payload into the loaned slot (one copy, no stack envelope) and drop the unused `send` helper; verify the com tests and the allocation probe still pass.
- [x] 2.2 Move the unknown-profile diagnostic out of `E2eConfig` into `ServiceManifest::diagnostics`, add `valid_service_manifest(manifest, error)`, and verify the config test sees the diagnostic.

## 3. Documentation and verification

- [x] 3.1 Update the E2E section of `docs/architecture.md` with the rule (`None` = plain ports; wrappers reject `None`) and the measured cost as the contract; verify the text matches the spec.
- [x] 3.2 Enforce the contract in `e2e_overhead` (1 us / 2.5 us Release, 5 us / 10 us sanitizer) and verify both builds pass.
- [x] 3.3 Run `nexigen_com_tests`, the complexity gate, and `openspec validate --changes --strict`; verify all pass.

## Workflow follow-up

- Archive the change after review.
