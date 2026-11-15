# Tasks

## 1. Manifest section

- [x] 1.1 Add `SettingsDigest` (`path`, `sha256`) and the optional `settings_digests` array to `SuiteManifest` with JSON round-trip; verify the round-trip preserves order and fields.
- [x] 1.2 Reject a relative path or a digest that is not 64 hex characters during manifest validation; verify one test per rejection.

## 2. Pre-launch verification

- [x] 2.1 Implement `verify_settings_digests(manifest, error)` with `read_verified` and run it in `start_suite_and_report` before `start_suite_apps`; a mismatch reports `SecurityError` and starts nothing.
- [x] 2.2 Add tests for a matching file, a changed file, and an absent section; verify the matching case passes and the changed case names the file.

## 3. Documentation and verification

- [x] 3.1 Document the section in `docs/architecture.md` and update `RS_SAF_00002`/`RS_SAF_10002` in the matrix; verify the traceability gate exits 0.
- [x] 3.2 Run `nexigen_app_suite_tests`, the complexity gate, and `openspec validate --changes --strict`; verify all pass.

## Workflow follow-up

- Archive the change after review.
