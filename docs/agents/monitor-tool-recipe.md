# Monitor tool recipe

How to build a new realtime monitor tool (another bus/channel, another viewer)
so that it is **written correctly on the first pass** and **decomposes correctly
on the first pass**, instead of repeating the logview/canview cycle:

> naive GUI -> 30–118 CCN monolith -> red complexity gate -> separate refactor wave -> green.

This document is the checklist to follow. After the next monitor is delivered,
update the **Refinement log** at the bottom with what actually happened.

Status markers: **[now]** = enforced by the repository today, **[planned]** =
proposed by this recipe, not yet implemented.

---

## 1. Non-negotiables

| Rule | Why |
|---|---|
| A new tool enters git as a compiling **skeleton** in its first commit | canview spent a day as 15.8k untracked lines, outside CI |
| Complexity target **12** per function, hard limit **15** | 15 is the gate; 12 leaves one branch of headroom |
| A new tool's **mean CCN <= 3.0** (target 2.5) | canview sits at 3.29 even after its GUI split |
| No GUI function is allowed to own the frame loop | `run_ui` (logview) reached 118; `run_gui` (canview) 33 |
| Scripted UI flows are data, not `switch` | `drive_view_test` 20 -> 5 with a step table |
| Tool tests are registered in CTest **and** coverage from the first commit | coverage gate scores `tools/`, not only `src/` |
| Every commit keeps `check_complexity.py` green | a red gate for one day costs a whole refactor wave |

### Shared code vs shared discipline

Two different things must not be conflated:

- **Shared code** (frame loop, panes, models, widgets) — deferring it is a
  legitimate strategy while both monitors are being shaped. Deliberate
  duplication avoids locking in an abstraction before the second shape is
  known; logview and canview were intentionally kept independent for exactly
  this reason (first make each correct and pleasant, then decide what to unify).
- **Shared discipline** (one pane = one function, phase entry, step tables,
  CCN budget, tests registered from day one) — this is not coupling and cannot
  be deferred. Duplicated code written as a monolith still fails the gate and
  still costs a refactor wave.

So: **copying is optional; decomposing is not.** A tool that borrows nothing
from its sibling must still be written in the shapes below.

Existing enforcement **[now]**:

```bash
python3 cmake/check_complexity.py            # scans include/ src/ tests/ tools/, fails > 15
cmake --build build-logview --target complexity-check
ctest -R nexigen_complexity_gate
cmake --build build-coverage --target coverage   # line coverage incl. tools/
```

Proposed enforcement **[planned]** (implement before the next monitor):

```bash
python3 cmake/check_complexity.py --changed --warn 12   # only touched files, warn 12-15
python3 cmake/check_complexity.py --allowlist tools/nexigen-<name>/complexity_allowlist.txt
```

plus a pre-commit hook that runs `clang-format` and the changed-file complexity
check (no hooks exist in the repository yet).

---

## 2. Step 0 — OpenSpec change with a function inventory

Before writing code, create the change and put this table in `design.md`
(tool changes only). It is the contract the implementation is reviewed against.

```markdown
## Function inventory & budget

| Unit | Name(s) | Target CCN | Hard CCN |
|---|---|---|---|
| Entry | `main` | 4 | 8 |
| Phase | `initialize_ui_state`, `start_capture`, `run_frame_loop`, `report_result` | 8 | 12 |
| Panes | one `draw_<pane>` per pane, dispatched by a table | 10 | 12 |
| Model | parser/ring/filter/presenter helpers | 12 | 15 |
| Scripted tests | step tables `{drive, advance}` | 8 | 12 |

Module mean CCN: target 2.5, hard 3.0.
Number of panes: <n>. Number of scripted flows: <n>.
```

Also list in `tasks.md`, as the **first** task:

- [ ] Create the skeleton from `tools/support` (or copy `tools/nexigen-canview`
      layout) with empty panes, one CTest entry and a green gate.

---

## 3. Step 1 — Skeleton (first commit)

Create `tools/nexigen-<name>/`:

```text
CMakeLists.txt          # option-gated like nexigen-logview, model + app targets
main.cpp                # CLI/config only; no UI logic
cli.cpp  cli.hpp        # parse + usage text
model/                  # pure: ring, filter, presenters, validation (testable)
backend/                # I/O, capture thread, transport
ui/
  window.cpp            # panes + entry; must call the shared frame loop
tests/
  model_test.cpp        # NX_CHECK harness, registered as nexigen_<name>_tests
  <name>_transport_test.cpp
  ui_*_test.sh          # scripted smoke/views/focus tests (xvfb-run)
```

