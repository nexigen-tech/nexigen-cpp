# Tasks

## 1. Policy provider

- [x] 1.1 Add `SharedNodeProvider` and the optional `NodeProvider` template parameter to `Iceoryx2Policy`; verify `Iceoryx2Policy<A,B,C,D>` still compiles in `ex_app_wait_loop_iceoryx2`.
- [x] 1.2 Generalize `to_string`, `std::formatter`, `to_json` and `from_json` over `NodeProvider`; verify `FailingPolicy` diagnostics compile and pass.
- [x] 1.3 Add `FailingNodeProvider` and assert `emplace_metrics_sink`/`emplace_trace_sink` return `SystemError` without constructing a sink; verify `nexigen_app_policies_com_tests` passes.

## 2. Documentation and verification

- [x] 2.1 Document the optional provider and default in `docs/articles/app.md`; verify the text names `SharedNodeProvider`.
- [x] 2.2 Update the `app/policies` requirement to `Iceoryx2Policy<A,B,C,D,NodeProvider>` and the null-node failure contract; verify `openspec validate` passes.
- [x] 2.3 Run `clang-format --dry-run --Werror` on the policy header and COM test; verify it exits 0.
- [x] 2.4 Run the console and iceoryx2 wait-loop CTests plus the COM policy test; verify all pass.

## Workflow follow-up

- Archive the change after review and verify the updated requirement in `openspec/specs/app/policies/spec.md`.
