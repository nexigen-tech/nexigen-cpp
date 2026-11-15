# Design

## Context

See proposal.md — Why. Verified facts (measured on the current tree):

- `src/exec/app_suite.cpp` is 1718 lines and still holds the pre-extraction
  definitions of everything `extract-child-process-class` moved into
  `suite_manifest.cpp`, `suite_control.cpp`, `suite_reporting.cpp` and
  `suite_supervisor.cpp`.
- **44 strong global symbols** (`nm -C --defined-only`, types `T`/`D`/`B`
  only, weak inline/template copies excluded) are defined in two translation
  units each: 32 `app_suite.cpp`+`suite_manifest.cpp`, 11
  `app_suite.cpp`+`suite_control.cpp`, 1 `app_suite.cpp`+`suite_reporting.cpp`
  (`suite_status`).
- **Which copy runs today is settled by archive order.** `nexigen_exec` is a
  static library and `ar t libnexigen_exec.a` lists
  `app_suite.cpp.o` at position 2 — before `suite_control.cpp.o` (9),
  `suite_manifest.cpp.o` (11) and `suite_reporting.cpp.o` (12). The linker
  resolves an archive member on demand and takes the **first** member that
  defines the symbol, so **the `app_suite.cpp` copies are the currently
  effective implementations for all 44 symbols**; the copies in the extracted
  units are dead code that no binary links today.
- Four pairs have **drifted** (instruction counts differ between the two
  objects): `SuiteManifest::to_string` (118 vs 124), `load_suite_manifest`
  (222 vs 221), `stop_suite` (369 vs 383), `suite_status` (447 vs 446). For
  these the extracted unit's copy is not merely dead — it is a *different*
  function, and picking either one changes behavior.
- The `nexigen::exec::detail::` helpers are **not** duplicated: they are
  `inline` in `include/nexigen/exec/detail/sys_helpers.hpp` and the `.cpp`
  files only carry `using detail::…;`. Their repeated weak symbols are legal
  and out of scope.
- Duplicate-block measurement (custom detector) for the four pairs:
  `app_suite.cpp`↔`suite_manifest.cpp` 711 lines, ↔`suite_supervisor.cpp` 302,
  ↔`suite_control.cpp` 177, ↔`suite_reporting.cpp` 113 — ≈1300 lines total.

## Goals / Non-Goals

**Goals:**
- Exactly one definition per entity with external linkage inside
  `nexigen_exec`; zero strong-symbol duplication.
- One owner per concern: the extracted unit owns what it was created to own.
- Byte-identical runtime behavior: the surviving implementation of every
  symbol is the one that executes today.

**Non-Goals:**
- No header, declaration, signature, vocabulary or wire-format change.
- No change to `suite_supervisor.cpp` / `suite_launch_plan.cpp` beyond the
  blocks that are exact duplicates of `app_suite.cpp`.
- Not the `detail::` inline helpers, not the test harness duplication, not the
  entity-per-file rule (separate work items).

## Decisions

1. **D1 — Ownership map (the target state).** Each symbol is owned by exactly
   one unit, and that unit is the one whose header already declares it:
   - `suite_manifest.cpp` — `SuiteManifest`/`AppSpec`/`SuiteMetadata`/
     `SuiteAppMetadata`/`SuiteStatus`/`SuiteAppStatus`/`SuiteExit`
     `to_string`, the whole `to_json`/`from_json` trio, the `valid_json_*`
     validators, `json_enum_fields_valid`, `attempt_dir_path`,
     `suite_dir_path`, `load_suite_manifest`, `validate_suite_manifest`.
   - `suite_control.cpp` — `start_suite`, `stop_suite`, `stop_suite_apps`,
     `suite_is_running`, `preflight_suite_threads`,
     `run_suite_supervisor_child`, `signal_supervisor_with_grace`,
     `create_suite_pipes`, `effective_stop_grace`, `wait_for_suite_exit`,
     `write_stop_request`.
   - `suite_reporting.cpp` — `suite_status`.
   - `app_suite.cpp` — removed entirely: the extracted units define every
     external entity, and the application-instance lifecycle lives in
     `child_process.cpp` / `suite_supervisor.cpp`, so the file retained no
     owned definitions.
2. **D2 — Drift protocol: the effective body wins.** For the four drifted
   pairs, the implementation that runs today (`app_suite.cpp`'s) is moved
   **into** the owning unit, replacing the owner's stale copy. The owner's
   definition then becomes byte-identical to what already executes, so the
   observable behavior is unchanged by construction. The alternative (keeping
   the owner's copy) would silently switch four functions to code that has
   never run — rejected.
3. **D3 — Bulk deletion is the identical pairs only.** For the 40 symbols
   whose two definitions are the same function, `app_suite.cpp`'s copy is
   deleted outright; the owner's copy stays. Deletion happens after D2, so the
   owner always has a definition in place.
4. **D4 — No new shared header.** The duplicated code is not "extracted into
   a helper"; it is *given back* to the unit that owns it. Introducing a
   common header would re-create the coupling the extraction was meant to
   remove, and would leave `nexigen::exec::detail` as a second home.
5. **D5 — Verification is per-step and mechanical.**
   (a) `nm -C --defined-only` over the built `nexigen_exec` objects: the count
   of strong symbols defined in more than one object must fall to **0**
   (from 44) — the gate for the whole change;
   (b) `ar t` order check: after the change, the owner member must be the one
   that defines the symbol (sanity, not a requirement);
   (c) the duplicate-block detector over `src/` must report zero blocks
   ≥6 lines between `app_suite.cpp` and the four extracted units;
   (d) full matrix: Debug/Release/ASAN/TSAN 37/37, plus the exec-specific
   suites (`nexigen_exec_tests`, `nexigen_app_suite_tests`).

## Risks / Trade-offs

- **[Behavior change on the four drifted pairs]** → D2 keeps the effective
  body; each pair is reconciled and verified individually before the bulk
  deletion, so a regression surfaces at the pair that caused it, not at the
  end of a 1300-line deletion.
- **[Deleting a definition that another unit still calls]** → the declarations
  live in headers (`suite_manifest.hpp`, `suite_control.hpp`,
  `suite_status.hpp`), and the linker will name the missing symbol immediately;
  the compile+link of the whole matrix is the gate.
- **[Accidentally deleting an `app_suite.cpp`-unique symbol]** → the removal
  list is derived mechanically from the `nm` duplicate set, not hand-picked;
  the gate (a) above must return 0 duplicates *and* the link must succeed.
- **[Archive-order dependence]** → after the change each symbol has exactly
  one definition, so archive order stops mattering. That is the structural
  fix; the order observation only tells us which body is currently effective.
- **[Scope creep into the entity-per-file rule]** → `include/nexigen/exec/
  suite_manifest.hpp` still holds two entities (`FunctionGroupConfig`,
  `SuiteManifest`) — recorded as a follow-up, deliberately not fixed here
  (it would move a public header and is a separate change).

## Migration Plan

1. Reconcile the four drifted pairs (D2), one pair per step, each verified by
   the exec suites.
2. Delete the 40 identical duplicate definitions from `app_suite.cpp` (D3).
3. Run the full gate (D5).
4. Rollback = revert this change alone; no data, wire, or settings format is
   involved.

## Open Questions

None. The canonical implementation for each drifted pair is determined by the
archive-order fact (D2), not by a human preference.