The skeleton must:

1. build in a GUI-enabled tree (`-DNEXIGEN_BUILD_<NAME>=ON -DNEXIGEN_ENABLE_COM=ON`);
2. pass `check_complexity.py` (empty panes are trivial);
3. register `nexigen_<name>_tests` in CTest and be visible to the coverage run
   (match the coverage filter `^nexigen_.*_tests$` or extend the filter in
   `CMakeLists.txt`);
4. print a stable `--help`/`--version` contract that later tests can diff.

**Do not** start with the GUI monolith "to be split later". That is the exact
failure this recipe exists to prevent.

---

## 4. Step 2 — Model first, UI last

Order: `model/` -> `backend/` -> `ui/`.

- Model functions are pure, cold-path, allocation-free where practical, and
  individually tested (`model_test.cpp`). Target CCN 12.
- Backend owns threads/transport; keep it close to `tools/nexigen-canview/backend`
  (anchor estimator, capture) rather than inventing a new threading model.
- The UI arrives only when the model is green and covered. A pane must be able
  to render a `const Model&` snapshot without parsing or transport calls.

---

## 5. Step 3 — Panes via a table, never one function

The frame loop is a shared, fixed sequence; panes are data.

```cpp
struct PaneSpec {
    const char* label;
    void (*draw)(Model&, const UiOptions&, UiState&);
    bool (*handle_input)(UiState&, const ImGuiIO&);
};
constexpr PaneSpec kPanes[] = { ... };
```

Rules:

- one pane = one `draw_<pane>` function, `SameLine()`/`PushID` scopes fully
  inside it;
- more than ~7 widgets in a strip -> split into a group builder immediately;
- the entry function is a phase list (`initialize`, `capture`, `loop`,
  `report`), not a switch with a loop inside;
- the loop body calls `draw_workspace_frame(...)` once — no per-pane
  conditionals in the loop.

Reference numbers from logview after its refactor: `run_ui` 118 -> 6,
`draw_local_controls` 47 -> 1, `drive_view_test` 20 -> 5.

---

## 6. Step 4 — Tests that make splitting cheap

Register from day one:

1. **Model tests** — `NX_CHECK` harness, same binary naming convention.
2. **Transport test** — real publisher/consumer, skips cleanly without transport.
3. **Scripted UI tests** — `ui_smoke`, `ui_views`, `ui_focus`, `ui_splash`,
   `ui_actions` with `xvfb-run`; each asserts exact output lines.
4. **Golden pane list** **[planned]** — print pane labels/order and diff against
   a checked-in snapshot. This makes any later decomposition mechanical: if the
   golden output is unchanged, the split is safe.

A UI split without golden output is a blind edit; with it, it is a refactor.

---

## 7. Step 5 — Decomposition recipes (use before merge)

Three patterns already paid for in this repository:

### 7.1 Phase orchestrator

`run_*` = calls to <= 8 phase functions, each <= 12 CCN:

`initialize_ui_state` / `start_capture` / `load_splash` / `run_splash_phase` /
`run_actions_test` / `run_frame_loop` / `report_ui_result`.

### 7.2 Descriptor or step table

Replace `switch`-based scripted flows and pane conditionals with a `constexpr`
array of `{drive, advance}` or `{label, draw}`. Cursor fields stay as they were
so debug output is unchanged.

### 7.3 Per-group builder

One control strip -> one builder per group (state badge, actions, filters,
find, time, progress, counters). `draw_local_controls` 47 -> 1.

### 7.4 Tail effect — budget, not one-off

After a split, functions often pile up at 14–15 (logview: 15 -> 17 breaches at
14–18; canview: 0 breaches but 16 functions at 14–15). Continue until:

- no function above 12 without an allowlist entry;
- mean CCN for the tool <= 3.0 (target 2.5).

Allowlist format (one line per exception, reviewed in the PR):

```text
# file:function:ccn:reason
ui/window.cpp:draw_heatmap:14:flat 2D cell switch; split tracked in <change>
```

---

## 8. Step 6 — Merge checklist

Before merging the tool:

- [ ] Gate green: `check_complexity.py` reports 0 functions > 15.
- [ ] Headroom report: list functions in 12–15; every one either split or
      allowlisted with a reason.
- [ ] Tool mean CCN <= 3.0.
- [ ] `clang-format --dry-run --Werror` clean on touched files.
- [ ] Model/transport/UI tests pass; test names unchanged across the split.
- [ ] Golden pane list unchanged by the split.
- [ ] Coverage: tool tests registered, `tools` module >= 85 (or the module floor
      in `CMakeLists.txt`), `coverage` target verdict reported.
