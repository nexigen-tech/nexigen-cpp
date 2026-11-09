# Design

## Context

`nexigen::runner` is a self-contained module: 25 public headers under `include/nexigen/runner/`, 16 sources under `src/runner/`, 4 test TUs under `tests/runner/`, a CMake library `nexigen_runner` (alias `nexigen::runner`), a CLI `nexigen-run` under `tools/nexigen-run/`, and four main-spec capabilities under `openspec/specs/runner/` (`app-suite`, `instance-lifecycle`, `logging`, `process-supervision`). The rename target, `nexigen::exec`, collides with nothing — there is no existing `exec` namespace, directory, target, or spec path.

The rename is purely mechanical: replace the namespace string, relocate the module directories, rename the CMake target/tool, and move the spec capability directories. No behavior changes. See proposal.md for motivation.

## Goals / Non-Goals

**Goals:**
- One consistent name (`exec`) across namespace, directories, CMake target/alias, tool binary, and spec paths.
- A mechanical, compiler-enforced rename: any missed `nexigen::runner` reference is a compile/link error.

**Non-Goals:**
- Do NOT rename types or their file names: `Runner` stays `runner.hpp`/`runner.cpp`; `runner_logging.*`, `app_suite.*`, `suite_*` stay. Only the enclosing namespace and module paths move.
- Do NOT rename the "runner" concept in prose/comments where it refers to the `Runner` class or a child-app "runner", rather than the namespace.
- No behavioral, API-shape, or hot-path change.

## Decisions

**D1 — Whole-identity rename, not namespace-only.** Rename every layer in one change: namespace → directories → CMake target/alias/tool → spec paths. Rationale: the project's convention is that `nexigen::<module>` ↔ `include/nexigen/<module>/` ↔ `nexigen_<module>` ↔ `openspec/specs/<module>/` are aligned; a namespace-only rename would leave the module named "runner" in every other layer and break the file-per-entity/directory mapping.

**D2 — `skip_specs` for the change.** The four `runner/*` capabilities move to `exec/*` as a `git mv` plus a `nexigen::runner`→`nexigen::exec` text update in three of them. This is a path/text relocation, not a behavioral requirement change, so there are no delta specs; the spec-tree move is an implementation task. (Matches the project's `skip_specs: true` precedent for pure refactorings.)

**D3 — Scope boundary of the `runner`→`exec` substitution.** The sed/git-mv sweep replaces, and only replaces:
- `nexigen::runner` → `nexigen::exec` (qualified name + `namespace nexigen::runner` declarations),
- bare namespace references `runner::` → `exec::` and the local convenience alias `namespace runner =` → `namespace exec =`,
- `nexigen/runner/` → `nexigen/exec/` (include directives) and `src/runner/` → `src/exec/` (source paths, including spec-text file references),
- `nexigen_runner` → `nexigen_exec`, `NEXIGEN_RUNNER_SOURCES` → `NEXIGEN_EXEC_SOURCES`, `nexigen::runner` alias → `nexigen::exec`, `EXPORT_NAME runner` → `exec`,
- the tool binary `nexigen-run` → `nexigen-exec` (word-boundary: excludes the prose noun "nexigen-runner"), `nexigen_run_help` → `nexigen_exec_help`,
- `openspec/specs/runner/` → `openspec/specs/exec/`, and `nexigen-run`/`nexigen::runner` mentions in spec text across all capabilities (not only the four relocated ones).

It does NOT rewrite: `Runner`, `runner.hpp`/`runner.cpp`/`runner_logging.*` file names; domain terms `runner.jsonl`, `runner_name`, `.nexigen-run/`-adjacent prose, or "the runner"/"nexigen-runner" prose about the `Runner` class; nor the run-identity env vars `NEXIGEN_RUN_DIR`/`NEXIGEN_RUN_ID` (the noun "run", not the module).

**D4 — Directory relocation via `git mv`.** Move `include/nexigen/runner/` → `include/nexigen/exec/`, `src/runner/` → `src/exec/`, `tests/runner/` → `tests/exec/`, `tools/nexigen-run/` → `tools/nexigen-exec/`, `openspec/specs/runner/` → `openspec/specs/exec/`. Use `git mv` so history is preserved. CMake globs (`file(GLOB ... CONFIGURE_DEPENDS)`) pick up the new paths automatically; the explicit `runner/` source path in `tests/CMakeLists.txt` (`runner/runner_test.cpp` etc.) must be updated to `exec/`.

**D5 — Ordering.** The change is a single compiling unit: relocate directories first, then sweep the namespace/string renames, then fix CMake, then the tool, then the spec paths, then build across all configs. No other active change references `nexigen::runner` in a way that conflicts (verified: `extract-child-process-class` already landed its `ChildProcess` into `src/runner/`, which simply moves with the directory).

## Risks / Trade-offs

- [A 58-file sweep can miss a TU] → compiler + linker enforce completeness (no `nexigen::runner` alias is kept, so a missed reference fails to compile); the task list carries an explicit grep gate (zero hits for `nexigen::runner`, `nexigen_runner`, `nexigen-run`, `NEXIGEN_RUNNER`, `NEXIGEN_RUN_DIR`, `nexigen/runner/`, `openspec/specs/runner`).
- [Spec path move breaks in-flight or archived references] → only `openspec/specs/runner/*` and this change's own artifacts reference the path; the move is `git mv` and the gate confirms no dangling references outside the archive.
- [The `Runner` class reads oddly as `exec::Runner`] → accepted; the user asked for a namespace rename only, and the class name is preserved. Flagged in the proposal; can be revisited separately if desired.

## Migration Plan

1. Land this change as one compiling unit (D4 relocation → D3 sweep → build gates).
2. Rollback = revert the single change (git mv + sed are all revertible; no data/format change).
3. No follow-on work: type-name renames (`Runner` → `Executor`) are explicitly out of scope and can be a later, separate change.

## Open Questions

None.
