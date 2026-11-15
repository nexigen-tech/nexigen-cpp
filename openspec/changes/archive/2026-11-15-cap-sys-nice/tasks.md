# Tasks

## 1. Manifest and child policy

- [x] 1.1 Add `allow_rt_priority` to `AppSpec` and `StartOptions` with JSON round-trip and pass it through the suite launch plan; verify the round-trip and the default `false`.
- [x] 1.2 In the child, before the credential drop, drop `CAP_SYS_NICE` from the bounding set unless opted in (setup code 12 on failure; skip for non-root); verify the bounding bit through `/proc/self/status` when running privileged.

## 2. Documentation and verification

- [x] 2.1 Document the field in `docs/architecture.md` and mark `REQ_EM_009b` implemented for the bounding set in `docs/em-deferred.md`; verify the traceability gate exits 0.
- [x] 2.2 Run `nexigen_app_suite_tests`, `nexigen_exec_tests`, the complexity gate, and `openspec validate --changes --strict`; verify all pass.

## Workflow follow-up

- Archive the change after review.

## 3. Review follow-up

- [x] 3.1 Reject `allow_rt_priority` combined with a non-root `uid` (the credential drop would clear the capability); verify the manifest is refused.
- [x] 3.2 Verify the opt-in reaches `StartOptions` through `resolve_suite_launch`; make the privileged test skip when `CAP_SYS_NICE`/`CAP_SETPCAP` are unavailable instead of failing.

