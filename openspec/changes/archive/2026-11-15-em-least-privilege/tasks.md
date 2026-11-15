# Tasks

## 1. Manifest and launch options

- [x] 1.1 Add optional `uid`/`gid` to `AppSpec` and `StartOptions`, parse/serialize them, and pass them through the suite launch plan; verify the JSON round-trip and that `uid` alone fills `gid`.
- [x] 1.2 Validate the fields: reject `0`, reject `gid` without `uid`, and add `trusted_platform.require_non_root` with JSON round-trip; verify one test per rejection.

## 2. Child credential drop

- [x] 2.1 Apply `setgroups` (only when privileged), `setgid`, `setuid`, and the verification check after all privileged child setup and before `exec`, reporting setup codes 7–10; verify a launch as the current user runs as that user.
- [x] 2.2 Enforce `require_non_root` in `ExecutionManager::start` (`SecurityError` when effective UID is 0 and no `uid` is configured); verify the policy-off default is unchanged.

## 3. Documentation and verification

- [x] 3.1 Document the fields and the policy in `docs/architecture.md`, and record `REQ_EM_009` as implemented for setuid/setgid in `docs/em-deferred.md`; verify the traceability gate exits 0.
- [x] 3.2 Run `nexigen_app_suite_tests`, `nexigen_exec_tests`, the complexity gate, and `openspec validate --changes --strict`; verify all pass.

## Workflow follow-up

- Archive the change after review.