- [ ] `--help`/unknown-flag/no-arg CLI outputs unchanged.
- [ ] No untracked WIP older than one day; the tool is committed in small,
      green commits.
- [ ] `graphify update .` run; architecture docs/Mermaid updated if the tool
      adds a new component.

---

## 9. Anti-patterns observed (with numbers)

| Anti-pattern | Observed cost |
|---|---|
| Monolithic `run_gui`/`run_ui` | logview 118 CCN / 318 NLOC; canview 33 / 112 NLOC |
| One strip function with every control | logview `draw_local_controls` 47 |
| `switch` over scripted test phases | logview `drive_view_test` 20, `run_ui_view_test` 38 |
| Duplicating a concept without duplicating its decomposition | `copy_state_from` 27 in logview, then 14 in canview (duplication itself was intentional) |
| WIP kept untracked | 15.8k lines outside git/CI for a day |
| Merging while the gate is red | one full refactor wave (~+600 to +1600 lines) |

---

## 10. Reuse map

- Layout, model/backend/ui split, CLI tests: `tools/nexigen-canview/`.
- Frame loop phases, step tables, per-group control builders, scripted UI
  tests: `tools/nexigen-logview/`.
- Shared GUI dependencies: `cmake/GuiDependencies.cmake` (one ImGui/GLFW pin
  for all GUI tools) — do not add a second `FetchContent` block.
- Shared kit **[planned]**: `tools/support/ui/` — frame loop, `PaneSpec`,
  panel builders, step tables, `view_model_base` (ring/filter/time/clone).
  Until it exists, copy from logview and keep the three recipes above.
- Deliberate duplication is allowed and expected before the unification
  checkpoint (section 11). A sibling tool is not a required source of code,
  but its decomposition discipline still applies.

---

## 11. Unification checkpoint (after both monitors are stable)

Unification is a **separate, explicit decision**, not a side effect of writing
the second tool. Trigger it only when:

- both monitors are shipped and used;
- the UX of each is stable (no open redesign);
- no open complexity-split wave remains in either;
- both have golden-output tests, so a shared regression cannot hide in both.

Then run a comparison inventory and decide item by item:

| Concept | Where it appears | Decision (unify / keep duplicated / later) | Reason |
|---|---|---|---|
| frame loop, `PaneSpec`, step tables | both `ui/` | usually **unify** | pure mechanism, no domain knowledge |
| panel builders (toolbar, table painter) | both `ui/` | usually **unify** | same ImGui idioms, same ordering rules |
| ring/history buffer | `model/` both | **unify if semantics match** | share tests first |
| filter criteria, time presenter | `model/` both | **later** if shapes differ | domain semantics may diverge |
| clone state (`copy_state_from`) | `model/` both | **unify after split** | same concept; share the decomposed form, not the monolith |
| record/sample types | CAN vs log | **keep separate** | different domains; do not build a god-model |
| JSON config / session format | both | **later** | formats may evolve independently |
| GUI dependency pins | `cmake/GuiDependencies.cmake` | **already shared** | one ImGui/GLFW pin for all GUI tools |

Rules:

1. Prefer extracting **mechanism** (loop, tables, builders, test harness) over
   **domain models**.
2. A shared component must have its own tests; a behavior covered only through
   two tools' tests is not covered.
3. Record the unification decision in an ADR — merging two implementations is
   hard to reverse.
4. If in doubt, keep duplicated: the budgets above already keep both healthy.

---

## Refinement log

After the next monitor is delivered, record what actually happened and change
this recipe. Suggested columns:

| Date | Monitor | First-commit size | First GUI commit max CCN | Splits after first commit | Final mean | Headroom 12–15 | Time to green CI | Recipe change |
|---|---|---|---|---|---|---|---|---|
| 2026-11-24 | logview | ~5.4k lines | 118 | 2 waves | ~2.9 | — | 1 day (red) | initial recipe |
| 2026-11-25 | canview (delivered) | 15.8k lines (untracked) | 33 | 1 wave | 3.29 | 16 | 1 day (red) | duplication intentional, unification deferred; gate, CTest and format green at archive |

Questions to answer next time:

1. Did the skeleton actually land first? If not, what blocked it?
2. Which pattern (phase/table/builder) was needed most, and was it obvious
   before writing?
3. Did the golden pane test catch a regression during a split?
4. Did the tool mean stay under budget without an allowlist? Which entries were
   added, and why?
5. What should move from the tool into `tools/support/ui` after this build?
