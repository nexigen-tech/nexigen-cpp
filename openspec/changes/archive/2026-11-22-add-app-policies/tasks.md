# Tasks

## 1. Policy headers

- [x] 1.1 Add `app::policies::ConsolePolicy` with console logger/metric/trace wiring and diagnostics; verify `ex_app_wait_loop` builds and `std::format`/`to_json` compile.
- [x] 1.2 Add `app::policies::FilePolicy` with `FileLogger`, JSONL metric/trace sinks, application-name injection, and `InvalidArgument` for empty metric/trace paths; verify the policy satisfies `InfrastructurePolicy`.
- [x] 1.3 Add `app::policies::NullPolicy` with null logger/metric/trace sinks; verify the policy satisfies `InfrastructurePolicy`.
- [x] 1.4 Add `app::policies::Iceoryx2Policy<A,B,C,D>` with `Iceoryx2Logger`, `Iceoryx2MetricSink`, `Iceoryx2TraceSink`, the four-character app tag, and a shared process-wide `com::Node`; verify `ex_app_wait_loop_iceoryx2` builds with `NEXIGEN_ENABLE_COM=ON`.
- [x] 1.5 Add `include/nexigen/app/policies/policies.hpp` for the COM-free policies; verify it compiles without COM headers.

## 2. Examples

- [x] 2.1 Extract the wait-loop body into `examples/app/wait_loop_common.hpp` and switch `ex_app_wait_loop.cpp` to `ConsolePolicy`; verify `ctest -R '^ex_app_wait_loop$'` passes.
- [x] 2.2 Add `examples/app/ex_app_wait_loop_iceoryx2.cpp` using `Iceoryx2Policy<'W','A','I','T'>` and wire it in CMake under `NEXIGEN_ENABLE_COM`; verify `ctest -R '^ex_app_wait_loop_iceoryx2$'` passes in `build-com`.

## 3. Documentation and spec alignment

- [x] 3.1 Document the policy table, file-path requirements, and both examples in `docs/articles/app.md`; verify the document renders and links to the module docs.
- [x] 3.2 Replace the stale `app/application` policy requirement with the implemented three-component contract; verify `openspec validate` reports no spec errors.

## 4. Verification

- [x] 4.1 Run `clang-format --dry-run --Werror` over the policy headers and wait-loop sources; verify it exits 0.
- [x] 4.2 Run the console and iceoryx2 wait-loop CTests in `build-debug`/`build-com`; verify both pass.
- [x] 4.3 Run `openspec validate "add-app-policies"` and verify the change is valid.

## Workflow follow-up

- Archive the change after review, then verify the archived capability in `openspec/specs/app/policies/`.
