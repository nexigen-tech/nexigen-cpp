# Design

## Context

See `proposal.md` — Why. Two facts drive the approach:

- The gate, the style file and CI all agree on one tool: clang-format 22.1.8 with `file:.clang-format`.
- The mismatch is not version drift: versions 17–22 each fail 115–126 files, and `.clang-format` uses options introduced after 20 (`AlignConsecutiveShortCaseStatements` with nested keys), so the pin is the intended tool and the tree simply predates/ignores it.

The dominant mismatch categories, from sampled diffs:

1. `template <...>` declarations: the tree mixes joined (`template <> struct X {`) and broken (`template <class Ctx>` + newline) spellings. No `BreakTemplateDeclarations` value reproduces both.
2. Short `case`/`default` labels: `AllowShortCaseLabelsOnASingleLine: true` collapses them onto the label line, with `AlignCaseColons: false` + consecutive-statement alignment.
3. Constructor initializer lists and braced-init lists: packing and alignment.
4. One deliberately compressed test harness: `#define NX_CHECK(...) do { ... } while (false)` on a single line in `tests/logging/log_daemon_test.cpp`.

## Goals / Non-Goals

**Goals:**

- `format-check` exits 0 on the committed tree with the existing configuration and version.
- Formatting is idempotent: a second `format` run changes nothing.
- Provable absence of semantic change: build plus the full test matrix, and a whitespace-only diff everywhere except the documented macro case.

**Non-Goals:**

- Changing `.clang-format`, the pinned version, the CI job or the target semantics (no ratchet, no changed-files-only mode).
- Reformatting `openspec/` (documentation) or touching build directories.

## Decisions

### D1 — Bulk reformat with the pinned version instead of aligning tooling to the tree

Pinning CI down to "the version that produced the committed bytes" is impossible: no version 17–22 matches (126/123/117/115/115/115 failing files). Reformatting also has a bounded, known cost (1 925 lines) and leaves every future file checks the same way CI does.

Alternative rejected: a changed-files-only ratchet (`clang-format-diff` against the merge base). It would keep the diff at zero, but it changes what the gate means, freezes 115 files of debt, and needs its own script plus CI wiring.

### D2 — Accept the expanded `NX_CHECK` macro in `log_daemon_test.cpp`

That file is the only one where formatting adds characters (line continuations) rather than only moving whitespace. Fencing it with `// clang-format off` would preserve the compact form but reintroduce an exemption the gate cannot see, and the compressed harness duplicates what every other test TU gets from `logging_test_support.hpp`. Accepting the expansion keeps one style and full gate coverage; the build proves the macro still works.

### D3 — Accept the formatter's string-literal splitting

`BreakStringLiterals` is on in the LLVM base profile, so long expected strings in
`tests/metrics/prometheus_render_test.cpp` are split into adjacent literals:

```cpp
NX_CHECK(contains(text, "request_ms_bucket{...version=\"1.0\",pid="
                        "\"42\",le=\"1\"} 1\n"));
```

C++ concatenates adjacent literals, so the value is identical and the suite proves
it. We keep the profile as it is rather than adding `BreakStringLiterals: false`:
changing the policy is a separate decision, and the split stays greppable enough
because the prefix up to the 100-column boundary is unchanged. Revisit if the
split strings turn out to hurt test review.

### D4 — `.git-blame-ignore-revs` lands in a second commit

The file must contain the hash of the formatting commit, which does not exist until that commit is created. So the sequence is: commit the reformat alone, then add `.git-blame-ignore-revs` with that hash plus a README Formatting note in a follow-up commit. Users opt in with `git config blame.ignoreRevsFile .git-blame-ignore-revs`.

### D5 — Verification order

1. `format` (apply) → `format-check` (must pass).
2. `format` again → `git status` must show no C++ changes (idempotency).
3. Byte-level check: strip every space, tab and newline from the parent and current
   contents of each changed file and compare. This is stricter than `git diff -w`,
   which is line-based and therefore reports the template line-joins as changes.
   Exactly two files may differ: `tests/logging/log_daemon_test.cpp` (macro line
   continuations) and `tests/metrics/prometheus_render_test.cpp` (adjacent string
   literals).
4. Build and `ctest` in Debug, Release (latency gates), TSan and ASan.
5. `check_naming.py`, `check_result_value.py`, `check_complexity.py`, `ctest -R complexity`.

## Risks / Trade-offs

- **Review noise and blame churn** → single formatting-only commit, `.git-blame-ignore-revs`, and `git diff -w` as the review shortcut.
- **A formatter change accidentally alters a macro or a string** → the whitespace-stripped comparison above reduces the whole diff to two known files, and the full build plus test matrix catches macro breakage; clang-format never edits the characters inside a literal, only the boundary between adjacent ones.
- **A parallel branch conflicts wholesale** → the diff is mechanical: re-running `format` after a merge resolves it, no manual conflict policy needed.
- **Latency numbers shift** → formatting cannot change code generation; the Release run re-measures and must reproduce the previous gates (0.50 ns median, 0.25 ns p99.9).
